
#include "Logger/Logger.hpp"

#include <memory>
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

  // 创建日志器，并把输出器列表交给它。
  Logger logger("main_logger", outputs);

  // 调用日志接口，验证日志是否能同时输出到终端和文件。
  logger.Info(__FILE__, __LINE__, "program start, value=%d", 10);
  logger.Error(__FILE__, __LINE__, "open file failed, code=%d", -1);

  return 0;
}
