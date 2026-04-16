#pragma once

#include <cstddef>
#include <exception>
#include <string>

/*
    HttpRange 是当前项目的 HTTP Range 解析工具。

    它只负责解析客户端传来的 Range 请求头，把 "bytes=100-200" 这样的字符串拆成
    起始字节和结束字节。后面的下载逻辑只需要拿解析结果决定返回哪一段数据，不需要
    自己再处理字符串细节。
*/
namespace HttpRange {

/*
    这里解析 HTTP Range 字符串，并按文件总大小做边界校验。

    支持的格式：
    - bytes=100-200
    - bytes=100-
*/
inline bool ParseRange(const std::string& range_str, size_t file_size, size_t* start,
                       size_t* end) {
  if (start == nullptr || end == nullptr || file_size == 0) {
    return false;
  }

  const std::string prefix = "bytes=";
  if (range_str.rfind(prefix, 0) != 0) {
    return false;
  }

  const std::string range_body = range_str.substr(prefix.size());
  const size_t dash_pos = range_body.find('-');
  if (dash_pos == std::string::npos) {
    return false;
  }

  const std::string start_str = range_body.substr(0, dash_pos);
  const std::string end_str = range_body.substr(dash_pos + 1);
  if (start_str.empty()) {
    return false;
  }

  try {
    size_t parsed_chars = 0;

    // stoull 会把十进制字符串转成无符号整数，parsed_chars 用来确认整段都被正确解析了。
    *start = static_cast<size_t>(std::stoull(start_str, &parsed_chars));
    if (parsed_chars != start_str.size()) {
      return false;
    }

    if (end_str.empty()) {
      *end = file_size - 1;
    } else {
      *end = static_cast<size_t>(std::stoull(end_str, &parsed_chars));
      if (parsed_chars != end_str.size()) {
        return false;
      }
    }
  } catch (const std::exception&) {
    return false;
  }

  // 这里只接受落在文件范围内、并且起点不大于终点的字节区间。
  if (*start > *end || *end >= file_size) {
    return false;
  }

  return true;
}

}  // namespace HttpRange
