#pragma once

#include "LogFlush.hpp"
#include "Message.hpp"

// <cstdarg> 提供 va_list、va_start、va_end，用来处理可变参数。
#include <cstdarg>
// <cstdio> 提供 vsnprintf，用来把 printf 风格参数格式化成字符串。
#include <cstdio>
// <ctime> 提供 time，用来获取当前时间戳。
#include <ctime>
#include <memory>
#include <string>
// <thread> 提供 std::this_thread::get_id，用来获取当前线程 ID。
#include <thread>
#include <vector>

/*
    Logger 所在层级：日志系统的“对外日志器层”。

    Logger 表示同步日志器。

    同步的含义是：
    - 业务线程调用 Info / Error 这类接口后
    - 日志会在这次调用里直接交给输出器处理
    - 不经过后台线程
    - 不经过双缓冲

    适用场景：
    - 先把日志链路跑通
    - 先验证日志格式和输出器行为
*/
class Logger {
public:
  // logger_name 用来标识当前日志器。
  // outputs 表示这条日志最终要交给哪些输出器。
  Logger(const std::string &logger_name,
         const std::vector<std::shared_ptr<LogFlush>> &outputs)
      : logger_name_(logger_name), outputs_(outputs) {}

  /*
      Debug:

      供谁调用：
      - 业务代码调用 Logger::Debug

      参数：
      - file: 源文件名（一般传 __FILE__）
      - line: 源码行号（一般传 __LINE__）
      - fmt:  printf 风格格式串
      - ...:  对应 fmt 的可变参数

      过程：
      1. 把 fmt + 可变参数整理成正文字符串
      2. 交给 serialize 组装完整日志并同步分发给输出器列表

      返回：
      - 无返回值，日志会被发送到 outputs_ 中的各个输出器
  */
  void Debug(const char *file, size_t line, const char *fmt, ...) {
    va_list args; // args是接管 ... 那部分参数的读取器
    // va_list 可变参数:一个函数可以接收“数量不固定”的额外参数
    va_start(args,
             fmt); // 从最后一个固定参数 fmt 后面开始，准备读取那些额外参数。
    std::string payload = formatPayload(
        fmt, args); // 把模板 fmt 和参数 args 合起来，生成真正的日志正文。
    va_end(args); // args 这次可变参数读取用完了，现在结束它。
    serialize(
        LogLevel::DEBUG, file, line,
        payload); // 正文已经准备好了，现在按 DEBUG 级别，把它组装成完整日志。
  }

  void Info(const char *file, size_t line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::string payload = formatPayload(fmt, args);
    va_end(args);
    serialize(LogLevel::INFO, file, line, payload);
  }

  void Warn(const char *file, size_t line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::string payload = formatPayload(fmt, args);
    va_end(args);
    serialize(LogLevel::WARN, file, line, payload);
  }

  void Error(const char *file, size_t line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::string payload = formatPayload(fmt, args);
    va_end(args);
    serialize(LogLevel::ERROR, file, line, payload);
  }

  void Fatal(const char *file, size_t line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::string payload = formatPayload(fmt, args);
    va_end(args);
    serialize(LogLevel::FATAL, file, line, payload);
  }

private:
  /*
      formatPayload:

      供谁调用：
      - Debug / Info / Warn / Error / Fatal

      参数：
      - fmt:  printf 风格格式串
      - args: 可变参数列表

      行为：
      - 第一次 vsnprintf 只计算正文长度
      - 第二次 vsnprintf 写入最终正文

      返回：
      - 拼接完成的正文字符串
  */
  std::string formatPayload(const char *fmt, va_list args) const {
    // args_copy 用来做第一次长度计算。
    // 因为 va_list 被消费后不能直接重复使用，所以这里先复制一份。
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
      serialize:

      供谁调用：
      - Debug / Info / Warn / Error / Fatal

      参数：
      - level:  日志级别
      - file:   源文件名
      - line:   源码行号
      - payload: 已经格式化好的正文

      行为：
      1. 组装 LogMessage（补齐时间、线程、文件、行号、级别、正文）
      2. 调用 LogMessage::format() 生成最终字符串
      3. 遍历 outputs_，把这条日志直接交给每个输出器
  */
  void serialize(LogLevel::value level, const std::string &file, size_t line,
                 const std::string &payload) {
    LogMessage msg;
    msg.ctime_ = std::time(nullptr);
    msg.tid_ = std::this_thread::get_id();
    msg.file_name_ = file;
    msg.line_ = line;
    msg.level_ = level;
    msg.payload_ = payload;

    std::string text = msg.format();

    /*
    用output的引用来遍历整个输出器列表outputs_
   ↓
    如果这个输出器是空的，就跳过
   ↓
    如果不是空的，就调用它的 Flush
   ↓
    把 text 这条日志交给它输出
    */
    for (const auto &output : outputs_) {
      if (output ==
          nullptr) { // 如果这个智能指针当前没有指向任何输出器对象，就跳过这一轮
        continue;
      }
      output->Flush(text.c_str(),
                    text.size()); // 调用命中的这个输出器，让它输出
    }
  }

private:
  // logger_name_ 表示当前日志器的名字。
  // 第一阶段里它先保存下来，后面可以继续接入最终日志格式。
  std::string logger_name_;

  // outputs_ 保存输出器列表。
  // 这样同一条日志可以同时输出到多个地方。
  std::vector<std::shared_ptr<LogFlush>> outputs_;
};
