#pragma once

#include "Net/Server/Data/FileMeta.hpp"

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
    auto [it, inserted] = files_.emplace(filename, meta);
    return inserted;
  }

  /*
      这里按文件名更新已有记录。如果目标文件不存在，就返回 false，表示这次更新没有
      命中任何旧记录。
  */
  bool Update(const std::string& filename, const FileMeta& meta) {
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
    return files_.find(filename) != files_.end();
  }

  /*
      这里按文件名删除一条记录。删到返回 true，目标不存在返回 false。
  */
  bool Remove(const std::string& filename) {
    return files_.erase(filename) > 0;
  }

  /*
      这里返回整张元数据表的只读引用。后面主页展示文件列表时，可以直接遍历这张表。
  */
  const std::unordered_map<std::string, FileMeta>& All() const { return files_; }

  /*
      这里返回当前元数据表里一共有多少条文件记录。
  */
  size_t Size() const { return files_.size(); }

  /*
      这里清空整张元数据表。后面做元数据恢复、测试重置或重新加载时都可能会用到。
  */
  void Clear() { files_.clear(); }

  // 这里预留把整张元数据表保存到备份文件的入口，后面接入 JSON 持久化时再补实现。
  bool Store(const std::string& backup_file) const {
    (void)backup_file;
    return false;
  }

  // 这里预留从备份文件恢复整张元数据表的入口，后面接入 JSON 反序列化时再补实现。
  bool Load(const std::string& backup_file) {
    (void)backup_file;
    return false;
  }

 private:
  // files_ 保存“文件名 -> 文件元数据”的对应关系。
  std::unordered_map<std::string, FileMeta> files_;
};
