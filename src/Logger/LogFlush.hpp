#pragma once

#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

/*
    需要 LogFlush 的原因：

    Logger 的职责是“生成一条完整日志”。
    但日志生成以后，可能交给不同的输出器：
    - 输出到终端
    - 写入文件
    - 以后也可以扩展成写入网络或别的地方

    如果把这些写法都直接写进 Logger，Logger 会越来越臃肿，
    而且每增加一种输出器，都要修改 Logger 本身。

    所以这里单独抽出一个基类 LogFlush。
    它只规定一件事：
    “已经生成好的日志字符串，应该交给哪个输出器处理。”
*/
class LogFlush {
public:
  virtual ~LogFlush() = default;

  /*
      Flush:

      供谁调用：
      - Logger::publishLog
      - AsyncLogger::consume

      参数：
      - data: 已经格式化好的日志字符串数据
      - len:  data 的字节长度

      行为：
      - 子类决定如何输出这段日志（终端/文件等）
  */
  virtual void Flush(const char *data, size_t len) = 0;
};

// StdoutFlush 是终端输出器。
class StdoutFlush : public LogFlush {
public:
  /*
      Flush:
      - 直接写到 std::cout
      - flush 确保立即输出到终端
  */
  void Flush(const char *data, size_t len) override {
    std::cout.write(data, static_cast<std::streamsize>(len));
    std::cout.flush();
  }
};

/*
    FileFlush 是文件输出器。

    这里使用 C++ 的 ofstream，而不是
   FILE*。之后如果有IO瓶颈再修改为底层的C风格。
*/
class FileFlush : public LogFlush {
public:
  /*
      构造函数：
      - file_path: 日志文件路径
      - 打开失败时抛异常，避免对象处于不可用状态
  */
  explicit FileFlush(const std::string &file_path)
      : ofs_(file_path, std::ios::app | std::ios::binary) {
    if (!ofs_.is_open()) {
      throw std::runtime_error("日志文件打开失败: " + file_path);
    }
  }

  /*
      Flush:
      - 把日志写入文件
      - flush 立即刷新到磁盘
  */
  void Flush(const char *data, size_t len) override {
    ofs_.write(data, static_cast<std::streamsize>(len));
    ofs_.flush();
  }

private:
  std::ofstream ofs_;
};
