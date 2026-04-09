#pragma once

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
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

/*
    SizeRotateFileFlush 是“按文件大小切换日志文件”的文件输出器。

    它所在的层级：
    - 输出器层

    需要它的原因：
    - 如果一直往同一个日志文件里写，文件会越来越大
    - 文件过大以后，查看、传输、清理都会变得麻烦
    - 所以这里加一个“单个文件大小上限”
    - 当当前文件再写下去就会超过上限时，先切到新文件，再写这一条日志

    它负责什么：
    - 维护当前日志文件
    - 记录当前文件已经写了多少字节
    - 超过上限时切到新文件

    它不负责什么：
    - 不负责生成日志字符串
    - 不负责同步 / 异步逻辑
    - 不负责线程调度
*/
class SizeRotateFileFlush : public LogFlush {
public:
  /*
      构造函数：

      参数：
      - base_file_path: 日志文件的基础路径
        例如：./log/app.log
      - max_file_size: 单个日志文件允许的最大字节数

      行为：
      - 保存基础路径和大小上限
      - 立即打开第一个日志文件
  */
  SizeRotateFileFlush(const std::string &base_file_path, size_t max_file_size)
      : base_file_path_(base_file_path), max_file_size_(max_file_size) {
    if (max_file_size_ == 0) {
      throw std::invalid_argument("max_file_size 不能为 0");
    }
    openNewFile();
  }

  /*
      Flush:

      参数：
      - data: 已经格式化好的日志字符串数据
      - len:  data 的字节长度

      行为：
      - 如果当前文件再写这一条日志就会超过上限，先切到新文件
      - 然后把这条日志写入当前文件
      - 最后更新 current_file_size_
  */
  void Flush(const char *data, size_t len) override {
    if (!ofs_.is_open()) {
      openNewFile();
    }

    // 策略 A：
    // 如果“当前大小 + 这一条日志大小”会超过上限，
    // 先切换到新文件，再写这一条日志。
    if (current_file_size_ + len > max_file_size_) {
      openNewFile();
    }

    ofs_.write(data, static_cast<std::streamsize>(len));
    ofs_.flush();
    current_file_size_ += len;
  }

private:
  /*
      makeFileName:

      作用：
      - 根据基础路径生成一个新的日志文件名

      生成规则：
      - 保留原来的目录
      - 保留原来的文件名主体
      - 在文件名后面追加时间戳和编号
      - 保留原来的扩展名

      例子：
      - base_file_path_ = ./log/app.log
      - 新文件名可能是 ./log/app_20260409_173015_0.log
  */
  std::string makeFileName() const {
    namespace fs = std::filesystem;

    fs::path base_path(base_file_path_);

    auto now = std::chrono::system_clock::now();
    std::time_t current_time = std::chrono::system_clock::to_time_t(now);

    std::tm tm_time{};
#ifdef _WIN32
    localtime_s(&tm_time, &current_time);
#else
    localtime_r(&current_time, &tm_time);
#endif

    std::ostringstream name_builder;
    name_builder << base_path.stem().string() << '_'
                 << std::put_time(&tm_time, "%Y%m%d_%H%M%S") << '_'
                 << file_index_ << base_path.extension().string();

    return (base_path.parent_path() / name_builder.str()).string();
  }

  /*
      openNewFile:

      作用：
      - 关闭旧文件
      - 生成新文件名
      - 打开新文件
      - 把 current_file_size_ 归零

      为什么需要 file_index_：
      - 如果同一秒内连续切了多个文件，只靠时间戳可能重名
      - 所以再追加一个递增编号
  */
  void openNewFile() {
    if (ofs_.is_open()) {
      ofs_.close();
    }

    const std::string new_file_name = makeFileName();
    ofs_.open(new_file_name, std::ios::out | std::ios::binary);
    if (!ofs_.is_open()) {
      throw std::runtime_error("日志文件打开失败: " + new_file_name);
    }

    current_file_size_ = 0;
    ++file_index_;
  }

private:
  // base_file_path_ 表示日志文件的基础路径。
  // 它不是当前真正打开的文件名，而是生成新文件名时使用的基础路径。
  std::string base_file_path_;

  // max_file_size_ 表示单个日志文件允许写入的最大字节数。
  size_t max_file_size_ = 0;

  // current_file_size_ 表示当前这个文件已经写入了多少字节。
  size_t current_file_size_ = 0;

  // file_index_ 用来区分同一秒内生成的多个日志文件。
  size_t file_index_ = 0;

  // ofs_ 表示当前真正处于打开状态的日志文件。
  std::ofstream ofs_;
};
