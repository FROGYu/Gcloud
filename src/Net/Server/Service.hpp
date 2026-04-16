#pragma once

#include "Logger/LogMacros.hpp"
#include "Net/Server/Config/Config.hpp"
#include "Net/Server/Data/FileMeta.hpp"
#include "Net/Server/Data/FileTable.hpp"
#include "Util/FileUtil.hpp"
#include "Util/TimeUtil.hpp"
#include "Util/UniqueFd.hpp"

#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>

#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <string>

/*
    Service 所在层级：云存储系统的“网络接入层”。

    这个类负责启动和管理 HTTP 服务。它基于 libevent 创建事件循环和 HTTP 服务对象，
    把所有请求先接进来，再交给成员函数继续处理具体业务。main 函数只需要调用 Init
    完成初始化，再调用 Run 启动服务。
*/
class Service {
 public:
  Service() = default;

  ~Service() { Cleanup(); }

  Service(const Service&) = delete;
  Service& operator=(const Service&) = delete;

  /*
      Init:

      这里完成 HTTP 服务的初始化。它会先清理旧资源，再创建 event_base 和 evhttp，
      然后注册统一的请求回调并绑定监听地址与端口。返回值表示这次初始化是否成功。
  */
  bool Init(const std::string& address = "0.0.0.0", uint16_t port = 8080) {
    Cleanup();

    address_ = address;
    port_ = port;

    // event_base_new: 创建整个 libevent 的事件底座，后面的事件循环都依赖它。
    base_ = event_base_new();
    if (base_ == nullptr) {
      LOG_ERROR("创建 event_base 失败");
      return false;
    }

    // evhttp_new: 基于 base_ 创建 HTTP 服务对象，后续专门用它接收和解析 HTTP 请求。
    http_ = evhttp_new(base_);
    if (http_ == nullptr) {
      LOG_ERROR("创建 evhttp 服务失败");
      Cleanup();
      return false;
    }

    // 注册统一请求入口，并把当前对象地址作为上下文一并交给 libevent 保存。
    evhttp_set_gencb(http_, &Service::HttpRequestCb, this);

    // evhttp_bind_socket: 把 http_ 绑定到指定地址和端口上，开始对外监听。
    if (evhttp_bind_socket(http_, address_.c_str(), static_cast<uint16_t>(port_)) != 0) {
      LOG_ERROR("绑定 HTTP 监听失败, address=%s, port=%u", address_.c_str(),
                static_cast<unsigned>(port_));
      Cleanup();
      return false;
    }

    // 启动阶段主动恢复元数据，避免服务重启后 file_table_ 丢失历史文件记录。
    const std::string& backup_file = Config::Instance().GetBackupFile();
    if (!file_table_.Load(backup_file)) {
      LOG_ERROR("加载文件元数据失败, backup_file=%s", backup_file.c_str());
      Cleanup();
      return false;
    }

    LOG_INFO("HTTP 服务初始化成功, address=%s, port=%u", address_.c_str(),
             static_cast<unsigned>(port_));
    return true;
  }

  /*
      Run:

      这里启动 libevent 的事件循环。主线程会阻塞在这里持续处理 HTTP 请求，直到服务
      正常退出或者中途发生错误。返回值用来表示事件循环是否正常结束。
  */
  bool Run() {
    if (base_ == nullptr || http_ == nullptr) {
      LOG_ERROR("启动 HTTP 服务失败, 服务尚未初始化");
      return false;
    }

    LOG_INFO("HTTP 服务开始事件循环, address=%s, port=%u", address_.c_str(),
             static_cast<unsigned>(port_));
    // event_base_dispatch: 启动事件循环，持续等待并分发 base_ 上注册的网络事件。
    int ret = event_base_dispatch(base_);
    if (ret != 0) {
      LOG_ERROR("事件循环异常退出, ret=%d", ret);
      return false;
    }

    LOG_INFO("HTTP 服务事件循环正常退出");
    return true;
  }

