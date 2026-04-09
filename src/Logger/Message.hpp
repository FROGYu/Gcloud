#pragma once

#include "Level.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>

// LogMessage 表示一条完整日志。
// 它负责收拢日志上下文，并把这些信息格式化成统一字符串。
struct LogMessage {
  // 日志产生时间。
  time_t ctime_ = 0;
  // 产生日志的线程 ID。
  std::thread::id tid_{};
  // 源文件名。
  std::string file_name_;
  // 源码行号。
  size_t line_ = 0;
  // 日志级别。
  LogLevel::value level_ = LogLevel::INFO;
  // 日志正文。
  std::string payload_;

    /*
        format:

        供谁调用：
        - Logger::serialize

        作用：
        - 把当前这条日志对象整理成一行字符串

        关键点：
        - ctime_ 是时间戳，需要转成本地时间
        - level_ 是枚举，需要转成字符串
        - line_ 是数字，直接拼接

        最终格式：
        [14:23:05][14012345][INFO][main.cpp:25] 用户登录成功

        返回：
        - 拼接完成的日志字符串
    */
    std::string format() const {
        // 根据时间戳生成本地时间对象。
        auto time_point = std::chrono::system_clock::from_time_t(ctime_);
        std::time_t current_time = std::chrono::system_clock::to_time_t(time_point);
    std::tm *tm_time = std::localtime(&current_time);

    // 按固定顺序拼接日志内容。
    std::ostringstream oss;
    oss << '[' << std::put_time(tm_time, "%H:%M:%S") << ']' << '[' << tid_
        << ']' << '[' << LogLevel::ToString(level_) << ']' << '[' << file_name_
        << ':' << line_ << "] " << payload_ << '\n';
    return oss.str();
  }
};
