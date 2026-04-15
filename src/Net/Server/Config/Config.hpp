#pragma once

#include <cstdint>
#include <string>

/*
    Config 是服务端的全局配置中心。

    它负责保存 HTTP 服务运行时会反复使用的一组固定参数，比如监听地址、监听端口、
    下载路由前缀、普通存储目录、压缩存储目录和元数据备份文件路径。后面的 Service、
    上传下载逻辑和元数据管理层都应该统一从这里读取配置，而不是在业务代码里到处写
    死字符串。
*/
class Config {
 public:
  /*
      这里返回全局唯一的配置对象。整个服务进程只保留一份配置，其他模块都通过这个
      入口访问同一份配置数据。
  */
  static Config& Instance() {
    static Config instance;
    return instance;
  }

  Config(const Config&) = delete;
  Config& operator=(const Config&) = delete;

  // 返回服务端当前监听的端口。
  uint16_t GetServerPort() const { return server_port_; }
  // 返回服务端当前监听的 IP 地址。
  const std::string& GetServerIp() const { return server_ip_; }
  // 返回下载接口使用的统一路由前缀。
  const std::string& GetDownloadPrefix() const { return download_prefix_; }
  // 返回压缩文件使用的后缀名。
  const std::string& GetPackfileSuffix() const { return packfile_suffix_; }
  // 返回深度存储文件的保存目录。
  const std::string& GetPackDir() const { return pack_dir_; }
  // 返回普通存储文件的保存目录。
  const std::string& GetBackDir() const { return back_dir_; }
  // 返回元数据备份文件路径。
  const std::string& GetBackupFile() const { return backup_file_; }

  // 修改服务端监听端口。
  void SetServerPort(uint16_t server_port) { server_port_ = server_port; }
  // 修改服务端监听 IP 地址。
  void SetServerIp(const std::string& server_ip) { server_ip_ = server_ip; }
  // 修改下载接口的路由前缀。
  void SetDownloadPrefix(const std::string& download_prefix) { download_prefix_ = download_prefix; }
  // 修改压缩文件后缀名。
  void SetPackfileSuffix(const std::string& packfile_suffix) { packfile_suffix_ = packfile_suffix; }
  // 修改深度存储目录。
  void SetPackDir(const std::string& pack_dir) { pack_dir_ = pack_dir; }
  // 修改普通存储目录。
  void SetBackDir(const std::string& back_dir) { back_dir_ = back_dir; }
  // 修改元数据备份文件路径。
  void SetBackupFile(const std::string& backup_file) { backup_file_ = backup_file; }

 private:
  /*
      构造函数里先写一组默认值。当前阶段先让系统具备“开箱可用”的默认配置，后面再
      扩展成从配置文件读取。
  */
  Config() = default;

 private:
  // server_port_ 保存服务默认监听端口。
  uint16_t server_port_ = 8080;

  // server_ip_ 保存服务默认监听地址。
  std::string server_ip_ = "0.0.0.0";

  // download_prefix_ 保存下载接口的统一路由前缀。
  std::string download_prefix_ = "/download/";

  // packfile_suffix_ 保存压缩文件的后缀名。
  std::string packfile_suffix_ = ".pack";

  // pack_dir_ 保存深度存储文件的默认目录。
  std::string pack_dir_ = "./packdir/";

  // back_dir_ 保存普通存储文件的默认目录。
  std::string back_dir_ = "./backdir/";

  // backup_file_ 保存元数据备份文件的默认路径。
  std::string backup_file_ = "./backup.json";
};
