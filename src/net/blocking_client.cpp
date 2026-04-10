#include "Util/NetAddress.hpp"
#include "Util/UniqueFd.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace {
// k 前缀表示常量。
constexpr const char *kServerIp = "127.0.0.1";
constexpr int kServerPort = 9090;
// 一次 read 最多接收的字节数。
constexpr size_t kBufferSize = 1024;

/*
    createClientSocket:

    作用：
    - 创建客户端 socket
    - 连接到服务端

    返回：
    - 已连接 socket 的文件描述符
*/
UniqueFd createClientSocket() {
  // AF_INET 表示 IPv4。
  // SOCK_STREAM 表示面向连接的 TCP。
  int client_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (client_fd < 0) {
    throw std::runtime_error("socket 创建失败");
  }

  sockaddr_in server_addr{};
  try {
    server_addr = BuildIpv4Address(kServerIp, kServerPort);
  } catch (const std::exception &) {
    ::close(client_fd);
    throw std::runtime_error("服务器 IP 地址无效");
  }

  if (::connect(client_fd, AsSockAddr(&server_addr), sizeof(server_addr)) < 0) {
    ::close(client_fd);
    throw std::runtime_error("connect 连接失败");
  }

  return UniqueFd(client_fd);
}

/*
    runClient:

    参数：
    - client_fd: 已连接 socket

    作用：
    - 从终端读取一行文本
    - 发送给服务端
    - 再读取服务端回显的数据

    输入 quit 时退出。
*/
void runClient(UniqueFd client_fd) {
  // 服务端回显缓冲区。
  char buffer[kBufferSize] = {0};

  while (true) {
    std::cout << "请输入要发送的内容，输入 quit 退出:\n";

    std::string message;
    if (!std::getline(std::cin, message)) {
      break;
    }

    if (message == "quit") {
      break;
    }

    const ssize_t write_size =
        ::write(client_fd.get(), message.data(), message.size());
    if (write_size < 0) {
      std::cerr << "write 失败: " << std::strerror(errno) << '\n';
      break;
    }

    const ssize_t read_size = ::read(client_fd.get(), buffer, sizeof(buffer));
    if (read_size < 0) {
      std::cerr << "read 失败: " << std::strerror(errno) << '\n';
      break;
    }

    if (read_size == 0) {
      std::cout << "服务端已经断开连接\n";
      break;
    }

    std::string reply(buffer, buffer + read_size);
    std::cout << "服务端回显: " << reply << '\n';
    std::memset(buffer, 0, sizeof(buffer));
  }
  // client_fd 在离开作用域时自动关闭。
}
} // namespace

int main() {
  try {
    UniqueFd client_fd = createClientSocket();
    std::cout << "已连接到服务端 " << kServerIp << ':' << kServerPort << '\n';
    runClient(std::move(client_fd));
  } catch (const std::exception &ex) {
    std::cerr << "客户端启动失败: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}
