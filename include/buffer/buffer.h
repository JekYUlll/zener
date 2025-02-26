#ifndef ZENER_BUFFER_H
#define ZENER_BUFFER_H

// 应用层缓冲区 把系统层面的socket缓冲区数据拷贝出来待以后使用
// 知乎的图：
/*
+-------------------+------------------+------------------+------------------+
| prependable bytes |   readed bytes   |  readable bytes  |  writable bytes  |
+-------------------+------------------+------------------+------------------+
|                   |                  |                  |                  |
0        <=       prePos      <=    readPos     <=     writerPos    <=     size

// 实际上似乎没必要抽象出 prependable bytes 和
prePos？读过的地方直接就能覆盖？直接用 readPos 当做 prePos 就行了？
// 11 版本里的实现并没有 prePos

*/

#include "common.h"

#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <string>
#include <strings.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#include <vector>

namespace zener {

static constexpr size_t INIT_BUFFER_SIZE = 1024;
// static constexpr size_t INIT_PREPEND_SIZE = 8;

class Buffer {
  public:
    explicit Buffer(size_t size = INIT_BUFFER_SIZE);
    ~Buffer() = default;
    Buffer(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(const Buffer&) = delete;
    Buffer& operator=(Buffer&& other) noexcept;

    // 可写的字节数
    _ZENER_SHORT_FUNC size_t WritableBytes() const {
        return _buffer.size() - _writePos;
    }
    // 未读的字节数
    _ZENER_SHORT_FUNC size_t ReadableBytes() const {
        return _writePos - _readPos;
    }
    // 已读的字节数
    // _ZENER_SHORT_FUNC size_t ReadedBytes() const { return _readPos - _prePos;
    // }

    // 可以用于前插的字节数
    _ZENER_SHORT_FUNC std::size_t PrependableBytes() const { return _readPos; }

    // _ZENER_SHORT_FUNC char* GetPrePtr() { return beginPtr() + _prePos; }
    // _ZENER_SHORT_FUNC const char* GetPrePtr() const {
    //     return beginPtr() + _prePos;
    // }

    _ZENER_SHORT_FUNC char* GetWritePtr() { return beginPtr() + _writePos; }
    _ZENER_SHORT_FUNC const char* GetWritePtr() const {
        return beginPtr() + _writePos;
    }

    // GetReadPtr
    _ZENER_SHORT_FUNC char* Peek() { return beginPtr() + _readPos; }
    _ZENER_SHORT_FUNC const char* Peek() const { return beginPtr() + _readPos; }

    _ZENER_SHORT_FUNC char* BeginWrite() { return beginPtr() + _writePos; }
    _ZENER_SHORT_FUNC const char* BeginWrite() const {
        return beginPtr() + _writePos;
    }

    void Retrieve(std::size_t len);
    void RetrieveUntil(const char* end);
    // Clear
    void RetrieveAll();

    std::string RetrieveAllToString();
    [[nodiscard]] std::string ToString() const;

    // 更新写位置，使用原子操作确保线程安全
    inline void HasWritten(size_t len) {
        size_t newPos = _writePos.load(std::memory_order_acquire) + len;
        _writePos.store(newPos, std::memory_order_release);
    }

    void Append(const std::string& str);
    void Append(const void* data, size_t len);
    void Append(const char* str, size_t len);
    void Append(const Buffer& buff);

    void EnsureWritable(size_t len);

    ssize_t ReadFd(int fd, int* saveErrno);
    ssize_t WriteFd(int fd, int* saveErrno);

  private:
    _ZENER_SHORT_FUNC char* beginPtr() { return &*_buffer.begin(); }
    _ZENER_SHORT_FUNC const char* beginPtr() const { return &*_buffer.begin(); }

    void makeSpace(size_t len);

    std::vector<char> _buffer;

    // std::atomic<size_t> _prePos; // 预置数据的末尾
    std::atomic<size_t> _readPos;
    std::atomic<size_t> _writePos;
};

} // namespace zener

#endif // !ZENER_BUFFER_H