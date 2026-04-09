#pragma once

#include "Logger/AsyncLogger.hpp"
#include "Logger/LoggerManager.hpp"
#include "Logger/LogFlush.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

/*
    LoggerInit.hpp 所在层级：通用初始化工具层。

    需要它的原因：
    - 业务代码不应该反复写“创建输出器 -> 创建日志器 -> 注册日志器”这套流程
    - 这些步骤固定且重复，适合收进一个初始化工具函数

    这一层负责什么：
    - 组织日志系统启动时需要的固定装配动作
    - 把日志器注册到 LoggerManager

    这一层不负责什么：
    - 不负责业务线程写日志
    - 不负责日志格式化
    - 不负责输出器内部的写文件逻辑
*/

/*
    RotateLoggerConfig:

    作用：
    - 把“滚动日志器初始化时需要的一组参数”收在一起
    - 避免初始化函数参数越写越多

    字段说明：
    - logger_name: 要注册到 LoggerManager 中的日志器名字
    - base_file_name: 日志文件的基础文件名
    - max_file_size: 单个日志文件允许写入的最大字节数
*/
struct RotateLoggerConfig {
  std::string logger_name;
  std::string base_file_name;
  size_t max_file_size = 0;
};

/*
    BuildLoggerLogDir:

    参数：
    - source_file: 当前源码文件路径，一般直接传 __FILE__

    返回：
    - src/Logger/logs 这个目录路径

    作用：
    - 根据当前源码文件的位置，反推出固定的日志目录
    - 避免日志路径跟着程序启动目录变化
*/
inline std::filesystem::path BuildLoggerLogDir(const char *source_file) {
  std::filesystem::path source_dir = std::filesystem::path(source_file).parent_path();
  return source_dir / "Logger" / "logs";
}

/*
    InitDefaultRotateLogger:

    参数：
    - source_file: 当前源码文件路径，一般直接传 __FILE__
    - config: 初始化滚动日志器所需的配置

    作用：
    - 计算固定日志目录
    - 创建 SizeRotateFileFlush
    - 创建 AsyncLogger
    - 把日志器注册到 LoggerManager

    返回：
    - 注册进去的默认日志器对象
*/
inline std::shared_ptr<AsyncLogger>
InitDefaultRotateLogger(const char *source_file,
                        const RotateLoggerConfig &config) {
  std::filesystem::path log_dir = BuildLoggerLogDir(source_file);
  std::filesystem::path log_file_path = log_dir / config.base_file_name;

  std::vector<std::shared_ptr<LogFlush>> outputs;
  outputs.push_back(std::make_shared<SizeRotateFileFlush>(
      log_file_path.string(), config.max_file_size));

  auto logger = std::make_shared<AsyncLogger>(config.logger_name, outputs);
  LoggerManager::Instance().RegisterLogger(config.logger_name, logger);
  return logger;
}
