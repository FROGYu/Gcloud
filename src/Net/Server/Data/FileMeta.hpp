#pragma once

#include <ctime>
#include <string>

/*
    FileMeta 表示一条文件元数据记录。

    它只负责保存单个文件当前的存储状态，后面的
    DataManager 会统一管理很多条 FileMeta，主页展示、下载查询和元数据备份都会依赖
    这份结构。
*/
struct FileMeta {
  // 标记当前文件是否已经进入压缩存储。
  bool is_packed_ = false;

  // 保存文件的原始大小，后面主页展示和下载响应都会依赖它。
  size_t file_size_ = 0;

  // 保存文件最后一次修改时间，后面可以用来展示更新时间或做简单同步判断。
  time_t modify_time_ = 0;

  // 保存文件在服务器磁盘上的真实路径，下载和删除文件时会直接使用它。
  std::string real_path_;
};
