#pragma once

#include "AsyncWorker.hpp"
#include "LogFlush.hpp"
#include "Message.hpp"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <memory>
#include <string>
#include <thread>
#include <vector>

/*
    AsyncLogger 所在层级：日志系统的“对外日志器层”。

    设计思路：
    1. 对业务代码保留和 Logger 类似的调用方式：
       - Info / Debug / Warn / Error / Fatal
    2. 业务线程调用这些接口时，不直接把日志写到终端或文件。
       它只负责：
       - 生成正文
       - 组装 LogMessage
       - 把最终字符串送进 AsyncWorker
    3. 真正的输出工作交给 AsyncWorker 的后台线程完成。

    供谁使用：
    - 业务代码：调用 Info / Debug / Warn / Error / Fatal
*/
class AsyncLogger {
public:
  /*
      构造函数

      参数：
      - logger_name: 当前日志器名称
      - outputs:     输出器列表

      行为：
      - 保存日志器名称和输出器列表
      - 创建 AsyncWorker
      - 把“如何输出一批日志”的逻辑绑定给 AsyncWorker
  */

  AsyncLogger(const std::string &logger_name,
              const std::vector<std::shared_ptr<LogFlush>> &outputs)
      : logger_name_(logger_name), outputs_(outputs),
        worker_([this](const char *data, size_t len) {
          this->consume(data, len);
        }) {}

  //[this](const char *data, size_t len) { ... }：这是一个 lambda
  // 把 “收到数据后调用 consume” 这件事注册给 AsyncWorker
  /*
      Debug:
      - 参数与 Logger::Debug 相同
      - 作用是把日志送进 AsyncWorker，而不是直接输出
  */
  void Debug(const char *file, size_t line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::string payload = formatPayload(fmt, args);
    va_end(args);
    publishLog(LogLevel::DEBUG, file, line, payload);
  }

  /*
      Info:
      - 参数与 Debug 相同
      - 日志级别为 INFO
  */
  void Info(const char *file, size_t line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::string payload = formatPayload(fmt, args);
    va_end(args);
    publishLog(LogLevel::INFO, file, line, payload);
  }

  /*
      Warn:
      - 参数与 Debug 相同
      - 日志级别为 WARN
  */
  void Warn(const char *file, size_t line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::string payload = formatPayload(fmt, args);
    va_end(args);
    publishLog(LogLevel::WARN, file, line, payload);
  }

  /*
      Error:
      - 参数与 Debug 相同
      - 日志级别为 ERROR
  */
  void Error(const char *file, size_t line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::string payload = formatPayload(fmt, args);
    va_end(args);
    publishLog(LogLevel::ERROR, file, line, payload);
  }

  /*
      Fatal:
      - 参数与 Debug 相同
      - 日志级别为 FATAL
  */
  void Fatal(const char *file, size_t line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::string payload = formatPayload(fmt, args);
    va_end(args);
    publishLog(LogLevel::FATAL, file, line, payload);
  }

private:
  /*
      formatPayload:

      供谁调用：
      - Debug / Info / Warn / Error / Fatal

      返回：
      - 格式化后的正文字符串
  */
  std::string formatPayload(const char *fmt, va_list args) const {
    va_list args_copy;
    va_copy(args_copy, args);
    int len = std::vsnprintf(nullptr, 0, fmt, args_copy);
    va_end(args_copy);

    if (len <= 0) {
      return "";
    }

    std::string payload(static_cast<size_t>(len), '\0');
    std::vsnprintf(payload.data(), payload.size() + 1, fmt, args);
    return payload;
  }

  /*
      publishLog:

      供谁调用：
      - Debug / Info / Warn / Error / Fatal

      行为：
      1. 组装 LogMessage
      2. 生成最终字符串
      3. 调用 AsyncWorker::Push，把日志送进内存缓冲区
      异步日志器是先交给 AsyncWorker

  */
  void publishLog(LogLevel::value level, const std::string &file, size_t line,
                  const std::string &payload) {
    LogMessage msg;
    msg.ctime_ = std::time(nullptr);
    msg.tid_ = std::this_thread::get_id();
    msg.file_name_ = file;
    msg.line_ = line;
    msg.level_ = level;
    msg.payload_ = payload;

    std::string text = msg.format();
    // 格式化消息之后把数据交给worker去做，而不是自己flush,实现了行为分离，完成了异步
    worker_.Push(text.c_str(), text.size());
  }

  /*
      consume:

      供谁调用：
      - AsyncWorker 的后台线程

      参数：
      - data: 一批已经格式化完成的日志内容
      - len:  这批日志内容的字节数

      行为：
      - 遍历输出器列表
      - 把这批日志交给每个输出器
  */
  void consume(const char *data, size_t len) {
    for (const auto &output : outputs_) {
      if (output == nullptr) {
        continue;
      }
      output->Flush(data, len);
    }
  }

private:
  // logger_name_ 表示当前日志器的名字。
  std::string logger_name_;
  // outputs_ 保存输出器列表。
  std::vector<std::shared_ptr<LogFlush>> outputs_;
  // worker_ 负责后台处理日志。
  AsyncWorker worker_;
};
