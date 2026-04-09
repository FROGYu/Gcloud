#pragma once

#include "LoggerManager.hpp"

/*
    LogMacros.hpp 所在层级：日志系统的“业务接入层”。

    需要它的原因：
    - 如果每次写日志都手动传 __FILE__ 和 __LINE__，代码会很长
    - 如果每次都手动写 LoggerManager::Instance().GetLogger(...)，也会重复
    - 宏把这些固定动作包起来，业务代码只保留“要写什么日志”

    这一层负责什么：
    - 自动补上 __FILE__ 和 __LINE__
    - 自动从 LoggerManager 中取日志器
    - 如果日志器不存在，直接跳过这次日志调用

    这一层不负责什么：
    - 不负责创建日志器
    - 不负责格式化日志
    - 不负责输出日志
*/

// 默认日志器名字固定为 "default"。
// 不指定日志器时，LOG_INFO / LOG_ERROR 这一组宏都会先去拿它。
#define MYLOG_DEFAULT_LOGGER_NAME "default"

/*
    LOG_xxx:
    - 使用默认日志器
    - 自动补上 __FILE__ 和 __LINE__

    LOG_xxx_TO:
    - 使用指定名字的日志器
    - 适合一个项目里有多个日志器的场景
*/

#define LOG_DEBUG(fmt, ...)                                                  \
  do {                                                                       \
    auto mylog_logger__ =                                                    \
        LoggerManager::Instance().GetLogger(MYLOG_DEFAULT_LOGGER_NAME);      \
    if (mylog_logger__) {                                                    \
      mylog_logger__->Debug(__FILE__, __LINE__, fmt, ##__VA_ARGS__);         \
    }                                                                        \
  } while (0)

#define LOG_INFO(fmt, ...)                                                   \
  do {                                                                       \
    auto mylog_logger__ =                                                    \
        LoggerManager::Instance().GetLogger(MYLOG_DEFAULT_LOGGER_NAME);      \
    if (mylog_logger__) {                                                    \
      mylog_logger__->Info(__FILE__, __LINE__, fmt, ##__VA_ARGS__);          \
    }                                                                        \
  } while (0)

#define LOG_WARN(fmt, ...)                                                   \
  do {                                                                       \
    auto mylog_logger__ =                                                    \
        LoggerManager::Instance().GetLogger(MYLOG_DEFAULT_LOGGER_NAME);      \
    if (mylog_logger__) {                                                    \
      mylog_logger__->Warn(__FILE__, __LINE__, fmt, ##__VA_ARGS__);          \
    }                                                                        \
  } while (0)

#define LOG_ERROR(fmt, ...)                                                  \
  do {                                                                       \
    auto mylog_logger__ =                                                    \
        LoggerManager::Instance().GetLogger(MYLOG_DEFAULT_LOGGER_NAME);      \
    if (mylog_logger__) {                                                    \
      mylog_logger__->Error(__FILE__, __LINE__, fmt, ##__VA_ARGS__);         \
    }                                                                        \
  } while (0)

#define LOG_FATAL(fmt, ...)                                                  \
  do {                                                                       \
    auto mylog_logger__ =                                                    \
        LoggerManager::Instance().GetLogger(MYLOG_DEFAULT_LOGGER_NAME);      \
    if (mylog_logger__) {                                                    \
      mylog_logger__->Fatal(__FILE__, __LINE__, fmt, ##__VA_ARGS__);         \
    }                                                                        \
  } while (0)

#define LOG_DEBUG_TO(logger_name, fmt, ...)                                  \
  do {                                                                       \
    auto mylog_logger__ = LoggerManager::Instance().GetLogger(logger_name);  \
    if (mylog_logger__) {                                                    \
      mylog_logger__->Debug(__FILE__, __LINE__, fmt, ##__VA_ARGS__);         \
    }                                                                        \
  } while (0)

#define LOG_INFO_TO(logger_name, fmt, ...)                                   \
  do {                                                                       \
    auto mylog_logger__ = LoggerManager::Instance().GetLogger(logger_name);  \
    if (mylog_logger__) {                                                    \
      mylog_logger__->Info(__FILE__, __LINE__, fmt, ##__VA_ARGS__);          \
    }                                                                        \
  } while (0)

#define LOG_WARN_TO(logger_name, fmt, ...)                                   \
  do {                                                                       \
    auto mylog_logger__ = LoggerManager::Instance().GetLogger(logger_name);  \
    if (mylog_logger__) {                                                    \
      mylog_logger__->Warn(__FILE__, __LINE__, fmt, ##__VA_ARGS__);          \
    }                                                                        \
  } while (0)

#define LOG_ERROR_TO(logger_name, fmt, ...)                                  \
  do {                                                                       \
    auto mylog_logger__ = LoggerManager::Instance().GetLogger(logger_name);  \
    if (mylog_logger__) {                                                    \
      mylog_logger__->Error(__FILE__, __LINE__, fmt, ##__VA_ARGS__);         \
    }                                                                        \
  } while (0)

#define LOG_FATAL_TO(logger_name, fmt, ...)                                  \
  do {                                                                       \
    auto mylog_logger__ = LoggerManager::Instance().GetLogger(logger_name);  \
    if (mylog_logger__) {                                                    \
      mylog_logger__->Fatal(__FILE__, __LINE__, fmt, ##__VA_ARGS__);         \
    }                                                                        \
  } while (0)
