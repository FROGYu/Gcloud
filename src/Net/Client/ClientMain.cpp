#include "Net/Client/ClientMain.hpp"

#include "Net/Client/Client.hpp"
#include "Util/LoggerInit.hpp"

#include <cstdint>
#include <string>

namespace {

constexpr size_t kClientLogMaxFileSize = 10 * 1024 * 1024;

const RotateLoggerConfig kClientLoggerConfig = {
    .logger_name = "default",
    .base_file_name = "client.log",
    .max_file_size = kClientLogMaxFileSize,
};

/*
    这里把命令行里的端口文本转换成 uint16_t。客户端后面既支持默认端口，也支持在
    启动时通过命令行覆盖服务端端口。
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

int RunClientMain(int argc, char* argv[]) {
  // 客户端入口同样先初始化默认异步日志器，后面的扫描和上传日志都统一记录到这里。
  InitDefaultRotateLogger(__FILE__, kClientLoggerConfig);

  std::string server_ip = "127.0.0.1";
  uint16_t server_port = 8080;
  std::string sync_dir = "./client_sync/";
  std::string backup_file = "./client_backup.json";

  if (argc >= 2) {
    server_ip = argv[1];
  }

  if (argc >= 3) {
    if (!ParsePort(argv[2], &server_port)) {
      LOG_ERROR("启动客户端失败, 端口参数非法, argv[2]=%s", argv[2]);
      return 1;
    }
  }

  if (argc >= 4) {
    sync_dir = argv[3];
  }

  if (argc >= 5) {
    backup_file = argv[4];
  }

  Client client(server_ip, server_port, sync_dir, backup_file);
  client.Run();
  return 0;
}
