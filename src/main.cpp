#include "Logger/LogMacros.hpp"
#include "Util/LoggerInit.hpp"

#include <thread>
#include <vector>

namespace {
constexpr int kThreadCount = 4;
constexpr int kLogCountPerThread = 400;

const RotateLoggerConfig kDefaultRotateLoggerConfig = {
    .logger_name = "default",
    .base_file_name = "rotate_test.log",
    .max_file_size = 64 * 1024,
};

void runRotateFlushTest() {
  // 这里启动多个业务线程，持续写日志。
  // 如果 SizeRotateFileFlush 正常工作，
  // 运行结束后应该能在 src/Logger/logs 里看到多个 rotate_test_*.log 文件。
  std::vector<std::thread> workers;

  for (int i = 0; i < kThreadCount; ++i) {
    workers.emplace_back([i]() {
      for (int j = 0; j < kLogCountPerThread; ++j) {
        LOG_INFO("rotate test: thread=%d, log_index=%d", i, j);
      }
    });
  }

  for (auto &worker : workers) {
    worker.join();
  }
}
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

  // 这里完成日志系统初始化。
  // 业务代码不再自己手动创建输出器、日志器和注册流程。
  InitDefaultRotateLogger(__FILE__, kDefaultRotateLoggerConfig);
  runRotateFlushTest();

  return 0;
}
