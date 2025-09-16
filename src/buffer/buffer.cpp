#include "buffer/buffer.h"

#include <limits>
#include <stdexcept>
#include <strings.h>

namespace zener {

// 原子类型（如
// std::atomic<T>）的移动构造函数是被删除的，这是为了保证原子操作的线程安全性和语义完整性

Buffer::Buffer(size_t size)
    : _buffer(INIT_BUFFER_SIZE), _readPos(0), _writePos(0) {
    // _prePos(INIT_PREPEND_SIZE)
}

Buffer::Buffer(Buffer &&other) noexcept : _buffer(std::move(other._buffer)) {
    // 使用原子操作来安全地设置值
    _readPos.store(other._readPos.load(std::memory_order_acquire),
                   std::memory_order_release);
    _writePos.store(other._writePos.load(std::memory_order_acquire),
                    std::memory_order_release);

    // 重置other的状态
    other._readPos.store(0, std::memory_order_release);
    other._writePos.store(0, std::memory_order_release);
    // 注意：不要对已移动的other._buffer进行bzero操作，因为它现在是一个空vector
}

Buffer &Buffer::operator=(Buffer &&other) noexcept {
    if (this != &other) {
        _buffer = std::move(other._buffer);
        _readPos.store(other._readPos.load(std::memory_order_acquire),
                       std::memory_order_release);
        _writePos.store(other._writePos.load(std::memory_order_acquire),
                        std::memory_order_release);

        // 重置other的状态
        other._readPos.store(0, std::memory_order_release);
        other._writePos.store(0, std::memory_order_release);
        // 注意：不要对已移动的other._buffer进行bzero操作
    }
    return *this;
}

void Buffer::Retrieve(std::size_t len) {
    assert(len <= ReadableBytes());
    size_t newPos = _readPos.load(std::memory_order_acquire) + len;
    _readPos.store(newPos, std::memory_order_release);
}

void Buffer::RetrieveUntil(const char *end) {
    assert(Peek() <= end);
    Retrieve(end - Peek());
}

// Clear
void Buffer::RetrieveAll() {
    if (!_buffer.empty()) {
        bzero(&_buffer[0], _buffer.size());
    }
    _readPos.store(0, std::memory_order_release);
    _writePos.store(0, std::memory_order_release);
}

std::string Buffer::RetrieveAllToString() {
    std::string str(Peek(), ReadableBytes());
    RetrieveAll();
    return str;
}

std::string Buffer::ToString() const {
    std::string str(Peek(), ReadableBytes());
    return str;
}

void Buffer::Append(const std::string &str) {
    Append(str.data(), str.length());
}

void Buffer::Append(const void *data, size_t len) {
    assert(data);
    Append(static_cast<const char *>(data), len);
}

void Buffer::Append(const char *str, size_t len) {
    assert(str);
    EnsureWritable(len);
    std::copy_n(str, len, BeginWrite());
    HasWritten(len);
}

void Buffer::Append(const Buffer &buff) {
    Append(buff.Peek(), buff.ReadableBytes());
}

void Buffer::EnsureWritable(const size_t len) {
    if (WritableBytes() < len) {
        makeSpace(len);
    }
    assert(WritableBytes() >= len);
}

ssize_t Buffer::ReadFd(const int fd, int *saveErrno) {
    /*
        在非阻塞网络编程中，如何设计并使用缓冲区？
    1.
    一方面我们希望减少系统调用，一次读的数据越多越划算，那么似乎应该准备一个大的缓冲区。
    2.
    另一方面，我们系统减少内存占用。如果有10k个连接，每个连接一建立就分配64k的读缓冲的话，将占用640M内存，而大多数时候这些缓冲区的使用率很低。
    3.
    因此读取数据的时候要尽可能读完，为了不扩大`buffer`，可以通过`extraBuf`来做第二层缓冲
    4. muduo
    的解决方案：将读取分散在两块区域，一块是`buffer`，一块是栈上的`extraBuf`（放置在栈上使得`extraBuf`随着`Read`的结束而释放，不会占用额外空间），当初始的小`buffer`放不下时才扩容
    */

    // 使用更小的栈上缓冲区，减小栈上内存使用
    constexpr size_t EXTRA_BUF_SIZE = 4096; // 减小到4KB以降低栈内存使用
    char extraBuff[EXTRA_BUF_SIZE];

    // struct iovec 用于进行分散 - 聚集 I/O（Scatter - Gather I/O）操作。
    // 允许程序在一次系统调用中从多个缓冲区读取数据（聚集）或向多个缓冲区写入数据（分散）
    struct iovec iov[2];

    // 检查 Buffer 是否有可写空间，如果不足则进行适当扩容
    if (const size_t writable = WritableBytes();
        writable < 1024) { // 如果可写空间小于1KB
        // 扩容至少4KB空间，但不超过已用空间的两倍
        const size_t newSpace = std::max<size_t>(4096, ReadableBytes());
        try {
            EnsureWritable(newSpace);
        } catch (const std::exception &e) {
            if (saveErrno) {
                *saveErrno = ENOMEM;
            }
            LOG_E("Buffer::ReadFd - failed: {}", e.what());
            return -1;
        }
    }

    // 重新获取可写空间
    const size_t updatedWritable = WritableBytes();

    // 分散读，保证数据读完
    iov[0].iov_base = GetWritePtr(); // 第一块指向 _buffer 里的 write_pos
    iov[0].iov_len = updatedWritable;
    iov[1].iov_base = extraBuff; // 第二块指向栈上的 extraBuff
    iov[1].iov_len = EXTRA_BUF_SIZE;

    // 判断需要写入几个缓冲区
    const int iovCnt = (updatedWritable < EXTRA_BUF_SIZE) ? 2 : 1;
    const ssize_t len = readv(fd, iov, iovCnt);

    if (len < 0) {
        if (saveErrno) {
            *saveErrno = errno;
        }
    } else if (len == 0) {
        // 连接已关闭，不做任何处理
    } else if (static_cast<size_t>(len) <= updatedWritable) {
        // 数据完全写入第一个缓冲区，使用原子操作更新写位置
        const size_t newPos = _writePos.load(std::memory_order_acquire) + len;
        _writePos.store(newPos, std::memory_order_release);
    } else {
        // 数据部分写入第一个缓冲区，部分写入第二个缓冲区
        // 先将第一个缓冲区填满，使用原子操作
        _writePos.store(_buffer.size(), std::memory_order_release);

        // 计算写入第二个缓冲区的数据量
        size_t extraLen = len - updatedWritable;

        // 确保不超出 extraBuff 的大小
        if (extraLen > EXTRA_BUF_SIZE) {
            LOG_W("Buffer::ReadFd - extraBuff溢出，截断数据");
            extraLen = EXTRA_BUF_SIZE;
        }

        // 将第二个缓冲区的数据追加到 Buffer 中
        try {
            Append(extraBuff, extraLen);
        } catch (const std::exception &e) {
            // 如果追加数据时发生异常，记录错误但不中断处理
            LOG_E("Buffer::ReadFd - 追加extraBuff数据失败: {}", e.what());
            if (saveErrno) {
                *saveErrno = ENOBUFS; // 缓冲区空间不足
            }
        }
    }

    return len;
}

ssize_t Buffer::WriteFd(const int fd, int *saveErrno) {
    const size_t readSize = ReadableBytes();
    const ssize_t len = write(fd, Peek(), readSize);
    if (len < 0) {
        if (saveErrno) {
            *saveErrno = errno;
        }
        return len;
    }

    // 更新读位置，使用原子操作
    const size_t newPos = _readPos.load(std::memory_order_acquire) + len;
    _readPos.store(newPos, std::memory_order_release);

    return len;
}

void Buffer::makeSpace(const size_t len) {
    /*
    写入空间不够处理方案：
    1.将readable bytes往前移动：因为每次读取数据，readed
    bytes都会逐渐增大。我们可以将readed bytes直接抛弃，把后面的readable
    bytes移动到前面prePos处。
    2.如果第一种方案的空间仍然不够，那么就直接对buffer扩容
    */
    try {
        // 获取当前位置的原子快照，确保线程安全
        size_t currentReadPos = _readPos.load(std::memory_order_acquire);
        size_t currentWritePos = _writePos.load(std::memory_order_acquire);

        if (WritableBytes() + PrependableBytes() < len) {
            // 检查整数溢出
            if (currentWritePos >
                std::numeric_limits<size_t>::max() - len - 1) {
                throw std::overflow_error("Buffer overflow");
            }
            _buffer.resize(currentWritePos + len + 1);
        } else {
            // 将已读数据丢弃，将未读数据前移
            const size_t readable = ReadableBytes();
            std::copy(beginPtr() + currentReadPos, beginPtr() + currentWritePos,
                      beginPtr());

            // 更新位置指针，使用原子操作
            _readPos.store(0, std::memory_order_release);
            _writePos.store(readable, std::memory_order_release);

            assert(readable == ReadableBytes());
        }
    } catch (const std::bad_alloc &e) {
        // 处理内存分配失败
        LOG_E("Buffer::makeSpace - 内存分配失败: {}", e.what());
        throw; // 重新抛出异常，让调用者处理
    } catch (const std::exception &e) {
        // 处理其他异常
        LOG_E("Buffer::makeSpace - 异常: {}", e.what());
        throw; // 重新抛出异常，让调用者处理
    }
}

} // namespace zener
