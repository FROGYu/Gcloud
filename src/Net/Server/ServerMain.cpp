#include "Net/Server/ServerMain.hpp"

#include "Net/Server/Config/Config.hpp"
#include "Net/Server/Service.hpp"
#include "Util/LoggerInit.hpp"

#include <cstdint>
#include <string>

namespace {

constexpr size_t kServerLogMaxFileSize = 10 * 1024 * 1024;

const RotateLoggerConfig kServerLoggerConfig = {
    .logger_name = "default",
    .base_file_name = "server.log",
    .max_file_size = kServerLogMaxFileSize,
};

/*
    这里把字符串端口转换成 uint16_t。main 后面解析命令行参数时，如果用户额外传了
    端口，就先经过这个函数校验，避免把非法文本直接塞进 Config。
*/
bool ParsePort(const std::string& port_text, uint16_t* port) {
  if (port == nullptr || port_text.empty()) {
    return false;
  }

  try {
    const unsigned long value = std::stoul(port_text);
    if (value > 65535) {
      return false;
    }

    *port = static_cast<uint16_t>(value);
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace

int RunServerMain(int argc, char* argv[]) {
  // 服务端入口先初始化默认异步日志器，后面的 Service 日志统一走这条输出链路。
  InitDefaultRotateLogger(__FILE__, kServerLoggerConfig);

  Config& config = Config::Instance();
  if (argc >= 2) {
    config.SetServerIp(argv[1]);
  }

  if (argc >= 3) {
    uint16_t port = 0;
    if (!ParsePort(argv[2], &port)) {
      LOG_ERROR("启动服务端失败, 端口参数非法, argv[2]=%s", argv[2]);
      return 1;
    }

    config.SetServerPort(port);
  }

  Service service;
  if (!service.Init(config.GetServerIp(), config.GetServerPort())) {
    LOG_ERROR("启动服务端失败, 初始化 Service 失败");
    return 1;
  }

  if (!service.Run()) {
    LOG_ERROR("服务端运行失败, 事件循环异常退出");
    return 1;
  }

  return 0;
}
