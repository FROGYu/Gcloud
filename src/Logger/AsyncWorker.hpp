#pragma once

#include "Buffer.hpp"

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

/*
    AsyncWorker 所在层级：日志系统的“异步处理层”。

    设计思路：
    1. 业务线程只负责把日志写进内存，不直接碰文件。
       因为写内存很快，写文件通常更慢。
    2. 专门启动一个后台线程。
       这个线程不参与业务逻辑，只负责把内存中的日志交给回调函数处理。
    3. 使用两块 Buffer：
       - front_buffer_：业务线程正在写入的缓冲区
       - back_buffer_：后台线程正在处理的缓冲区
    4. 当业务线程写入了一批日志后，后台线程把 front_buffer_ 和 back_buffer_
   交换。 交换完成后：
       - 业务线程继续往新的 front_buffer_ 写
       - 后台线程拿着新的 back_buffer_ 去处理刚刚那批日志
    5. 这样做的目的，是让“继续写日志”和“处理上一批日志”尽量并行进行。

    供谁使用：
    - AsyncLogger：调用 Push 把格式化后的日志送进来
    - 后台线程：调用回调函数处理 back_buffer_ 中的数据
*/
class AsyncWorker {
public:
  using ConsumeCallback = std::function<void(const char *, size_t)>;

  /*
      构造函数

      参数：
      - callback: 后台线程拿到一批日志后，要如何处理这批日志

      行为：
      - 拿到回调函数
      - 创建并且启动一个后台线程,并且让它从当前这个对象的 Run() 开始执行。
  */
  explicit AsyncWorker(ConsumeCallback callback)
      : callback_(std::move(callback)), stop_(false), running_(true),
        worker_(&AsyncWorker::Run, this) {}

  /*
      析构函数
         通知停止、等待清空、确认退出、最后回收线程。
      行为：
      - 通知后台线程停止
      - 等待后台线程把剩余日志处理完并退出
  */
  ~AsyncWorker() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_ = true; // 让后台线程准备停止，不要无限循环了
    }
    data_ready_cv_.notify_one();
    /*因为后台线程可能正睡着。
      如果不把它叫醒，它根本不知道前台已经说停工了。*/

    {
      // 主线程在这里等，直到后台线程把 running_ 变成 false。说明日志全写完了
      std::unique_lock<std::mutex> lock(mutex_);
      worker_done_cv_.wait(lock, [this] { return !running_; });
    }

    if (worker_.joinable()) {
      worker_.join();
    }
  }

  /*
      Push:

      供谁调用：
      - 前台 AsyncLogger

      参数：
      - data: 一段已经格式化好的日志内容
      - len:  这段日志内容的字节数

      行为：
      - 加锁后把日志写入 front_buffer_
      - 通知后台线程有新数据可以处理
  */
  void Push(const char *data, size_t len) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      front_buffer_.Push(
          data, len); // 前台线程并没有写文件，它只是把数据写进内存缓冲区
    } // 走出大括号锁释放
    data_ready_cv_.notify_one();
  }

private:
  /*
      Run:

      供谁调用：
      - 构造函数启动的后台线程
      AsyncWorker 出生（构造函数）
        ↓
      前台不断塞日志（Push）
        ↓
      后台线程循环干活（Run）
        ↓
      收工（析构）

      行为：
      1. 等待：直到业务线程写入了日志，或者收到了停止信号
                后台线程拿着锁睡觉，直到满足下面两个条件之一才醒：
                  收到停工命令 stop_ ==true
                  前缓冲区里真的有数据了 front_buffer_.ReadableSize() > 0
      2. 交换：把 front_buffer_ 和 back_buffer_ 交换
      3. 解锁：让业务线程继续往新的 front_buffer_ 写
      4. 读取：后台线程从 back_buffer_ 取出这一批日志内容
      5. 调用：把这批日志内容交给回调函数
      6. 清空：这一批日志内容用完后重置 back_buffer_
  */
  void Run() {
    while (true) {
      // 后台线程在这里不断重复同一件事：
      // 等待日志 -> 拿到一批日志 -> 交出去 -> 再等待。
      {
        // unique_lock 和条件变量配合使用。
        // 后面 wait 时会先放锁，等被唤醒后再重新拿锁。
        std::unique_lock<std::mutex> lock(mutex_);

        // 如果 front_buffer_ 里还没有日志，后台线程就在这里等待。
        // 满足下面任意一个条件，wait 才会结束：
        // 1. stop_ == true，表示外面要求线程结束
        // 2. front_buffer_ 里已经有日志了
        data_ready_cv_.wait(
            lock, [this] { return stop_ || front_buffer_.ReadableSize() > 0; });

        // 只有在“收到结束命令”并且“两块缓冲区都没有剩余日志”时，
        // 后台线程才真正退出。
        if (stop_ && front_buffer_.ReadableSize() == 0 &&
            back_buffer_.ReadableSize() == 0) {
          running_ = false;
          // 通知析构函数：后台线程已经结束，可以继续 join。
          worker_done_cv_.notify_all();
          return;
        }

        // 交换后：
        // - 业务线程会继续往新的 front_buffer_ 写
        // - 后台线程开始读取新的 back_buffer_
        front_buffer_.Swap(back_buffer_);
      } // 锁在这里释放，业务线程可以继续写 front_buffer_

      if (back_buffer_.ReadableSize() > 0) {
        // ReadPtr() 给出这一批日志内容从哪开始读，
        // ReadableSize() 给出这一批日志内容一共有多少字节。
        callback_(
            back_buffer_.ReadPtr(),
            back_buffer_
                .ReadableSize()); // 会走到
                                  // AsyncLogger::consume()，再去遍历输出器列表，调用
                                  // Flush

        // 这一批日志已经交出去了，清空 back_buffer_，等待下一轮复用。
        back_buffer_.Reset();
      }
    }
  }

private:
  // callback_ 表示“这一批日志内容接下来要交给谁”。
  // 后台线程从 back_buffer_ 读出数据后，会调用这个函数。
  ConsumeCallback callback_;

  // front_buffer_ 由业务线程写入。
  Buffer front_buffer_;
  // back_buffer_ 保存一批已经写好的日志内容。
  // front_buffer_ 和它交换后，后台线程从这里读取数据。
  Buffer back_buffer_;

  // mutex_ 保护双缓冲和停止标记。
  std::mutex mutex_;
  // data_ready_cv_ 前台用来通知后台线程：front_buffer_ 里有新日志了。
  std::condition_variable data_ready_cv_;
  // worker_done_cv_ 用来通知析构函数：后台线程已经退出了。
  std::condition_variable worker_done_cv_;

  // stop_ 表示后台线程被要求停止。
  bool stop_;
  // running_ 表示后台线程当前是否仍在运行。
  // true：还在干活
  // false：已经退出了
  bool running_;
  // worker_ 是后台线程对象。
  std::thread worker_;
};