 private:
  /*
      HttpRequestCb:

      这是 libevent 的统一请求入口。收到 HTTP 请求后，先拿到request,再从 arg 中取回当前的
      Service 对象，再把service和request转交给 HandleRequest 处理具体业务。
  */
  static void HttpRequestCb(evhttp_request* request, void* arg) {
    // 这里把回调上下文参数还原成当前 Service 对象，后续才能继续调用成员函数。
    Service* service = static_cast<Service*>(arg);
    if (service == nullptr) {
      if (request != nullptr) {
        // evhttp_send_error: 直接给当前 request 返回错误响应，由 libevent 组装 HTTP 报文。
        evhttp_send_error(request, HTTP_INTERNAL, "Internal Server Error");
      }
      return;
    }

    service->HandleRequest(request);
  }

  /*
      HandleRequest:

      这里执行真正的对 request 处理逻辑。当前这一步先根据请求方法和路径做路由
      分发，把请求交给主页、上传和下载三个处理函数。还没有匹配上的请求统一返
      回 404，后面再继续往各自的处理函数里填具体业务。
  */
  void HandleRequest(evhttp_request* request) {
    const auto start_time = std::chrono::steady_clock::now();

    // evhttp_request_get_command: 从 request 里读取这次 HTTP 请求的方法。
    evhttp_cmd_type command = evhttp_request_get_command(request);
    // evhttp_request_get_uri: 从 request 里读取原始 URI 字符串。
    const char* uri = evhttp_request_get_uri(request);
    if (uri == nullptr) {
      LOG_ERROR("解析请求失败, URI 为空");
      evhttp_send_error(request, HTTP_BADREQUEST, "Bad Request");
      return;
    }

    // evhttp_uri_parse: 把原始 URI 解析成结构化对象，后面才能继续拆路径。
    evhttp_uri* decoded_uri = evhttp_uri_parse(uri);
    if (decoded_uri == nullptr) {
      LOG_ERROR("解析 URI 失败, uri=%s", uri);
      evhttp_send_error(request, HTTP_BADREQUEST, "Bad Request");
      return;
    }

    // evhttp_uri_get_path: 从解析后的 URI 对象里取出纯路径部分。
    const char* path = evhttp_uri_get_path(decoded_uri);
    std::string request_path = (path == nullptr || *path == '\0') ? "/" : path;
    // evhttp_uri_free: 释放上面解析出来的 URI 对象。
    evhttp_uri_free(decoded_uri);

    const char* method_name = "UNKNOWN";
    if (command == EVHTTP_REQ_GET) {
      method_name = "GET";
    } else if (command == EVHTTP_REQ_POST) {
      method_name = "POST";
    }

    LOG_INFO("收到 HTTP 请求, method=%s, path=%s", method_name, request_path.c_str());

    if (command == EVHTTP_REQ_GET && request_path == "/") {
      GetMainPage(request);
    } else if (command == EVHTTP_REQ_POST && request_path == "/upload") {
      UploadFile(request);
    } else if (command == EVHTTP_REQ_GET &&
               request_path.rfind(Config::Instance().GetDownloadPrefix(), 0) == 0) {
      DownloadFile(request);
    } else {
      LOG_ERROR("未匹配到路由, method=%s, path=%s", method_name, request_path.c_str());
      // evhttp_send_error: 直接给当前 request 返回错误响应，由 libevent 组装 HTTP 报文。
      evhttp_send_error(request, HTTP_NOTFOUND, "Not Found");
    }

    const auto end_time = std::chrono::steady_clock::now();
    const auto cost_us =
        std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
    LOG_INFO("完成 HTTP 请求, method=%s, path=%s, cost_us=%lld", method_name, request_path.c_str(),
             static_cast<long long>(cost_us));
  }

