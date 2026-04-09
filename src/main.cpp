#include "Logger/LogMacros.hpp"

#include <memory>
#include <thread>
#include <vector>

namespace {
constexpr const char *kRotateBaseFilePath = "./rotate_test.log";
constexpr const char *kDefaultLoggerName = "default";
constexpr size_t kMaxFileSize = 2048;
}

int main() {
  /*
      这次 main 的验证目标：
      - 验证 SizeRotateFileFlush 是否会在文件即将超过上限前切到新文件
      - 验证 LoggerManager 和 LOG_INFO 宏在这个场景下仍然能正常工作

      只保留一个日志器：
      - 先把“按大小切文件”这件事单独验证清楚
      - 不把多个日志器共享同一个输出器的并发问题混进来
  */

  // 这里使用一个很小的文件大小上限，目的是让测试运行一次就能看到切文件效果。
  std::shared_ptr<LogFlush> rotate_file_flush =
      std::make_shared<SizeRotateFileFlush>(kRotateBaseFilePath, kMaxFileSize);
  std::vector<std::shared_ptr<LogFlush>> outputs;
  outputs.push_back(rotate_file_flush);

  // 这次只注册一个默认日志器，业务线程统一通过 LOG_INFO 写日志。
  auto default_logger = std::make_shared<AsyncLogger>(kDefaultLoggerName, outputs);

  LoggerManager::Instance().RegisterLogger(kDefaultLoggerName, default_logger);

  // 启动多个业务线程，持续写日志。
  // 如果 SizeRotateFileFlush 正常工作，运行结束后应该能看到多个 rotate_test_*.log 文件。
  const int thread_count = 4;
  const int log_count_per_thread = 400;
  std::vector<std::thread> workers;

  for (int i = 0; i < thread_count; ++i) {
    workers.emplace_back([i, log_count_per_thread]() {
      for (int j = 0; j < log_count_per_thread; ++j) {
        LOG_INFO("rotate test: thread=%d, log_index=%d", i, j);
      }
    });
  }

  for (auto &worker : workers) {
    worker.join();
  }

  return 0;
}
