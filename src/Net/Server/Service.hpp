#pragma once

#include "Logger/LogMacros.hpp"
#include "Net/Server/Config/Config.hpp"
#include "Net/Server/Data/FileMeta.hpp"
#include "Net/Server/Data/FileTable.hpp"
#include "Util/FileUtil.hpp"
#include "Util/HttpRange.hpp"
#include "Util/ThreadPool.hpp"
#include "Util/TimeUtil.hpp"
#include "Util/UniqueFd.hpp"
#include "Util/ZstdUtil.hpp"

#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>

#include <fcntl.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <queue>
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

    // eventfd: 创建一个“跨线程通知计数器”，后台线程往里写，主线程就能被唤醒。
    notify_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (notify_fd_ < 0) {
      LOG_ERROR("创建 eventfd 失败");
      Cleanup();
      return false;
    }

    // event_new: 把 notify_fd_ 包成 libevent 事件，后面只要这个 fd 可读就会回调。
    notify_event_ =
        event_new(base_, notify_fd_, EV_READ | EV_PERSIST, &Service::OnNotifyFdRead, this);
    if (notify_event_ == nullptr) {
      LOG_ERROR("创建 eventfd 监听事件失败");
      Cleanup();
      return false;
    }

    // event_add: 把 eventfd 监听事件正式挂到事件循环里。
    if (event_add(notify_event_, nullptr) != 0) {
      LOG_ERROR("注册 eventfd 监听事件失败");
      Cleanup();
      return false;
    }

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
      OnNotifyFdRead:

      这是主线程监听 eventfd 的回调入口。后台线程把完成任务写进队列后，只需要往
      eventfd 里写一个数字，libevent 就会唤醒主线程并进入这里。
  */
  static void OnNotifyFdRead(evutil_socket_t fd, short events, void* arg) {
    Service* service = static_cast<Service*>(arg);
    if (service == nullptr) {
      return;
    }

    service->HandleNotifyFdRead(fd, events);
  }

  /*
      OnAsyncRequestComplete:

      这里处理“异步持有的 request 已经完成回包”的收尾动作。因为 deep 上传会把
      request 的生命周期延长到后台任务结束后，所以真正写完响应后要在这里显式释放。
      供evhttp_request_set_on_complete_cb使用的响应完成的处理函数
  */
  static void OnAsyncRequestComplete(evhttp_request* request, void* arg) {
    (void)arg;

    // evhttp_request_is_owned: 判断这个 request 的生命周期是不是已经被我们手动接管了。
    // 只有调用过 evhttp_request_own 的 request，才应该在这里由我们自己释放。
    if (request != nullptr && evhttp_request_is_owned(request)) {
      // evhttp_request_free: 释放被 own 过的 request，结束这次异步请求的生命周期。
      evhttp_request_free(request);
    }
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
      HandleNotifyFdRead:

      这里处理 eventfd 可读事件。当前这一步先把 eventfd 计数读空，确认后台线程确
      实发来了完成通知，并记录当前完成队列里积压了多少个请求，后面再继续补“取队列
      -> 回响应”的主线程逻辑。
  */
  void HandleNotifyFdRead(evutil_socket_t fd, short events) {
    (void)events;

    uint64_t ready_count = 0;
    // eventfd 每次可读都必须把计数读出来，不然这个可读事件会一直重复触发。
    ssize_t n = ::read(static_cast<int>(fd), &ready_count, sizeof(ready_count));
    if (n != static_cast<ssize_t>(sizeof(ready_count))) {
      LOG_ERROR("读取 eventfd 失败");
      return;
    }

    size_t queue_size = 0;
    {
      std::lock_guard<std::mutex> lock(completed_mutex_);
      queue_size = completed_results_.size();
    }

    LOG_INFO("收到后台完成通知, ready_count=%llu, completed_queue_size=%zu",
             static_cast<unsigned long long>(ready_count), queue_size);

    while (true) {
      CompletedResult result;

      {
        std::lock_guard<std::mutex> lock(completed_mutex_);
        // eventfd 一次可读可能对应多个后台任务完成，这里要把当前积压的结果都取干净。
        if (completed_results_.empty()) {
          break;
        }

        // 这里一次性取出“该给谁回包”和“要回什么结果”，主线程后面不需要再额外查表。
        result = std::move(completed_results_.front());
        completed_results_.pop();
      }

      if (result.request_ == nullptr) {
        continue;
      }

      // 真正的 HTTP 回包必须回到 libevent 主线程执行，不能让后台线程直接碰 request。
      if (result.success_) {
        SendTextResponse(result.request_,
                         result.message_.empty() ? "Upload Success" : result.message_);
      } else {
        evhttp_send_error(result.request_, HTTP_INTERNAL,
                          result.message_.empty() ? "Internal Server Error"
                                                  : result.message_.c_str());
      }
    }
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

      这里处理文件上传。它先从请求头里读取文件名和存储类型，再从输入缓冲区里拷
      贝文件正文。low 直接写入普通目录，deep 会先压缩再写入深度存储目录，最后把
      对应的 FileMeta 登记到 FileTable 中。
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

    std::string real_path;
    bool is_packed = false;

    if (std::string(store_type) == "low") {
      // low 表示普通存储，原始文件直接落到 backdir。
      real_path = Config::Instance().GetBackDir() + file_name;
      if (!FileUtil::WriteFile(real_path, file_body)) {
        LOG_ERROR("上传失败, 普通文件落盘失败, file_name=%s, real_path=%s", file_name,
                  real_path.c_str());
        evhttp_send_error(request, HTTP_INTERNAL, "Internal Server Error");
        return;
      }
    } else {
      // deep 走异步路径：主线程只负责收完请求体并把任务扔给线程池，不再同步压缩和写盘。
      const std::string file_name_str(file_name);

      // evhttp_request_set_on_complete_cb: 给 request 注册“响应真正发完之后”的收尾回调。
      // deep 上传不会在当前栈帧里立刻回包，所以要把释放动作延后到异步回包完成之后。
      evhttp_request_set_on_complete_cb(request, &Service::OnAsyncRequestComplete, this);

      // evhttp_request_own: 把 request 的生命周期从 libevent 默认流程里接管出来。
      // 这样 UploadFile 当前 return 之后，request 也不会立刻被 libevent 回收。
      evhttp_request_own(request);

      // Enqueue: 把 deep 上传后续的重任务提交给线程池。主线程到这里不再继续压缩和写盘，
      // 只负责把当前请求需要的数据按值带进 lambda，交给后台线程慢慢处理。
      // this: 后台线程后面还要回调当前 Service 的成员函数。
      // file_body: 这次上传的原始文件正文，后台线程压缩时直接用它。
      // file_name_str: 这次上传的文件名，后台线程后面要拿它拼压缩文件路径和更新元数据。
      // request: 当前 HTTP 请求对象，后台线程做完后要靠它通知主线程“该给谁回包”。
      if (!thread_pool_.Enqueue([this, file_body, file_name_str, request]() {
            this->HandleAsyncDeepUpload(file_body, file_name_str, request);
          })) {
        LOG_ERROR("上传失败, 异步任务入队失败, file_name=%s", file_name);
        evhttp_send_error(request, HTTP_INTERNAL, "Internal Server Error");
        return;
      }

      LOG_INFO("上传任务已异步提交, file_name=%s, store_type=%s, size=%zu", file_name, store_type,
               body_length);
      return;
    }

    FileMeta meta{
        // file_size_ 继续记录原始大小，主页展示和后续业务都以用户上传的原始文件为准。
        .is_packed_ = is_packed,
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
      HandleAsyncDeepUpload:

      这里运行在后台线程里，专门负责 deep 上传的重任务：压缩、写压缩文件、更新元
      数据并持久化。任务做完后，它不会直接回 HTTP 响应，而是把结果塞进完成队列，
      再通过 eventfd 唤醒主线程。
  */
  void HandleAsyncDeepUpload(const std::string& file_body, const std::string& file_name,
                             evhttp_request* request) {
    bool success = false;
    std::string message = "Internal Server Error";

    do {
      std::string packed_body;
      if (!ZstdUtil::Compress(file_body, &packed_body)) {
        LOG_ERROR("异步上传失败, 压缩文件失败, file_name=%s", file_name.c_str());
        message = "Compress Failed";
        break;
      }

      const std::string real_path =
          Config::Instance().GetPackDir() + file_name + Config::Instance().GetPackfileSuffix();
      if (!FileUtil::WriteFile(real_path, packed_body)) {
        LOG_ERROR("异步上传失败, 压缩文件写入失败, file_name=%s, real_path=%s", file_name.c_str(),
                  real_path.c_str());
        message = "Write File Failed";
        break;
      }

      FileMeta meta{
          .is_packed_ = true,
          .file_size_ = file_body.size(),
          .modify_time_ = std::time(nullptr),
          .real_path_ = real_path,
      };

      if (!file_table_.Insert(file_name, meta)) {
        file_table_.Update(file_name, meta);
      }

      const std::string& backup_file = Config::Instance().GetBackupFile();
      if (!file_table_.Store(backup_file)) {
        LOG_ERROR("异步上传失败, 元数据持久化失败, file_name=%s, backup_file=%s", file_name.c_str(),
                  backup_file.c_str());
        message = "Store Metadata Failed";
        break;
      }

      success = true;
      message = "Upload Success";
    } while (false);

    {
      std::lock_guard<std::mutex> lock(completed_mutex_);
      // 这里直接把 request 和它最终的处理结果打包进同一个完成队列，方便主线程顺序回包。
      completed_results_.push(
          CompletedResult{.request_ = request, .success_ = success, .message_ = message});
    }

    uint64_t one = 1;
    // 往 eventfd 写 1 相当于告诉主线程：“完成队列里新到了一份结果，起来处理”。
    if (::write(notify_fd_, &one, sizeof(one)) != static_cast<ssize_t>(sizeof(one))) {
      LOG_ERROR("异步上传失败, 唤醒主线程失败, file_name=%s", file_name.c_str());
    }
  }

  /*
      DownloadFile:

      这里处理文件下载。它先从下载路径中取出文件名，再从 FileTable 查询文件元数
      据。普通文件继续走 evbuffer_add_file 零拷贝发送；压缩存储的文件会先整体读
      出并解压，再把解压后的正文写回响应缓冲区。
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

    // evbuffer_new: 创建响应体缓冲区，后面把文件描述符挂到这个 buffer 上。
    evbuffer* buffer = evbuffer_new();
    if (buffer == nullptr) {
      LOG_ERROR("下载失败, 创建响应缓冲区失败, file_name=%s", filename.c_str());
      evhttp_send_error(request, HTTP_INTERNAL, "Internal Server Error");
      return;
    }

    // evhttp_request_get_input_headers: 取出客户端这次下载请求附带的请求头容器。
    evkeyvalq* input_headers = evhttp_request_get_input_headers(request);

    // evhttp_find_header: 在请求头里查找 Range 字段，看看客户端是不是在发起断点续传请求。
    const char* range_str =
        (input_headers == nullptr) ? nullptr : evhttp_find_header(input_headers, "Range");

    size_t start = 0;
    size_t end = meta.file_size_ - 1;
    bool is_partial = false;
    if (range_str != nullptr) {
      // ParseRange 会把 "bytes=100-200" 这类字符串解析成实际字节区间。
      if (!HttpRange::ParseRange(range_str, meta.file_size_, &start, &end)) {
        LOG_ERROR("下载失败, Range 非法, file_name=%s, range=%s", filename.c_str(), range_str);
        evbuffer_free(buffer);
        evhttp_send_error(request, 416, "Requested Range Not Satisfiable");
        return;
      }

      is_partial = true;
    }

    // 这里统一算出本次真正要返回的字节数，后面普通文件和压缩文件都复用它。
    const size_t length = end - start + 1;

    if (!meta.is_packed_) {
      // open: 打开磁盘上的真实文件，拿到底层文件描述符交给 libevent 做零拷贝发送。
      UniqueFd file_fd(::open(meta.real_path_.c_str(), O_RDONLY));
      if (!file_fd.valid()) {
        LOG_ERROR("下载失败, 打开文件失败, file_name=%s, real_path=%s", filename.c_str(),
                  meta.real_path_.c_str());
        evbuffer_free(buffer);
        evhttp_send_error(request, HTTP_INTERNAL, "Internal Server Error");
        return;
      }

      // evbuffer_add_file: 把 fd 对应文件加入响应缓冲区，libevent 会接管并在发送后关闭 fd。
      // 把已经打开的文件挂到响应缓冲区上，后续发送时可以直接走零拷贝。
      if (evbuffer_add_file(buffer, file_fd.get(), static_cast<ev_off_t>(start),
                            static_cast<ev_off_t>(length)) != 0) {
        LOG_ERROR("下载失败, 挂载文件到响应缓冲区失败, file_name=%s", filename.c_str());
        evbuffer_free(buffer);
        evhttp_send_error(request, HTTP_INTERNAL, "Internal Server Error");
        return;
      }

      // evbuffer_add_file 成功后 fd 已经交给 libevent，UniqueFd 不能再负责关闭它。
      file_fd.release();
    } else {
      std::string packed_body;
      if (!FileUtil::ReadFile(meta.real_path_, &packed_body)) {
        LOG_ERROR("下载失败, 读取压缩文件失败, file_name=%s, real_path=%s", filename.c_str(),
                  meta.real_path_.c_str());
        evbuffer_free(buffer);
        evhttp_send_error(request, HTTP_INTERNAL, "Internal Server Error");
        return;
      }

      std::string file_body;
      if (!ZstdUtil::Decompress(packed_body, &file_body)) {
        LOG_ERROR("下载失败, 解压文件失败, file_name=%s, real_path=%s", filename.c_str(),
                  meta.real_path_.c_str());
        evbuffer_free(buffer);
        evhttp_send_error(request, HTTP_INTERNAL, "Internal Server Error");
        return;
      }

      // 元数据里记录的是原始大小，这里顺手校验一下，避免压缩文件损坏后把错误内容返回给客户端。
      if (file_body.size() != meta.file_size_) {
        LOG_ERROR("下载失败, 解压后大小异常, file_name=%s, expect=%zu, actual=%zu",
                  filename.c_str(), meta.file_size_, file_body.size());
        evbuffer_free(buffer);
        evhttp_send_error(request, HTTP_INTERNAL, "Internal Server Error");
        return;
      }

      // evbuffer_add 会把 [start, end] 这段内存拷贝进响应缓冲区，适合发送解压后的字符串数据。
      if (evbuffer_add(buffer, file_body.data() + start, length) != 0) {
        LOG_ERROR("下载失败, 写入解压后的响应正文失败, file_name=%s", filename.c_str());
        evbuffer_free(buffer);
        evhttp_send_error(request, HTTP_INTERNAL, "Internal Server Error");
        return;
      }
    }

    // evkeyvalq 是 libevent 用来保存 HTTP 头字段的链表容器，这里拿到的是“响应头集合”。
    evkeyvalq* output_headers = evhttp_request_get_output_headers(request);
    if (output_headers != nullptr) {
      // 告诉客户端：这次返回的是一段二进制文件数据，不要按文本或 HTML 去解释。
      evhttp_add_header(output_headers, "Content-Type", "application/octet-stream");

      // attachment 表示按“下载附件”处理；filename 用来告诉浏览器默认保存文件名。
      std::string disposition = "attachment; filename=\"" + filename + "\"";
      evhttp_add_header(output_headers, "Content-Disposition", disposition.c_str());

      if (is_partial) {
        // Content-Range 用来明确告诉客户端：这次返回的是整个文件中的哪一段。
        std::string content_range = "bytes " + std::to_string(start) + "-" + std::to_string(end) +
                                    "/" + std::to_string(meta.file_size_);
        evhttp_add_header(output_headers, "Content-Range", content_range.c_str());
      }
    }

    LOG_INFO("下载成功, file_name=%s, start=%zu, end=%zu, partial=%d, packed=%d, real_path=%s",
             filename.c_str(), start, end, static_cast<int>(is_partial),
             static_cast<int>(meta.is_packed_), meta.real_path_.c_str());

    if (is_partial) {
      // 206 表示本次返回的是文件的部分内容，浏览器和下载器会据此继续做断点续传。
      evhttp_send_reply(request, 206, "Partial Content", buffer);
    } else {
      evhttp_send_reply(request, HTTP_OK, "OK", buffer);
    }
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

      这里校验客户端传入的存储类型。当前只接受 deep 和 low 两种取值：low 代表普
      通存储，deep 代表压缩后再落盘。
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
    if (notify_event_ != nullptr) {
      // event_free: 释放挂在 base_ 上的 eventfd 监听事件。
      event_free(notify_event_);
      notify_event_ = nullptr;
    }

    if (notify_fd_ >= 0) {
      // close: 关闭跨线程通知用的 eventfd。
      ::close(notify_fd_);
      notify_fd_ = -1;
    }

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

  // notify_fd_ 是跨线程通知通道，后台线程写它来唤醒 libevent 主线程。
  int notify_fd_ = -1;

  // notify_event_ 是 notify_fd_ 对应的 libevent 监听事件。
  event* notify_event_ = nullptr;

  // 保存监听地址和端口，便于启动日志和后续诊断。
  std::string address_ = "0.0.0.0";
  uint16_t port_ = 8080;

  // file_table_ 保存当前服务已经接收的文件元数据。
  FileTable file_table_;

  // thread_pool_ 负责执行 deep 上传里的压缩、写盘和持久化这类后台任务。
  ThreadPool thread_pool_;

  /*
      CompletedResult:

      这是异步上传任务回到主线程时携带的结果单元。后台线程完成压缩和持久化后，会把
      “该给哪个 request 回包、回成功还是失败、附带什么提示信息” 一次性打包进这个
      结构，再塞进完成队列交给主线程处理。
  */
  struct CompletedResult {
    evhttp_request* request_ = nullptr;  // 指向这次异步上传对应的 HTTP 请求，主线程后面靠它回包。
    bool success_ = false;               // 标记后台任务最终是成功还是失败。
    std::string message_;                // 保存要返回给客户端的提示信息或错误原因。
  };

  // completed_results_ 保存已经完成后台处理的异步请求结果，主线程被唤醒后按队列顺序回包。
  std::queue<CompletedResult> completed_results_;

  // completed_mutex_ 保护异步完成结果队列，避免主线程和后台线程并发访问冲突。
  std::mutex completed_mutex_;
};