  /*
      GetMainPage:

      这里处理主页展示。它先读取服务端 HTML 模板，再遍历 FileTable 快照生成文件
      列表行，最后把列表内容替换进模板占位符并返回给浏览器渲染。
  */
  void GetMainPage(evhttp_request* request) {
    std::string html_content;
    const std::string& template_path = Config::Instance().GetIndexTemplateFile();
    if (!FileUtil::ReadFile(template_path, &html_content)) {
      LOG_ERROR("读取主页模板失败, path=%s", template_path.c_str());
      evhttp_send_error(request, HTTP_INTERNAL, "Internal Server Error");
      return;
    }

    // list_html 保存最终要塞进 {{FILE_LIST}} 占位符里的所有 <tr>...</tr> 行。
    std::string list_html;

    // All 返回的是 FileTable 的快照副本，后面拼 HTML 时不会继续占用 FileTable 的读锁。
    auto files = file_table_.All();
    if (files.empty()) {
      // colspan="4" 表示这一格横跨四列，用一行提示当前没有任何文件。
      list_html = "<tr><td colspan=\"4\">暂无文件</td></tr>";
    } else {
      for (const auto& [filename, meta] : files) {
        // 文件名来自客户端，写进 HTML 前必须转义，避免破坏页面结构。
        const std::string safe_filename = HtmlEscape(filename);

        // href 属性里也要做 HTML 转义，避免特殊字符破坏属性边界。
        const std::string safe_link = HtmlEscape(Config::Instance().GetDownloadPrefix() + filename);

        // FileMeta 里保存的是 time_t，展示到页面前先转成人类可读时间字符串。
        const std::string modify_time = HtmlEscape(TimeUtil::FormatTime(meta.modify_time_));

        // 每个文件对应表格中的一行：文件名、大小、修改时间和下载链接。
        // 开始拼接这一行表格。
        list_html.append("<tr>");
        list_html.append("<td>").append(safe_filename).append("</td>");
        list_html.append("<td>").append(std::to_string(meta.file_size_)).append("</td>");
        list_html.append("<td>").append(modify_time).append("</td>");
        // 第四列：下载链接。
        list_html.append("<td><a href=\"").append(safe_link).append("\">下载</a></td>");
        // 当前文件这一行结束。
        list_html.append("</tr>");
      }
    }
    // 模板里预留的文件列表占位符。
    const std::string placeholder = "{{FILE_LIST}}";
    // 在主页 HTML 模板中查找占位符位置。
    size_t pos = html_content.find(placeholder);
    if (pos == std::string::npos) {
      LOG_ERROR("主页模板缺少文件列表占位符, path=%s", template_path.c_str());
      evhttp_send_error(request, HTTP_INTERNAL, "Internal Server Error");
      return;
    }
    // 用刚刚生成好的文件列表 HTML 替换模板中的占位符。
    html_content.replace(pos, placeholder.size(), list_html);
    // 把替换完成后的完整 HTML 页面返回给浏览器。
    SendHtmlResponse(request, html_content);
  }

