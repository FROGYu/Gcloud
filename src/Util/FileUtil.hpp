#pragma once

#include <filesystem>
#include <fstream>
#include <string>

/*
    FileUtil 是当前项目的文件操作工具层。

    它负责把“读整个文件、写整个文件、获取文件大小”这几件基础能力统一，
    避免后面的主页、上传、下载逻辑直接散落着写 fstream 代码。
*/
namespace FileUtil {

/*
    ReadFile:

    这里把指定文件一次性读进内存。后面主页读取 HTML 模板、下载逻辑读取小文件时，
    都可以直接复用这个入口。把 file_path 对应文件的全部内容读进 body
*/
inline bool ReadFile(const std::string& file_path, std::string* body) {
  if (body == nullptr) {
    return false;
  }

  std::ifstream file(file_path, std::ios::binary);
  if (!file.is_open()) {
    return false;
  }

  // 先移动到文件尾部，取出整个文件的字节数。
  file.seekg(0, std::ios::end);
  std::streamoff file_size = file.tellg();
  if (file_size < 0) {
    body->clear();
    return false;
  }

  body->clear();
  body->resize(static_cast<size_t>(file_size));
  file.seekg(0, std::ios::beg);

  if (file_size == 0) {
    return true;
  }

  // 把整个文件内容一次性读进 body。
  if (!file.read(body->data(), file_size)) {
    body->clear();
    return false;
  }

  return true;
}

/*
    WriteFile:

    这里把内存里的整段数据一次性写到指定文件。后面处理上传文件落盘时，可以先直接
    用这个入口完成最简单的普通存储。
*/
inline bool WriteFile(const std::string& file_path, const std::string& body) {
  std::filesystem::path path(file_path);

  // 如果上层传进来的路径带父目录，就先把目录创建出来。
  if (path.has_parent_path()) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      return false;
    }
  }
  //打开文件:2进制（统一支持各种消息）截断模式trunc 打开文件时，把原来的内容清空
  std::ofstream file(file_path, std::ios::binary | std::ios::trunc);
  if (!file.is_open()) {
    return false;
  }

  // 把 body 里的全部字节写进目标文件。
  file.write(body.data(), static_cast<std::streamsize>(body.size()));
  if (!file) {
    return false;
  }

  return true;
}

/*
    FileSize:

    这里返回指定文件的字节数。后面主页展示文件列表时，可以直接拿这个结果给前端显
    示文件大小。
*/
inline size_t FileSize(const std::string& file_path) {
  std::error_code ec;
  uintmax_t file_size = std::filesystem::file_size(file_path, ec);
  if (ec) {
    return 0;
  }

  return static_cast<size_t>(file_size);
}

}  // namespace FileUtil
