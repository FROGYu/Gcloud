#pragma once

#include "AsyncLogger.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

/*
    LoggerManager 所在层级：日志系统的“全局管理层”。

    需要它的原因：
    - 业务代码不会在每次写日志时都重新创建一个 AsyncLogger
    - 一个项目里往往会有多个日志器，例如：
      - system_log
      - network_log
      - storage_log
    - 这些日志器需要有一个统一的登记和获取入口

    它负责什么：
    - 保存已经注册好的日志器
    - 按名字查找日志器
    - 提供全局唯一的管理入口
*/
class LoggerManager {
public:
  /*
      Instance:

      供谁调用：
      - 任何需要注册或获取日志器的代码

      返回：
      - 全局唯一的 LoggerManager 对象，只提供一个全局入口
      这里是单例模式，但外加一个注册表（管理者）的角色

      行为：
      - 第一次调用时创建对象
      - 之后都返回同一个对象
  */
  static LoggerManager &Instance() {
    static LoggerManager instance;
    return instance; // 唯一实例
  }

  /*
      RegisterLogger:

      供谁调用：
      - 初始化日志系统的代码

      参数：
      - name: 日志器名字
      - logger: 要注册的日志器对象

      行为：
      - 把 logger 保存到 loggers_ 中
      - 如果同名日志器已存在，会被新的 logger 覆盖
  */
  void RegisterLogger(const std::string &name,
                      const std::shared_ptr<AsyncLogger> &logger) {
    std::lock_guard<std::mutex> lock(mutex_);
    loggers_[name] = logger;
  }

  /*
      GetLogger:

      供谁调用：
      - 业务代码
      - 需要按名字取日志器的初始化代码

      参数：
      - name: 要获取的日志器名字

      返回：
      - 找到时返回对应的日志器
      - 找不到时返回 nullptr
  */
  std::shared_ptr<AsyncLogger> GetLogger(const std::string &name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = loggers_.find(name);
    if (it == loggers_.end()) {
      return nullptr;
    }
    return it->second;
  }

  /*
      HasLogger:

      供谁调用：
      - 检查某个名字的日志器是否已经注册的代码

      参数：
      - name: 要检查的日志器名字

      返回：
      - true:  已经注册过
      - false: 还没有注册
  */
  bool HasLogger(const std::string &name) {
    std::lock_guard<std::mutex> lock(mutex_);
    return loggers_.find(name) != loggers_.end();
  }

private:
  // 单例模式，构造函数放到了private
  LoggerManager() = default;
  ~LoggerManager() = default;

  // 禁用拷贝函数，不能复制出第二份
  LoggerManager(const LoggerManager &) = delete;
  LoggerManager &operator=(const LoggerManager &) = delete;

private:
  // loggers_ 保存“日志器名字 -> 日志器对象”的对应关系。
  // 这个唯一的管理器，要负责保存很多个日志器。
  std::unordered_map<std::string, std::shared_ptr<AsyncLogger>> loggers_;

  // mutex_ 保护 loggers_，避免多个线程同时注册或获取时出现竞态。
  std::mutex mutex_;
};