  /*
      UploadFile:

      这里处理普通文件上传。它先从请求头里读取文件名和存储类型，再从输入缓冲区
      里拷贝文件正文，最后把文件写入普通存储目录，并把对应的 FileMeta 登记到
      FileTable 中。
  */
  void UploadFile(evhttp_request* request) {
    // evhttp_request_get_input_headers: 从 request 中取出客户端发来的请求头容器。
    evkeyvalq* input_headers = evhttp_request_get_input_headers(request);
    if (input_headers == nullptr) {
      LOG_ERROR("上传失败, 请求头为空");
      evhttp_send_error(request, HTTP_BADREQUEST, "Bad Request");
      return;
    }

    // evhttp_find_header: 从请求头容器里查找指定头部字段。
    const char* file_name = evhttp_find_header(input_headers, "File-Name");
    const char* store_type = evhttp_find_header(input_headers, "Store-Type");
    if (!IsValidUploadFileName(file_name)) {
      LOG_ERROR("上传失败, 文件名非法");
      evhttp_send_error(request, HTTP_BADREQUEST, "Bad Request");
      return;
    }

    if (!IsValidStoreType(store_type)) {
      LOG_ERROR("上传失败, 存储类型非法, file_name=%s", file_name);
      evhttp_send_error(request, HTTP_BADREQUEST, "Bad Request");
      return;
    }

    // evhttp_request_get_input_buffer: 从 request 中取出 POST 请求体(body 正文)所在的输入缓冲区。
    evbuffer* input_buffer = evhttp_request_get_input_buffer(request);
    if (input_buffer == nullptr) {
      LOG_ERROR("上传失败, 请求体缓冲区为空, file_name=%s", file_name);
      evhttp_send_error(request, HTTP_BADREQUEST, "Bad Request");
      return;
    }

    // evbuffer_get_length: 获取输入缓冲区中当前保存的请求体字节数。
    size_t body_length = evbuffer_get_length(input_buffer);
    std::string file_body;
    file_body.resize(body_length);  //用接收到的HTTP请求的正文的长度来初始化我们的string

    // evbuffer_copyout: 把输入缓冲区里的文件正文拷贝到自己的 std::string file_body，不会消耗原缓冲区。
    if (body_length > 0 && evbuffer_copyout(input_buffer, file_body.data(), body_length) !=
                               static_cast<ev_ssize_t>(body_length)) {
      LOG_ERROR("上传失败, 拷贝请求体失败, file_name=%s", file_name);
      evhttp_send_error(request, HTTP_INTERNAL, "Internal Server Error");
      return;
    }

    // 拼接普通存储路径。并且保存
    const std::string real_path = Config::Instance().GetBackDir() + file_name;
    if (!FileUtil::WriteFile(real_path, file_body)) {
      LOG_ERROR("上传失败, 文件落盘失败, file_name=%s, real_path=%s", file_name, real_path.c_str());
      evhttp_send_error(request, HTTP_INTERNAL, "Internal Server Error");
      return;
    }

    FileMeta meta{
        .is_packed_ = false,
        .file_size_ = body_length,
        .modify_time_ = std::time(nullptr),
        .real_path_ = real_path,
    };

    if (!file_table_.Insert(file_name, meta)) {
      file_table_.Update(file_name, meta);
    }

    // 元数据更新成功后立刻刷盘，保证这次上传能在下次重启后被恢复出来。
    const std::string& backup_file = Config::Instance().GetBackupFile();
    if (!file_table_.Store(backup_file)) {
      LOG_ERROR("上传失败, 元数据持久化失败, file_name=%s, backup_file=%s", file_name,
                backup_file.c_str());
      evhttp_send_error(request, HTTP_INTERNAL, "Internal Server Error");
      return;
    }

    LOG_INFO("上传成功, file_name=%s, store_type=%s, size=%zu, real_path=%s", file_name, store_type,
             body_length, real_path.c_str());
    SendTextResponse(request, "Upload Success");
  }

