#pragma once

#include "Logger/LogMacros.hpp"
#include "Net/Client/Data/FileStateTable.hpp"
#include "Util/FileUtil.hpp"

#include <httplib.h>

#include <cstdint>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

/*
    Client 所在层级：云存储系统的“客户端同步层”。

    这一层后面负责串联本地目录扫描、状态比对和 HTTP 上传。当前这一版先把客户端的
    基础骨架搭起来，并补齐文件 ETag 的生成逻辑，给后面的扫描和上传流程做准备。
*/
class Client {
 public:
  Client(std::string server_ip = "127.0.0.1", uint16_t server_port = 8080)
      : server_ip_(std::move(server_ip)), server_port_(server_port) {}

  ~Client() = default;

  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

 private:
  /*
      这里根据文件当前状态生成一个轻量级 ETag。客户端后面每次扫描目录时，都会拿这
      个字符串和 FileStateTable 里的历史记录做比较，只要值变了，就说明文件需要重
      新上传。
  */
  std::string GetETag(const std::string& filepath) const {
    std::error_code ec;
    const fs::path file_path(filepath);

    // 这里只接受真实存在的普通文件，目录、软链接异常和不存在的路径都不生成 ETag。
    if (!fs::exists(file_path, ec) || ec || !fs::is_regular_file(file_path, ec) || ec) {
      LOG_ERROR("生成 ETag 失败, 文件不存在或不是普通文件, path=%s", filepath.c_str());
      return "";
    }

    const auto file_size = fs::file_size(file_path, ec);
    if (ec) {
      LOG_ERROR("生成 ETag 失败, 获取文件大小失败, path=%s", filepath.c_str());
      return "";
    }

    const auto write_time = fs::last_write_time(file_path, ec);
    if (ec) {
      LOG_ERROR("生成 ETag 失败, 获取文件修改时间失败, path=%s", filepath.c_str());
      return "";
    }

    // last_write_time 返回的是文件系统时钟时间点，这里直接取内部 tick 值参与拼接，
    // 不去做复杂的 time_t 转换，只把它当成“这次修改时间的稳定标识”来使用。
    return std::to_string(file_size) + "-" +
           std::to_string(write_time.time_since_epoch().count());
  }

  /*
      这里上传一个本地文件。它先把 filepath 对应文件整体读进内存，再构造同步 HTTP
      请求发给服务端的 /upload 接口。当前客户端默认都按 deep 模式上传，用来直接测
      试服务端的压缩存储链路。
  */
  bool Upload(const std::string& filepath, const std::string& filename) const {
    std::string file_body;
    if (!FileUtil::ReadFile(filepath, &file_body)) {
      LOG_ERROR("上传文件失败, 读取本地文件失败, filepath=%s", filepath.c_str());
      return false;
    }

    // httplib::Client 是 cpp-httplib 提供的同步 HTTP 客户端，这里用它直连服务端。
    httplib::Client cli(server_ip_, server_port_);

    // httplib::Headers 表示一组 HTTP 请求头，后面会跟着 POST 请求一起发给服务端。
    httplib::Headers headers = {
        {"File-Name", filename},
        {"Store-Type", "deep"},
    };

    // Post 会同步发起 HTTP POST 请求：路径是 /upload，请求体就是整个文件正文。
    auto response = cli.Post("/upload", headers, file_body, "application/octet-stream");
    if (!response) {
      LOG_ERROR("上传文件失败, HTTP 请求没有拿到有效响应, filepath=%s, filename=%s",
                filepath.c_str(), filename.c_str());
      return false;
    }

    if (response->status != 200) {
      LOG_ERROR("上传文件失败, 服务端返回异常状态码, filepath=%s, filename=%s, status=%d",
                filepath.c_str(), filename.c_str(), response->status);
      return false;
    }

    LOG_INFO("上传文件成功, filepath=%s, filename=%s", filepath.c_str(), filename.c_str());
    return true;
  }

 private:
  // server_ip_ 保存目标服务端地址，后面的同步上传请求会直接连到这里。
  std::string server_ip_ = "127.0.0.1";

  // server_port_ 保存目标服务端端口，和 server_ip_ 一起组成上传目标。
  uint16_t server_port_ = 8080;

  // file_state_table_ 保存客户端记住的本地文件状态，后面扫描目录时会先拿它判断是否要上传。
  FileStateTable file_state_table_;
};
