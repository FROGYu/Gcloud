#pragma once

#include <ctime>
#include <string>

/*
    TimeUtil 是当前项目的时间格式化工具。

    它只负责把程序里使用的时间戳转换成人类可读的字符串，后面主页展示文件修改时
    间、日志辅助展示等场景都可以复用这里的接口。
*/
namespace TimeUtil {

/*
    这里把 time_t 时间戳格式化成 "YYYY-MM-DD HH:MM:SS" 字符串，方便直接展示到
    HTML 页面里。
*/
inline std::string FormatTime(time_t timestamp) {
  std::tm tm_time{};

#ifdef _WIN32
  if (localtime_s(&tm_time, &timestamp) != 0) {
    return "";
  }
#else
  if (localtime_r(&timestamp, &tm_time) == nullptr) {
    return "";
  }
#endif

  char buffer[64] = {0};
  if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm_time) == 0) {
    return "";
  }

  return std::string(buffer);
}

}  // namespace TimeUtil
