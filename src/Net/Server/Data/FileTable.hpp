#pragma once

#include "Net/Server/Data/FileMeta.hpp"
#include "Util/FileUtil.hpp"

#include <nlohmann/json.hpp>

#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

/*
    FileTable 是文件元数据表。

    它负责在内存里维护“文件名 -> FileMeta”的对应关系，后面的上传、下载、主页展示
    都会先从这里增删查文件记录。当前这一版先只处理内存中的表操作，持久化保存和恢
    复后面再单独接入。
*/
class FileTable {
 public:
  FileTable() = default;

  /*
      这里向表里新增一条文件记录。如果文件名已经存在，就返回 false，避免不小心把
      已有记录静默覆盖掉。
  */
  bool Insert(const std::string& filename, const FileMeta& meta) {
    std::unique_lock<std::shared_mutex> lock(rw_lock_);
    auto [it, inserted] = files_.emplace(filename, meta);
    return inserted;
  }

  /*
      这里按文件名更新已有记录。如果目标文件不存在，就返回 false，表示这次更新没有
      命中任何旧记录。
  */
  bool Update(const std::string& filename, const FileMeta& meta) {
    std::unique_lock<std::shared_mutex> lock(rw_lock_);
    auto it = files_.find(filename);
    if (it == files_.end()) {
      return false;
    }

    it->second = meta;
    return true;
  }

  /*
      这里按文件名查询一条记录。查到后把结果写进 meta，查不到就返回 false。
  */
  bool Get(const std::string& filename, FileMeta* meta) const {
    std::shared_lock<std::shared_mutex> lock(rw_lock_);
    if (meta == nullptr) {
      return false;
    }

    auto it = files_.find(filename);
    if (it == files_.end()) {
      return false;
    }

    *meta = it->second;
    return true;
  }

  /*
      这里判断某个文件名是否已经出现在元数据表里，上传防重和下载查询都会依赖这个判
      断结果。
  */
  bool Exists(const std::string& filename) const {
    std::shared_lock<std::shared_mutex> lock(rw_lock_);
    return files_.find(filename) != files_.end();
  }

  /*
      这里按文件名删除一条记录。删到返回 true，目标不存在返回 false。
  */
  bool Remove(const std::string& filename) {
    std::unique_lock<std::shared_mutex> lock(rw_lock_);
    return files_.erase(filename) > 0;
  }

  /*
      这里返回整张元数据表的快照副本。因为内部已经加了读写锁，如果直接把内部 map
      的引用返回出去，函数结束后锁会释放，外面继续用这份引用就不安全了。
  */
  std::unordered_map<std::string, FileMeta> All() const {
    std::shared_lock<std::shared_mutex> lock(rw_lock_);
    return files_;
  }

  /*
      这里返回当前元数据表里一共有多少条文件记录。
  */
  size_t Size() const {
    std::shared_lock<std::shared_mutex> lock(rw_lock_);
    return files_.size();
  }

  /*
      这里清空整张元数据表。后面做元数据恢复、测试重置或重新加载时都可能会用到。
  */
  void Clear() {
    std::unique_lock<std::shared_mutex> lock(rw_lock_);
    files_.clear();
  }

  // 这里把整张元数据表序列化成 JSON 文本，再整体写入备份文件。
  bool Store(const std::string& backup_file) const {
    if (backup_file.empty()) {
      return false;
    }

    nlohmann::json root;
    {
      /*因为 files_ 可能被上传线程或后续后台线程同时修改，遍历时必须先用读锁把它保护住，
      但我又不想把锁持有到磁盘写入阶段，所以我选择“加读锁读表组 JSON，锁外再落盘”。*/
      std::shared_lock<std::shared_mutex> lock(rw_lock_);

      // json::array() 明确告诉库：这里要构造的是 JSON 数组，不是 JSON 对象。
      root = nlohmann::json::array();
      for (const auto& [filename, meta] : files_) {
        nlohmann::json item;

        // 这里就是按“字段名 -> 字段值”的方式给 JSON 对象逐项赋值。
        item["filename"] = filename;
        item["is_packed"] = meta.is_packed_;
        item["file_size"] = meta.file_size_;
        item["modify_time"] = meta.modify_time_;
        item["real_path"] = meta.real_path_;

        // push_back 会把当前这一条文件记录追加到 JSON 数组末尾。
        root.push_back(item);
      }
    }

    // dump(4) 会把 JSON 序列化成字符串，4 表示每一级缩进 4 个空格，便于人读。
    const std::string backup_body = root.dump(4);
    return FileUtil::WriteFile(backup_file, backup_body);
  }

  // 这里预留从备份文件恢复整张元数据表的入口，后面接入 JSON 反序列化时再补实现。
  bool Load(const std::string& backup_file) {
    std::unique_lock<std::shared_mutex> lock(rw_lock_);
    (void)backup_file;
    return false;
  }

 private:
  // files_ 保存“文件名 -> 文件元数据”的对应关系。
  std::unordered_map<std::string, FileMeta> files_;

  // rw_lock_ 保护 files_，后面网络线程读、后台线程写时都要先经过这把读写锁。
  mutable std::shared_mutex rw_lock_;  //mutable允许这个成员在 const 函数里也能改
};
