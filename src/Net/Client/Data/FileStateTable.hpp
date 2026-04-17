#pragma once

#include "Logger/LogMacros.hpp"
#include "Net/Client/Data/FileState.hpp"
#include "Util/FileUtil.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_map>

/*
    FileStateTable 是客户端的同步状态表。

    它负责在内存里维护“文件名 -> ETag”的对应关系。客户端每次扫描本地目录时，
    都会先来这里判断文件是否已经同步过，以及当前文件状态是不是已经变化。只要文件
    名不存在，或者 ETag 和上次记录的不一样，就说明这个文件需要重新上传。
*/
class FileStateTable {
 public:
  FileStateTable() = default;

  /*
      负责更新“这次最新状态是什么”
      这里记录一条客户端文件状态。只要文件名和 etag 有效，就把最新状态写进
      表里，供下一轮扫描时继续比对。
  */
  bool Record(const std::string& filename, const std::string& etag) {
    if (filename.empty() || etag.empty()) {
      return false;
    }

    files_[filename] = FileState{.etag_ = etag};
    return true;
  }

  /*
      这里判断某个文件当前状态是不是和表里记录的一样。只有文件名存在，并且表里的
      etag 和这次扫描出来的一致，才返回 true。
  */
  bool IsSameState(const std::string& filename, const std::string& etag) const {
    if (filename.empty() || etag.empty()) {
      return false;
    }

    auto it = files_.find(filename);
    if (it == files_.end()) {
      return false;
    }

    return it->second.etag_ == etag;
  }

  /*
      这里把整张客户端状态表序列化成 JSON 文本，再整体写入备份文件。这样客户端重启
      后，不会把本地所有文件都误判成“第一次看到”。
  */
  bool Save(const std::string& backup_file) const {
    if (backup_file.empty()) {
      return false;
    }

    // json::array() 明确说明最外层要构造的是 JSON 数组。
    nlohmann::json root = nlohmann::json::array();
    for (const auto& [filename, state] : files_) {
      nlohmann::json item;

      // item 表示数组中的“一条文件状态记录”，这里按字段名逐项写入。
      item["filename"] = filename;
      item["etag"] = state.etag_;

      // push_back 会把当前这条记录追加到 JSON 数组末尾。
      root.push_back(item);
    }

    // dump(4) 会把 JSON 结构序列化成带 4 空格缩进的文本，便于人工查看。
    return FileUtil::WriteFile(backup_file, root.dump(4));
  }

  /*
      这里从备份文件恢复客户端状态表。第一次启动时如果文件不存在或内容为空，就直接
      当成“还没有历史记录”处理，不算错误。
  */
  bool Load(const std::string& backup_file) {
    if (backup_file.empty()) {
      return false;
    }

    std::string backup_body;
    if (!FileUtil::ReadFile(backup_file, &backup_body) || backup_body.empty()) {
      return true;
    }

    try {
      // parse 会把 JSON 文本解析成内存里的 JSON 结构。
      nlohmann::json root = nlohmann::json::parse(backup_body);
      if (!root.is_array()) {
        LOG_ERROR("加载客户端状态表失败, 备份文件不是 JSON 数组, file=%s", backup_file.c_str());
        return false;
      }

      std::unordered_map<std::string, FileState> new_files;
      for (const auto& item : root) {
        // get<T>() 会把 JSON 字段取出来，并转换成目标 C++ 类型。
        const std::string filename = item.at("filename").get<std::string>();
        const std::string etag = item.at("etag").get<std::string>();
        new_files[filename] = FileState{.etag_ = etag};
      }

      files_ = std::move(new_files);
      return true;
    } catch (const nlohmann::json::exception& e) {
      LOG_ERROR("加载客户端状态表失败, backup_file=%s, reason=%s", backup_file.c_str(), e.what());
      return false;
    }
  }

  /*
      这里清空整张客户端状态表。后面做测试、重建同步状态或重新加载时会用到。
  */
  void Clear() { files_.clear(); }

  /*
      这里返回当前一共记录了多少个本地文件状态。
  */
  size_t Size() const { return files_.size(); }

 private:
  // files_ 保存“文件名 -> FileState”的对应关系，是客户端判断是否需要重新上传的依据。
  std::unordered_map<std::string, FileState> files_;
};
