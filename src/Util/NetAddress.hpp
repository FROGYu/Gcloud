#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdexcept>

/*
    NetAddress.hpp 负责准备 IPv4 地址结构体。
    以及把 sockaddr_in*
    转成系统调用需要的 sockaddr*（AsSockAddr）。
*/

/*
    BuildAnyIpv4Address:
    - 构造监听本机所有 IPv4 地址的 sockaddr_in
*/
inline sockaddr_in BuildAnyIpv4Address(int port) {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);
  return addr;
}

/*
    BuildIpv4Address:
    - 根据 IP 字符串和端口构造 sockaddr_in
*/
inline sockaddr_in BuildIpv4Address(const char *ip, int port) {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);

  if (::inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
    throw std::runtime_error("IPv4 地址无效");
  }

  return addr;
}

/*
    AsSockAddr:
    - 把 sockaddr_in* 转成 socket 系统调用需要的 sockaddr*
*/
// 可修改的地址对象
inline sockaddr *AsSockAddr(sockaddr_in *addr) {
  return reinterpret_cast<sockaddr *>(addr);
}
// 只读的地址对象

inline const sockaddr *AsSockAddr(const sockaddr_in *addr) {
  return reinterpret_cast<const sockaddr *>(addr);
}
