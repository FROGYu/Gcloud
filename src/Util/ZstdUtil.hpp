#pragma once

#include "Logger/LogMacros.hpp"

#include <zstd.h>

#include <string>

/*
    ZstdUtil 是当前项目的 zstd 压缩工具层。

    它负责把 zstd 提供的 C 风格压缩接口，封装成项目里统一使用的 C++ 风格函数。
    后面的深度存储、下载前解压等逻辑，只需要和 std::string 打交道，不需要直接接
    触 zstd 的底层细节。
*/
namespace ZstdUtil {

/*
    这里把 input 里的原始数据压缩到 output。压缩级别越高，通常压缩率越好，但耗时
    也会更长。
*/
inline bool Compress(const std::string& input, std::string* output, int level = 1) {
  if (output == nullptr) {
    return false;
  }

  // ZSTD_compressBound 会给出“最坏情况下压缩后最多需要多大空间”。
  const size_t bound = ZSTD_compressBound(input.size());
  output->resize(bound);

  // ZSTD_compress 把原始数据压缩到目标缓冲区，返回真正写入了多少字节。
  const size_t compressed_size =
      ZSTD_compress(output->data(), output->size(), input.data(), input.size(), level);

  // ZSTD_isError 用来判断 zstd 返回值是不是一个错误码。
  if (ZSTD_isError(compressed_size)) {
    LOG_ERROR("zstd 压缩失败, reason=%s", ZSTD_getErrorName(compressed_size));
    output->clear();
    return false;
  }

  output->resize(compressed_size);
  return true;
}

/*
    这里把 input 里的 zstd 压缩数据解压到 output。
*/
inline bool Decompress(const std::string& input, std::string* output) {
  if (output == nullptr) {
    return false;
  }

  // frameContentSize 会尝试从压缩帧头里读出“原始数据应该有多大”。
  const unsigned long long content_size = ZSTD_getFrameContentSize(input.data(), input.size());
  if (content_size == ZSTD_CONTENTSIZE_ERROR) {
    LOG_ERROR("zstd 解压失败, 输入数据不是有效的 zstd 压缩帧");
    output->clear();
    return false;
  }

  if (content_size == ZSTD_CONTENTSIZE_UNKNOWN) {
    LOG_ERROR("zstd 解压失败, 压缩帧未记录原始大小");
    output->clear();
    return false;
  }

  output->resize(static_cast<size_t>(content_size));

  // ZSTD_decompress 会把压缩数据完整解压到目标缓冲区里。
  const size_t decompressed_size =
      ZSTD_decompress(output->data(), output->size(), input.data(), input.size());
  if (ZSTD_isError(decompressed_size)) {
    LOG_ERROR("zstd 解压失败, reason=%s", ZSTD_getErrorName(decompressed_size));
    output->clear();
    return false;
  }

  output->resize(decompressed_size);
  return true;
}

}  // namespace ZstdUtil
