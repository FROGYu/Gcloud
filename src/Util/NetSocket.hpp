#pragma once

#include "Util/NetAddress.hpp"
#include "Util/UniqueFd.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

/*
是最上层的网络创建工具，真正给外面提供“创建监听 socket / 创建客户端
socket”这两个入口： CreateTcpListenSocket 和 CreateTcpClientSocket。它内部会调用
NetAddress 里的地址构造函数来准备 sockaddr_in，同时把最终得到的裸 fd 包装成
UniqueFd 返回，所以它同时依赖 NetAddress 和 UniqueFd。
*/

// 创建监听 socket，并完成 bind 和 listen。
inline UniqueFd CreateTcpListenSocket(int port, int backlog) {
  int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0); // 创建 TCP 监听 socket。
  if (listen_fd < 0) {
    throw std::runtime_error("socket 创建失败");
  }

  int opt = 1; // 1 表示打开 SO_REUSEADDR。
  if (::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) <
      0) {
    ::close(listen_fd);
    throw std::runtime_error("setsockopt 设置失败");
  }

  sockaddr_in server_addr = BuildAnyIpv4Address(port); // 监听本机所有网卡。

  if (::bind(listen_fd, AsSockAddr(&server_addr), sizeof(server_addr)) < 0) {
    ::close(listen_fd);
    throw std::runtime_error("bind 绑定失败");
  }

  if (::listen(listen_fd, backlog) < 0) {
    ::close(listen_fd);
    throw std::runtime_error("listen 启动监听失败");
  }

  return UniqueFd(listen_fd);
}

// 创建客户端 socket，并连接到指定服务端。
inline UniqueFd CreateTcpClientSocket(const char *ip, int port) {
  int client_fd = ::socket(AF_INET, SOCK_STREAM, 0); // 创建客户端 socket。
  if (client_fd < 0) {
    throw std::runtime_error("socket 创建失败");
  }

  sockaddr_in server_addr{};
  try {
    server_addr = BuildIpv4Address(ip, port); // 组装服务端地址。
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

// 把 IPv4 地址结构转成 "ip:port" 字符串。
inline std::string FormatIpv4Address(const sockaddr_in &addr) {
  char ip[INET_ADDRSTRLEN] = {0}; // 保存点分十进制 IP。
  ::inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
  return std::string(ip) + ":" + std::to_string(ntohs(addr.sin_port));
}
