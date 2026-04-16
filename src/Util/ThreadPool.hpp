#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

/*
    ThreadPool 是当前项目的后台任务线程池。

    它负责提前创建一组工作线程，让外部可以把“以后再做”的任务塞进队列里。工作线程
    会在后台循环取任务执行，这样主线程就不需要自己同步把所有重活都做完。
*/
class ThreadPool {
 public:
  using Task = std::function<void()>;

  /*
      这里创建一组后台工作线程。thread_count 为 0 时，兜底创建 1 个线程，避免线程池
      处于“存在但没人干活”的无效状态。
  */
  explicit ThreadPool(size_t thread_count = std::thread::hardware_concurrency()) {
    if (thread_count == 0) {
      thread_count = 1;
    }

    for (size_t i = 0; i < thread_count; ++i) {
      workers_.emplace_back([this] { WorkerLoop(); });
    }
  }

  ~ThreadPool() { Stop(); }

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  /*
      这里向任务队列里追加一个后台任务。任务入队成功后，会唤醒一个正在等待的工作线
      程去处理它。
  */
  bool Enqueue(Task task) {
    if (!task) {
      return false;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stop_) {
        return false;
      }

      tasks_.push(std::move(task));
    }

    task_ready_cv_.notify_one();
    return true;
  }

  /*
      这里通知线程池停止接新任务，并等待已经创建出来的工作线程全部退出。重复调用也
      是安全的。
  */
  void Stop() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stop_) {
        return;
      }

      stop_ = true;
    }

    task_ready_cv_.notify_all();

    for (std::thread& worker : workers_) {
      if (worker.joinable()) {
        worker.join();
      }
    }
  }

 private:
  /*
      工作线程会一直循环等待：要么任务队列里来了新任务，要么线程池收到了停止信号。
      收到任务就执行；如果已经要求停止并且队列也空了，就安全退出。
  */
  void WorkerLoop() {
    while (true) {
      Task task;

      {
        std::unique_lock<std::mutex> lock(mutex_);
        // 工作线程平时睡在这里，只有“来了新任务”或者“线程池准备停机”时才会被唤醒。
        task_ready_cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });

        // 停机后如果任务队列也已经空了，说明当前工作线程可以安全退出了。
        if (stop_ && tasks_.empty()) {
          return;
        }

        // 这里把队头任务取出来，接下来会在锁外执行，避免长时间占着队列锁。
        task = std::move(tasks_.front());
        tasks_.pop();
      }

      // 真正执行任务的地方在线程池锁外，避免任务本身阻塞其他线程取任务。
      task();
    }
  }

 private:
  // workers_ 保存线程池里的所有后台工作线程。
  std::vector<std::thread> workers_;

  // tasks_ 保存等待后台执行的任务队列。
  std::queue<Task> tasks_;

  // mutex_ 保护任务队列和 stop_ 状态。
  std::mutex mutex_;

  // task_ready_cv_ 用来在“有新任务”或“要求停机”时唤醒工作线程。
  std::condition_variable task_ready_cv_;

  // stop_ 表示线程池已经进入停机状态，不再接收新任务。
  bool stop_ = false;
};