  /*
      DownloadFile:

      这里处理普通文件下载。它先从下载路径中取出文件名，再从 FileTable 查询文件
      元数据，最后通过 evbuffer_add_file 把文件描述符挂到响应缓冲区，避免把大文件
      读进用户态内存。
  */
  void DownloadFile(evhttp_request* request) {
    std::string filename;
    if (!ParseDownloadFileName(request, &filename)) {
      LOG_ERROR("下载失败, 解析下载文件名失败");
      evhttp_send_error(request, HTTP_BADREQUEST, "Bad Request");
      return;
    }

    FileMeta meta;
    if (!file_table_.Get(filename, &meta)) {
      LOG_ERROR("下载失败, 文件不存在, file_name=%s", filename.c_str());
      evhttp_send_error(request, HTTP_NOTFOUND, "File Not Found");
      return;
    }

    // open: 打开磁盘上的真实文件，拿到底层文件描述符交给 libevent 做零拷贝发送。
    UniqueFd file_fd(::open(meta.real_path_.c_str(), O_RDONLY));
    if (!file_fd.valid()) {
      LOG_ERROR("下载失败, 打开文件失败, file_name=%s, real_path=%s", filename.c_str(),
                meta.real_path_.c_str());
      evhttp_send_error(request, HTTP_INTERNAL, "Internal Server Error");
      return;
    }

    // evbuffer_new: 创建响应体缓冲区，后面把文件描述符挂到这个 buffer 上。
    evbuffer* buffer = evbuffer_new();
    if (buffer == nullptr) {
      LOG_ERROR("下载失败, 创建响应缓冲区失败, file_name=%s", filename.c_str());
      evhttp_send_error(request, HTTP_INTERNAL, "Internal Server Error");
      return;
    }

    // evbuffer_add_file: 把 fd 对应文件加入响应缓冲区，libevent 会接管并在发送后关闭 fd。
    //把已经打开的文件，挂到响应缓冲区 buffer 上，准备发给客户端。
    if (evbuffer_add_file(buffer, file_fd.get(), 0, static_cast<ev_off_t>(meta.file_size_)) != 0) {
      LOG_ERROR("下载失败, 挂载文件到响应缓冲区失败, file_name=%s", filename.c_str());
      evbuffer_free(buffer);
      evhttp_send_error(request, HTTP_INTERNAL, "Internal Server Error");
      return;
    }

    // evbuffer_add_file 成功后 fd 已经交给 libevent，UniqueFd 不能再负责关闭它。
    file_fd.release();

    evkeyvalq* output_headers = evhttp_request_get_output_headers(request);
    if (output_headers != nullptr) {
      evhttp_add_header(output_headers, "Content-Type", "application/octet-stream");
      std::string disposition = "attachment; filename=\"" + filename + "\"";
      evhttp_add_header(output_headers, "Content-Disposition", disposition.c_str());
    }

    LOG_INFO("下载成功, file_name=%s, size=%zu, real_path=%s", filename.c_str(), meta.file_size_,
             meta.real_path_.c_str());
    evhttp_send_reply(request, HTTP_OK, "OK", buffer);
    evbuffer_free(buffer);
  }

  /*
      SendTextResponse:

      这里返回纯文本响应。它只指定 text/plain 类型，真正的缓冲区创建、正文写入和
      发送流程交给 SendResponse 统一处理。
  */
  void SendTextResponse(evhttp_request* request, const std::string& body) {
    SendResponse(request, body, "text/plain; charset=utf-8");
  }

  void SendHtmlResponse(evhttp_request* request, const std::string& body) {
    SendResponse(request, body, "text/html; charset=utf-8");
  }

  /*
      SendResponse:

      这里统一处理普通内存响应。调用方只需要传入响应体和 Content-Type，底层仍然复
      用同一套 evbuffer 创建、写入、发送和释放流程。
  */
  void SendResponse(evhttp_request* request, const std::string& body, const char* content_type) {
    // evbuffer_new: 创建响应体缓冲区，后面返回给客户端的正文会先写到这里。
    evbuffer* buffer = evbuffer_new();
    if (buffer == nullptr) {
      LOG_ERROR("创建响应缓冲区失败");
      evhttp_send_error(request, HTTP_INTERNAL, "Internal Server Error");
      return;
    }

    // evhttp_request_get_output_headers: 从 request 中取出响应头容器，后面往这里添加响应头。
    evkeyvalq* output_headers = evhttp_request_get_output_headers(request);
    if (output_headers != nullptr) {
      // evhttp_add_header: 往响应头容器里添加一条头部，让客户端按指定方式解析响应体。
      evhttp_add_header(output_headers, "Content-Type", content_type);
    }

    // evbuffer_add: 把整段响应正文写进响应体缓冲区。
    if (evbuffer_add(buffer, body.data(), body.size()) != 0) {
      LOG_ERROR("写入响应缓冲区失败");
      evbuffer_free(buffer);
      evhttp_send_error(request, HTTP_INTERNAL, "Internal Server Error");
      return;
    }

    // evhttp_send_reply: 把状态码、响应头和 buffer 里的响应体一起发回当前客户端。
    evhttp_send_reply(request, HTTP_OK, "OK", buffer);
    // evbuffer_free: 释放响应体缓冲区，发送完成后这块临时内存就可以回收了。
    evbuffer_free(buffer);
  }

