
#include "Logger/AsyncLogger.hpp"

#include <memory>
#include <thread>
#include <vector>

namespace {
constexpr const char *kLogFilePath = "./test.log";
}

int main() {
  // 创建两个输出器：
  // 一个负责把日志打印到终端，一个负责把日志写入文件。
  std::shared_ptr<LogFlush> stdout_flush = {std::make_shared<
      StdoutFlush>()}; // 都用父类输出器来实例化,底层真实对象：StdoutFlush,对外统一身份：LogFlush
  std::shared_ptr<LogFlush> file_flush =
      std::make_shared<FileFlush>(kLogFilePath);

  // outputs 用来保存当前日志器的输出器列表。
  std::vector<std::shared_ptr<LogFlush>> outputs;
  outputs.push_back(stdout_flush);
  outputs.push_back(file_flush);

  // 创建异步日志器，并把输出器列表交给它。
  // 创建 logger 的同时，把后台 worker 也一起启动了
  /*
      1. logger 被创建
      2. logger 保存 outputs
      3. logger 内部把“怎么消费日志”注册给 worker
      4. logger 内部把 worker 也创建出来
      5. worker 一创建，后台线程就启动了
  */
  AsyncLogger logger("async_logger", outputs);

  // 启动多个业务线程，连续写日志。
  const int thread_count = 4;            // 开4个业务线程
  const int log_count_per_thread = 2000; // 每个线程写 2000 条日志
  std::vector<std::thread> workers; // workers 用来把这些线程对象存起来

  for (int i = 0; i < thread_count; ++i) {
    workers.emplace_back(
        [&logger, i, log_count_per_thread]() { // 这里构造了线程
          for (int j = 0; j < log_count_per_thread; ++j) {
            logger.Info(__FILE__, __LINE__, "thread=%d, log_index=%d", i, j);
          }
        });
  }

  for (auto &worker : workers) {
    worker.join();
  }

  return 0;
}
