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
        format 的作用是把当前这条日志对象里的数据整理成一行字符串。

        这几个成员不会自动变成可读文本：
        - ctime_ 保存的是时间戳
        - level_ 保存的是枚举值
        - line_ 保存的是行号数字

        这个函数会做四件事：
        1. 把 ctime_ 转成当前时区下的本地时间
        2. 从本地时间里取出时、分、秒
        3. 把 level_ 转成 "INFO" 这类字符串
        4. 按固定格式拼接成一整条日志

        最终格式：
        [14:23:05][14012345][INFO][main.cpp:25] 用户登录成功
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
