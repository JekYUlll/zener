#include "buffer/buffer.h"

#include <limits>
#include <stdexcept>
#include <strings.h>

namespace zws {

// 原子类型（如
// std::atomic<T>）的移动构造函数是被删除的，这是为了保证原子操作的线程安全性和语义完整性

Buffer::Buffer(size_t size)
    : _buffer(INIT_BUFFER_SIZE), _readPos(0), _writePos(0) {
    // _prePos(INIT_PREPEND_SIZE)
}

Buffer::Buffer(Buffer&& other) noexcept
    : _buffer(std::move(other._buffer)), _readPos(other._readPos.load()),
      _writePos(other._writePos.load()) {
    // , _prePos(other._prePos.load())
    bzero(&other._buffer[0], other._buffer.size());
    other._readPos = 0;
    other._writePos = 0;
    // other._prePos = 0;
}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        _buffer = std::move(other._buffer);
        bzero(&other._buffer[0], other._buffer.size());
        _readPos = other._readPos.load();
        _writePos = other._writePos.load();
        // _prePos = other._prePos.load();
        other._readPos = 0;
        other._writePos = 0;
        // other._prePos = 0;
    }
    return *this;
}

void Buffer::Retrieve(std::size_t len) {
    assert(len <= ReadableBytes());
    _readPos += len;
}

void Buffer::RetrieveUntil(const char* end) {
    assert(Peek() <= end);
    Retrieve(end - Peek());
}
// Clear
void Buffer::RetrieveAll() {
    bzero(&_buffer[0], _buffer.size());
    _readPos = 0;
    _writePos = 0;
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

void Buffer::Append(const std::string& str) {
    Append(str.data(), str.length());
}

void Buffer::Append(const void* data, size_t len) {
    assert(data);
    Append(static_cast<const char*>(data), len);
}

void Buffer::Append(const char* str, size_t len) {
    assert(str);
    EnsureWritable(len);
    std::copy_n(str, len, BeginWrite());
    HasWritten(len);
}

void Buffer::Append(const Buffer& buff) {
    Append(buff.Peek(), buff.ReadableBytes());
}

void Buffer::EnsureWritable(const size_t len) {
    if (WritableBytes() < len) {
        makeSpace(len);
    }
    assert(WritableBytes() >= len);
}

ssize_t Buffer::ReadFd(const int fd, int* saveErrno) {
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
    char extraBuff[65535];
    // struct iovec 用于进行分散 - 聚集 I/O（Scatter - Gather I/O）操作。
    // 允许程序在一次系统调用中从多个缓冲区读取数据（聚集）或向多个缓冲区写入数据（分散）
    struct iovec iov[2];
    const size_t writable = WritableBytes();
    // 分散读，保证数据读完
    iov[0].iov_base = GetWritePtr(); // 第一块指向 _buffer 里的 write_pos
    iov[0].iov_len = writable;
    iov[1].iov_base = extraBuff; // 第二块指向栈上的 extraBuff
    iov[1].iov_len = sizeof(extraBuff);
    // 判断需要写入几个缓冲区 *
    const int iovCnt = writable < sizeof(extraBuff) ? 2 : 1;
    const ssize_t len = readv(fd, iov, iovCnt);
    if (len < 0) {
        *saveErrno = errno;
    } else if (static_cast<size_t>(len) <= writable) {
        _writePos += len;
    } else {
        _writePos = _buffer.size();
        Append(extraBuff, len - writable);
    }
    return len;
}

ssize_t Buffer::WriteFd(const int fd, int* saveErrno) {
    const size_t readSize = ReadableBytes();
    const ssize_t len = write(fd, Peek(), readSize);
    if (len < 0) {
        *saveErrno = errno;
        return len;
    }
    _readPos += len;
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
    if (WritableBytes() + PrependableBytes() < len) {
        if (_writePos > std::numeric_limits<size_t>::max() - len - 1) {
            throw std::overflow_error("Buffer overflow");
        }
        _buffer.resize(_writePos + len + 1);
    } else {
        const size_t readable = ReadableBytes();
        std::copy(beginPtr() + _readPos, beginPtr() + _writePos, beginPtr());
        _readPos = 0;
        _writePos = _readPos + readable;
        assert(readable == ReadableBytes());
    }
}

} // namespace zws