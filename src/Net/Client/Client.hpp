#pragma once

#include "Logger/LogMacros.hpp"
#include "Net/Client/Data/FileStateTable.hpp"

#include <httplib.h>

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
  Client() = default;

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

 private:
  // file_state_table_ 保存客户端记住的本地文件状态，后面扫描目录时会先拿它判断是否要上传。
  FileStateTable file_state_table_;
};