  /*
      HtmlEscape:

      这里把文件名里可能影响 HTML 结构的字符转义掉，避免客户端上传的文件名破坏页面
      结构或形成脚本注入。
  */
  std::string HtmlEscape(const std::string& input) const {
    std::string output;
    output.reserve(input.size() + 16);

    for (char c : input) {
      switch (c) {
        case '&':
          output.append("&amp;");
          break;
        case '<':
          output.append("&lt;");
          break;
        case '>':
          output.append("&gt;");
          break;
        case '"':
          output.append("&quot;");
          break;
        case '\'':
          output.append("&#39;");
          break;
        default:
          output.push_back(c);
          break;
      }
    }

    return output;
  }

  /*
      IsValidUploadFileName:

      这里校验上传文件名是否可以直接拼到存储目录后面。当前先禁止空文件名、路径分隔
      符和 ".."，避免客户端通过文件名把内容写到普通存储目录之外。
  */
  bool IsValidUploadFileName(const char* file_name) const {
    if (file_name == nullptr || *file_name == '\0') {
      return false;
    }

    std::string name(file_name);
    //只允许 file_name 是一个单纯文件名 不允许它带目录 不允许它跳到上级目录
    /*禁止：
            ../a.txt
            dir/a.txt
            dir\a.txt
            ../../evil.txt
    */
    return name.find('/') == std::string::npos && name.find('\\') == std::string::npos &&
           name.find("..") == std::string::npos;
  }

  /*
      IsValidStoreType:

      这里校验客户端传入的存储类型。当前第一版还没有接压缩库，所以 deep 和 low 都会
      先按普通存储落盘，但请求头本身必须是约定好的值。
  */
  bool IsValidStoreType(const char* store_type) const {
    if (store_type == nullptr) {
      return false;
    }

    std::string type(store_type);
    return type == "deep" || type == "low";
  }

  /*
      ParseDownloadFileName:

      这里从下载请求的 URI 中提取文件名。它会重新解析 URI，取出纯路径，然后去掉配置
      中的下载前缀，得到真正要查 FileTable 的文件名。
  */
  bool ParseDownloadFileName(evhttp_request* request, std::string* filename) const {
    if (filename == nullptr) {
      return false;
    }

    const char* uri = evhttp_request_get_uri(request);
    if (uri == nullptr) {
      return false;
    }

    evhttp_uri* decoded_uri = evhttp_uri_parse(uri);
    if (decoded_uri == nullptr) {
      return false;
    }

    const char* path = evhttp_uri_get_path(decoded_uri);
    std::string request_path = (path == nullptr || *path == '\0') ? "/" : path;
    evhttp_uri_free(decoded_uri);

    const std::string& prefix = Config::Instance().GetDownloadPrefix();
    if (request_path.rfind(prefix, 0) != 0) {
      return false;
    }

    *filename = request_path.substr(prefix.size());
    return IsValidUploadFileName(filename->c_str());
  }

  /*
      Cleanup:

      这里释放当前 Service 持有的 libevent 资源。它会销毁 HTTP 服务对象和事件底
      座，避免资源泄漏，也让 Init 在重复调用时能够从干净状态重新开始。
  */

  void Cleanup() {
    if (http_ != nullptr) {
      // evhttp_free: 释放 HTTP 服务对象以及它内部持有的 HTTP 相关资源。
      evhttp_free(http_);
      http_ = nullptr;
    }

    if (base_ != nullptr) {
      // event_base_free: 释放事件底座，归还整个事件循环占用的资源。
      event_base_free(base_);
      base_ = nullptr;
    }
  }

 private:
  // base_ 是 libevent 的事件底座，底层负责调度 epoll 等 IO 多路复用机制。
  event_base* base_ = nullptr;

  // http_ 负责把网络字节流解析成 HTTP 请求对象，供上层直接处理业务。
  evhttp* http_ = nullptr;

  // 保存监听地址和端口，便于启动日志和后续诊断。
  std::string address_ = "0.0.0.0";
  uint16_t port_ = 8080;

  // file_table_ 保存当前服务已经接收的文件元数据。
  FileTable file_table_;
};
