#pragma once

#include <string>

/*
    FileState 表示客户端的一条单文件同步状态。

    它和服务端的 FileMeta 是对称关系：FileMeta 负责描述“服务端保存的这个文件是什
    么状态”，而 FileState 负责描述“客户端记住的这个本地文件目前是什么状态”。
    当前第一版先只记录 etag，后面如果需要补重试次数、最后同步时间、同步结果等信
    息，都可以继续往这个结构里扩展。
*/
struct FileState {
  // etag_ 保存这个文件上一次被客户端记录下来的轻量级状态标识。
  std::string etag_;
};
