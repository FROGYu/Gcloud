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
    Logger 是第一阶段直接给业务代码使用的类。

    业务代码调用 Logger::Info、Logger::Error 这类接口时，
    不需要自己手动创建 LogMessage，也不需要自己决定写到终端还是文件。

    Logger 在中间做两件事：
    1. 把变参整理成日志正文
    2. 组装完整日志，并交给各个输出器
*/
class Logger {
public:
  // logger_name 用来标识当前日志器。
  // outputs 表示这条日志最终要交给哪些输出器。
  Logger(const std::string &logger_name,
         const std::vector<std::shared_ptr<LogFlush>> &outputs)
      : logger_name_(logger_name), outputs_(outputs) {}

  // 定义了5个接口的调用方式
  // 进入函数后会先把变参整理成正文，再交给 serialize。
  // 最后这个 ... 就表示：从 fmt 后面开始，还可以继续传很多参数，传几个都行。
  /*
  1. 把 ... 那些参数接过来
  2. 按 fmt 拼成正文字符串
  3. 带着 INFO 级别、文件名、行号、正文，去调用 serialize
  4. serialize 再补齐时间和线程
  5. 生成最终日志文本
  6. 发给 outputs_ 里的所有输出器*/

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
      formatPayload 负责把 printf 风格参数变成 std::string。

      这里分两次调用 vsnprintf：
      1. 第一次只计算正文长度
      2. 第二次把内容真正写入字符串

      这样做的原因是正文长度在格式化之前并不知道。
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
      serialize 负责把零散信息组装成一条完整日志。

      这一步会补齐四类信息：
      - 当前时间
      - 当前线程 ID
      - 文件名和行号
      - 日志级别和正文

      然后调用 LogMessage::format() 生成最终字符串，
      再把这条字符串交给 outputs_ 里的每个输出器。
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
