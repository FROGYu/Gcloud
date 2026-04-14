#pragma once

#include "Logger/LogMacros.hpp"

#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>

#include <chrono>
#include <cstdint>
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

    base_ = event_base_new();
    if (base_ == nullptr) {
      LOG_ERROR("创建 event_base 失败");
      return false;
    }

    http_ = evhttp_new(base_);
    if (http_ == nullptr) {
      LOG_ERROR("创建 evhttp 服务失败");
      Cleanup();
      return false;
    }

    // 注册统一请求入口，并把当前对象地址作为上下文一并交给 libevent 保存。
    evhttp_set_gencb(http_, &Service::HttpRequestCb, this);

    if (evhttp_bind_socket(http_, address_.c_str(), static_cast<uint16_t>(port_)) != 0) {
      LOG_ERROR("绑定 HTTP 监听失败, address=%s, port=%u", address_.c_str(),
                static_cast<unsigned>(port_));
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

      这是 libevent 的统一请求入口。收到 HTTP 请求后，先从 arg 中取回当前的
      Service 对象，再转交给 HandleRequest 处理具体业务。
  */
  static void HttpRequestCb(evhttp_request* request, void* arg) {
    // 这里把回调上下文参数还原成当前 Service 对象，后续才能继续调用成员函数。
    Service* service = static_cast<Service*>(arg);
    if (service == nullptr) {
      if (request != nullptr) {
        evhttp_send_error(request, HTTP_INTERNAL, "Internal Server Error");
      }
      return;
    }

    service->HandleRequest(request);
  }

  /*
      HandleRequest:

      这里执行真正的请求处理逻辑。它负责读取请求的 URI，构造响应缓冲区，设置响应
      头并写入响应正文，最后把结果发回客户端，同时记录本次请求的耗时。
  */
  void HandleRequest(evhttp_request* request) {
    const auto start_time = std::chrono::steady_clock::now();

    const char* uri = evhttp_request_get_uri(request);
    const char* safe_uri = (uri == nullptr) ? "/" : uri;
    LOG_INFO("收到 HTTP 请求, uri=%s", safe_uri);

    // 这里创建一块响应缓冲区，后面要回给客户端的正文，先写进这里
    //HTTP 响应整体是：状态行 + 响应头 + 响应体
    //状态行：后面 evhttp_send_reply 里给 响应头：后面 evhttp_add_header 响应体：写在 buffer 里
    evbuffer* buffer = evbuffer_new();
    if (buffer == nullptr) {
      LOG_ERROR("创建响应缓冲区失败, uri=%s", safe_uri);
      evhttp_send_error(request, HTTP_INTERNAL, "Internal Server Error");
      return;
    }
    //从 request 里拿到“响应头容器” 后面往这个容器里加 header，最终会一起发给客户端

    evkeyvalq* output_headers = evhttp_request_get_output_headers(request);
    if (output_headers != nullptr) {
      evhttp_add_header(output_headers, "Content-Type", "text/plain; charset=utf-8");
      // 先返回纯文本，后续切到 HTML 页面时只需要替换这里的 Content-Type。
      // 客户端收到响应后，要根据 Content-Type 决定怎么解释 body
    }

    if (evbuffer_add_printf(buffer, "Hello, Cloud Storage!") != 0) {
      LOG_ERROR("写入响应缓冲区失败, uri=%s", safe_uri);
      evbuffer_free(buffer);
      evhttp_send_error(request, HTTP_INTERNAL, "Internal Server Error");
      return;
    }

    // 这里把状态码、状态文本和缓冲区正文一起发回浏览器。
    evhttp_send_reply(request, HTTP_OK, "OK", buffer);
    evbuffer_free(buffer);
    /*
        HTTP/1.1 200 OK
        Content-Type: text/plain; charset=utf-8

        Hello, Cloud Storage!*/

    const auto end_time = std::chrono::steady_clock::now();
    const auto cost_us =
        std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
    LOG_INFO("完成 HTTP 请求, uri=%s, cost_us=%lld", safe_uri, static_cast<long long>(cost_us));
  }

  /*
      Cleanup:

      这里释放当前 Service 持有的 libevent 资源。它会销毁 HTTP 服务对象和事件底
      座，避免资源泄漏，也让 Init 在重复调用时能够从干净状态重新开始。
  */
  void Cleanup() {
    if (http_ != nullptr) {
      evhttp_free(http_);
      http_ = nullptr;
    }

    if (base_ != nullptr) {
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
};
