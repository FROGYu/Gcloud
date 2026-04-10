#include "Util/NetAddress.hpp"
#include "Util/UniqueFd.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace {
// k 前缀表示常量。
constexpr int kListenPort = 9090;
// listen 的等待队列长度。
constexpr int kBacklog = 8;
// 一次 read 最多读取的字节数。
constexpr size_t kBufferSize = 1024;

// 当前监听 socket。
UniqueFd g_listen_fd;

/*
    handleSignal:

    作用：
    - 在收到退出信号时关闭监听 socket
*/
void handleSignal(int) {
  if (g_listen_fd.valid()) {
    // 先关闭监听 socket，再结束进程。
    g_listen_fd.reset();
  }
  std::exit(0);
}

/*
    createListenSocket:

    作用：
    - 完成阻塞式服务端最开始的四步：
      1. socket
      2. bind
      3. listen

    返回：
    - 监听 socket 的文件描述符
*/
UniqueFd createListenSocket() {
  // AF_INET 表示 IPv4。
  // SOCK_STREAM 表示面向连接的 TCP。
  int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    throw std::runtime_error("socket 创建失败");
  }

  int opt = 1;
  // 允许服务端更快重绑同一个端口。
  if (::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    ::close(listen_fd);
    throw std::runtime_error("setsockopt 设置失败");
  }

  sockaddr_in server_addr = BuildAnyIpv4Address(kListenPort);

  if (::bind(listen_fd, AsSockAddr(&server_addr), sizeof(server_addr)) < 0) {
    ::close(listen_fd);
    throw std::runtime_error("bind 绑定失败");
  }

  if (::listen(listen_fd, kBacklog) < 0) {
    ::close(listen_fd);
    throw std::runtime_error("listen 启动监听失败");
  }

  return UniqueFd(listen_fd);
}

/*
    handleClient:

    参数：
    - client_fd: accept 返回的已连接 socket

    作用：
    - 循环读取客户端发来的数据
    - 再把数据原样写回去
    - 如果客户端不发数据，read 会阻塞在这里
*/
void handleClient(UniqueFd client_fd) {
  // 临时接收缓冲区。
  char buffer[kBufferSize] = {0};

  while (true) {
    // read 返回：
    // - 正数：读取到的字节数
    // - 0：客户端关闭连接
    // - 负数：读取失败
    const ssize_t read_size = ::read(client_fd.get(), buffer, sizeof(buffer));

    if (read_size < 0) {
      std::cerr << "read 失败: " << std::strerror(errno) << '\n';
      break;
    }

    if (read_size == 0) {
      std::cout << "客户端已经断开连接\n";
      break;
    }

    std::string message(buffer, buffer + read_size);
    std::cout << "收到客户端数据: " << message << '\n';

    const ssize_t write_size =
        ::write(client_fd.get(), buffer, static_cast<size_t>(read_size));
    if (write_size < 0) {
      std::cerr << "write 失败: " << std::strerror(errno) << '\n';
      break;
    }
  }
  // client_fd 在离开作用域时自动关闭。
}

/*
    runServer:

    参数：
    - listen_fd: 监听 socket

    作用：
    - 一直调用 accept 等待新连接
    - 接到一个连接后，立刻进入 handleClient
    - 没有客户端连接时，accept 会阻塞
    - 一个客户端没处理完之前，服务端不会回到 accept 去接下一个客户端
*/
void runServer(const UniqueFd &listen_fd) {
  while (true) {
    std::cout << "等待客户端连接，当前监听端口: " << kListenPort << '\n';

    sockaddr_in client_addr{};
    socklen_t client_addr_len = sizeof(client_addr);
    // accept 返回新的连接 fd。
    const int accepted_fd =
        ::accept(listen_fd.get(), AsSockAddr(&client_addr), &client_addr_len);

    if (accepted_fd < 0) {
      std::cerr << "accept 失败: " << std::strerror(errno) << '\n';
      continue;
    }

    char client_ip[INET_ADDRSTRLEN] = {0};
    ::inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    std::cout << "客户端已连接: " << client_ip << ':'
              << ntohs(client_addr.sin_port) << '\n';

    handleClient(UniqueFd(accepted_fd));
  }
}
} // namespace

int main() {
  // Ctrl+C 时执行 handleSignal。
  std::signal(SIGINT, handleSignal);

  try {
    g_listen_fd = createListenSocket();
    runServer(g_listen_fd);
  } catch (const std::exception &ex) {
    std::cerr << "服务端启动失败: " << ex.what() << '\n';
    if (g_listen_fd.valid()) {
      g_listen_fd.reset();
    }
    return 1;
  }

  return 0;
}
