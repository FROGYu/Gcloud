#include "Logger/LogMacros.hpp"

#include <memory>
#include <thread>
#include <vector>

namespace {
constexpr const char *kLogFilePath = "./test.log";
constexpr const char *kDefaultLoggerName = "default";
constexpr const char *kNetworkLoggerName = "network_log";
}

int main() {
  // 先准备输出器列表。
  // 之后创建日志器时，会把这组输出器一起交进去。
  std::shared_ptr<LogFlush> stdout_flush = std::make_shared<StdoutFlush>();
  std::shared_ptr<LogFlush> file_flush = std::make_shared<FileFlush>(kLogFilePath);
  std::vector<std::shared_ptr<LogFlush>> outputs;
  outputs.push_back(stdout_flush);
  outputs.push_back(file_flush);

  // 这里创建两个异步日志器：
  // - default：给默认日志宏使用
  // - network_log：给指定名字的日志宏使用
  auto default_logger = std::make_shared<AsyncLogger>(kDefaultLoggerName, outputs);
  auto network_logger = std::make_shared<AsyncLogger>(kNetworkLoggerName, outputs);

  // 把日志器注册到全局管理器。
  // 后面业务代码就不再直接持有日志器对象，而是通过宏间接获取。
  LoggerManager::Instance().RegisterLogger(kDefaultLoggerName, default_logger);
  LoggerManager::Instance().RegisterLogger(kNetworkLoggerName, network_logger);

  // 启动多个业务线程，验证两种宏都能正常工作。
  const int thread_count = 4;
  const int log_count_per_thread = 2000;
  std::vector<std::thread> workers;

  for (int i = 0; i < thread_count; ++i) {
    workers.emplace_back([i, log_count_per_thread]() {
      for (int j = 0; j < log_count_per_thread; ++j) {
        LOG_INFO("default logger: thread=%d, log_index=%d", i, j);
        LOG_INFO_TO(kNetworkLoggerName, "network logger: thread=%d, log_index=%d",
                    i, j);
      }
    });
  }

  for (auto &worker : workers) {
    worker.join();
  }

  return 0;
}
