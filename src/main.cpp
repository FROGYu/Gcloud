#include "Net/Client/ClientMain.hpp"
#include "Net/Server/ServerMain.hpp"

#include <cstdio>
#include <string>

namespace {

/*
    这里打印统一用法说明。当前总入口只负责模式分发，不直接参与服务端和客户端的具
    体业务，所以命令行帮助也统一收在这里维护。
*/
void PrintUsage(const char* program_name) {
  std::fprintf(stderr,
               "Usage:\n"
               "  %s server [listen_ip] [listen_port]\n"
               "  %s client [server_ip] [server_port] [sync_dir] [backup_file]\n",
               program_name, program_name);
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
  }

  const std::string mode = argv[1];
  if (mode == "server") {
    // argv + 1 表示把 "server" 当作子模块自己的 argv[0]，后面的监听参数继续顺次转发。
    return RunServerMain(argc - 1, argv + 1);
  }

  if (mode == "client") {
    // argv + 1 表示把 "client" 当作子模块自己的 argv[0]，后面的同步参数继续顺次转发。
    return RunClientMain(argc - 1, argv + 1);
  }

  PrintUsage(argv[0]);
  return 1;
}
