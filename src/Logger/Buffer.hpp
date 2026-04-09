#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

/*
    Buffer 所在层级：日志系统的“内存缓冲层”。

    设计方法：
    1. 用连续内存（std::vector<char>）保存日志字节流。
       如果每次写日志都重新分配内存，会产生大量小分配，分配器开销会变成瓶颈。
       连续内存 + 复用容量，可以把“频繁分配”的成本压到更低。
    2. 用 write_pos_ / read_pos_ 两个索引描述“已写入”和“可读取”的范围。
       这样读写只是在移动索引，不需要移动整个内存块。
    3. 提供 Push / ReadPtr / ReadableSize 三个接口。
       Push 的作用是：业务线程把日志先写进内存。
       ReadPtr 和 ReadableSize 的作用是：后台线程知道“当前这批日志在内存里的起点在哪里、一共有多长”，
       这样后台线程才能把这一整段日志从内存取出来，再写到文件里。
       这里把“写进内存”和“写进文件”分开，是因为内存快、文件慢。
       业务线程先把日志放进内存，可以减少它被慢速文件写入拖住的时间。
    4. 提供 Swap，支持双缓冲快速交换（业务线程写入的一块缓冲区 / 后台线程处理的一块缓冲区）。
       如果只有一块缓冲区，业务线程和后台线程会同时争这块内存。
       有两块缓冲区时，业务线程可以继续往一块里写，后台线程去处理另一块，
       这样两边不会总是互相等待。
    5. 自动扩容，保证写入不会失败。

    供谁使用：
    - 业务线程：调用 Push 写入日志
    - 后台线程：调用 ReadPtr + ReadableSize 读取日志
    - AsyncWorker：调用 Swap 交换双缓冲
*/
class Buffer {
public:
  /*
      构造函数

      参数：
      - initial_size: 初始缓冲区大小（字节数）

      说明：
      - explicit 用来禁止“整数 -> Buffer”的隐式转换
      - 默认初始大小为 1024 字节
  */
  explicit Buffer(size_t initial_size = 1024)
      : data_(initial_size), write_pos_(0), read_pos_(0) {}

  /*
      Push: 把数据追加写入缓冲区。

      参数：
      - data: 一段已经整理好的日志内容
      - len:  这段日志内容占多少字节

      行为：
      - len 为 0 时直接返回
      - 空间不足时确保容量足够，使用ensureWritable函数自动扩容
      - 写入后把 write_pos_ 向后移动，指向下一个可写位置
  */
  void Push(const char *data, size_t len) {
    if (len == 0) {
      return;
    }
    ensureWritable(len);
    std::memcpy(data_.data() + write_pos_, data, len);
    write_pos_ += len;
  }

  /*
      ReadPtr: 返回当前可读数据的起始地址。

      为什么需要这个函数：
      - 后台线程处理日志时，不能只知道“有多少数据”，还必须知道“数据从哪里开始”

      返回：
      - 指向 data_ 内部可读区域的指针
  */
  const char *ReadPtr() const { return data_.data() + read_pos_; }

  /*
      ReadableSize: 返回当前可读的数据长度。

      为什么需要这个函数：
      - 后台线程处理日志时，不能只知道起始地址，还必须知道这一段数据有多长

      返回：
      - 当前可读字节数
  */
  size_t ReadableSize() const { return write_pos_ - read_pos_; }

  /*
      WritableSize: 返回剩余可写空间。

      返回：
      - 还能写入的字节数
  */
  size_t WritableSize() const { return data_.size() - write_pos_; }

  /*
      Reset: 清空读写位置，保留容量以便复用。

      使用场景：
      - 后台线程处理完成后，复用缓冲区内存
  */
  void Reset() {
    write_pos_ = 0;
    read_pos_ = 0;
  }

  /*
      Swap: 与另一个 Buffer 交换内部数据和读写位置。

      使用场景：
      - 双缓冲切换：业务线程写入的缓冲区和后台线程处理的缓冲区互换

      为什么要交换：
      - 业务线程写日志时，希望尽快回去继续执行自己的工作
      - 后台线程处理日志时，会比写内存慢很多
      - 直接交换两块 Buffer，比把大块数据复制来复制去更省时间
  */
  void Swap(Buffer &other) {
    data_.swap(other.data_);
    std::swap(write_pos_, other.write_pos_);
    std::swap(read_pos_, other.read_pos_);
  }

private:
  /*
      ensureWritable: 保证缓冲区至少还有 len 字节可写空间。

      参数：
      - len: 需要保证的可写字节数

      行为：
      - 空间不足时按倍数扩容 data_
  */
  void ensureWritable(size_t len) {
    if (WritableSize() >= len) {
      return;
    }
    size_t new_size = data_.size();
    if (new_size == 0) {
      new_size = 1;
    }
    while (new_size - write_pos_ < len) {
      new_size *= 2;
    }
    data_.resize(new_size);
  }

private:
  // data_ 保存实际字节内容，索引操作都基于这块连续内存。
  std::vector<char> data_;
  // write_pos_ 指向下一个可写入的位置。
  size_t write_pos_;
  // read_pos_ 指向当前可读数据的起点。
  size_t read_pos_;
  // 这里处理的是大小和索引，它们不会为负，而标准库对这类量本来就使用 size_t /
  // size_type，所以选择无符号整数类型既符合语义，也避免类型反复转换。
};
