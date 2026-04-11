#pragma once

#include "Util/NetAddress.hpp"
#include "Util/UniqueFd.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

inline UniqueFd CreateTcpListenSocket(int port, int backlog) {
  int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);  // 创建 TCP 监听 socket。
  if (listen_fd < 0) {
    throw std::runtime_error("socket 创建失败");
  }

  int opt = 1;  // 1 表示打开 SO_REUSEADDR。
  if (::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) <
      0) {
    ::close(listen_fd);
    throw std::runtime_error("setsockopt 设置失败");
  }

  sockaddr_in server_addr = BuildAnyIpv4Address(port);  // 监听本机所有网卡。

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

inline UniqueFd CreateTcpClientSocket(const char *ip, int port) {
  int client_fd = ::socket(AF_INET, SOCK_STREAM, 0);  // 创建客户端 socket。
  if (client_fd < 0) {
    throw std::runtime_error("socket 创建失败");
  }

  sockaddr_in server_addr{};
  try {
    server_addr = BuildIpv4Address(ip, port);  // 组装服务端地址。
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

inline std::string FormatIpv4Address(const sockaddr_in &addr) {
  char ip[INET_ADDRSTRLEN] = {0};  // 保存点分十进制 IP。
  ::inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
  return std::string(ip) + ":" + std::to_string(ntohs(addr.sin_port));
}
