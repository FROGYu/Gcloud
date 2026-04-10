#pragma once

#include <unistd.h>

/*
    UniqueFd 是一个 fd 管理工具。

    它负责：
    - 保存一个文件描述符
    - 在对象析构时自动 close
*/
class UniqueFd {
public:
  UniqueFd() = default;

  explicit UniqueFd(int fd) : fd_(fd) {}

  ~UniqueFd() { reset(); }

  UniqueFd(const UniqueFd &) = delete;
  UniqueFd &operator=(const UniqueFd &) = delete;

  UniqueFd(UniqueFd &&other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

  UniqueFd &operator=(UniqueFd &&other) noexcept {
    if (this != &other) {
      reset();
      fd_ = other.fd_;
      other.fd_ = -1;
    }
    return *this;
  }

  /*
      get:

      返回：
      - 当前保存的 fd 数字
  */
  int get() const { return fd_; }

  /*
      valid:

      返回：
      - true:  当前保存的是有效 fd
      - false: 当前没有保存有效 fd
  */
  bool valid() const { return fd_ >= 0; }

  /*
      release:

      作用：
      - 把内部保存的 fd 交出去
      - 交出去以后，当前对象不再负责 close 它

      返回：
      - 原来的 fd 数字
  */
  int release() {
    int old_fd = fd_;
    fd_ = -1;
    return old_fd;
  }

  /*
      reset:

      参数：
      - new_fd: 新的 fd，默认是 -1

      作用：
      - 如果当前对象里已经有 fd，就先 close
      - 再把内部值改成 new_fd
  */
  void reset(int new_fd = -1) {
    if (fd_ >= 0) {
      ::close(fd_);
    }
    fd_ = new_fd;
  }

private:
  int fd_ = -1;
};
