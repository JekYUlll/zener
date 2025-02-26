#include "http/conn.h"
#include "utils/log/logger.h"

#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/uio.h>
#include <unistd.h>

namespace zener::http {

const char* Conn::staticDir;
std::atomic<int> Conn::userCount;
bool Conn::isET;

Conn::Conn()
    : _fd(-1), _addr({0}), _connId(0), _isClose(true), _iovCnt(-1), _iov{} {
    // 将 _iovCnt 初始化为-1 不知道是否合适
}

Conn::~Conn() { Close(); }

void Conn::init(int fd, const sockaddr_in& addr) {
    assert(fd > 0);
    ++userCount;
    _addr = addr;
    _fd = fd;
    // 连接ID由Server设置，这里不初始化
    _writeBuff.RetrieveAll();
    _readBuff.RetrieveAll();
    _isClose = false;
    LOG_I("Client {0} [{1}:{2}] in, userCount: {3}", _fd, GetIP(), GetPort(),
          (int)userCount);
}

void Conn::Close() {
    _response.UnmapFile();
    if (!_isClose) {
        _isClose = true;
        --userCount;
        // 确保只关闭有效的文件描述符
        if (_fd > 0) {
            close(_fd);
            LOG_I("Client {0} [{1}:{2}] (connId={3}) quit, userCount: {4}", _fd,
                  GetIP(), GetPort(), _connId, (int)userCount);
        } else {
            LOG_W("Client with invalid fd={} (connId={}) quit, userCount: {}",
                  _fd, _connId, (int)userCount);
        }
        // 关闭后将fd设为-1，防止重复关闭
        _fd = -1;
    }
}

ssize_t Conn::read(int* saveErrno) {
    ssize_t len = -1;
    ssize_t totalLen = 0;
    int maxIterations = 8; // 限制最大读取次数，防止一直读取导致其他连接饥饿
    int iterations = 0;

    do {
        len = _readBuff.ReadFd(_fd, saveErrno);
        if (len > 0) {
            totalLen += len;
            iterations++;
            // 如果读取了足够多的数据，可以考虑在下一个事件循环中继续处理
            if (totalLen > 65536) { // 64KB
                break;
            }
        } else if (len == 0) {
            // 连接关闭
            *saveErrno = ECONNRESET;
            break;
        } else {
            // len < 0, 出错情况
            if (*saveErrno == EAGAIN || *saveErrno == EWOULDBLOCK) {
                // 非阻塞模式下，所有数据已读完
                break;
            }
            return len;
        }
    } while (isET &&
             (iterations <
              maxIterations)); // ET模式需要循环读取，但添加最大迭代次数限制

    return totalLen > 0 ? totalLen : len;
}

ssize_t Conn::write(int* saveErrno) {
    LOG_D("Writing connection fd={}, connId={}: starting to send data, "
          "remaining bytes to write={}",
          _fd, _connId, _iov[0].iov_len + _iov[1].iov_len);
    ssize_t ret = 0;
    ssize_t totalWritten = 0;

    if (ToWriteBytes() == 0) {
        return totalWritten;
    }

    LOG_D("Writing connection fd={}, connId={}: starting to send, iovCnt={}",
          _fd, _connId, _iovCnt);

    // 调试输出，查看iov结构
    for (int i = 0; i < _iovCnt; ++i) {
        LOG_D("Writing connection fd={}, connId={}: iov[{}] - base={:p}, "
              "length={}",
              _fd, _connId, i, _iov[i].iov_base, _iov[i].iov_len);
    }

    // 最多循环写两次
    constexpr int MAX_ATTEMPTS = 2;
    for (int attempt = 0; attempt < MAX_ATTEMPTS; ++attempt) {
        ret = writev(_fd, _iov, _iovCnt);

        if (ret < 0) {
            // 如果是EAGAIN，在ET模式下应继续尝试
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                *saveErrno = errno;
                LOG_D("Writing connection fd={}, connId={}: temporarily unable "
                      "to write (EAGAIN), "
                      "already written {} bytes",
                      _fd, _connId, totalWritten);
                return totalWritten;
            }

            // 其他错误则直接返回
            *saveErrno = errno;
            LOG_E("Writing connection fd={}, connId={}: write error, errno={}, "
                  "error message={}",
                  _fd, _connId, errno, strerror(errno));
            return -1;
        }

        // 累计已写入的数据量
        totalWritten += ret;
        LOG_D("Writing connection fd={}, connId={}: sent {} bytes in one go, "
              "total sent {} bytes",
              _fd, _connId, ret, totalWritten);

        // 更新iov结构以反映已写入的数据
        if (static_cast<size_t>(ret) > _iov[0].iov_len) {
            // 头部数据全部发送完毕，开始发送文件数据
            LOG_D("Writing connection fd={}, connId={}: header data sent, "
                  "starting to process file data",
                  _fd, _connId);

            // 更新文件数据指针和长度
            size_t fileWritten = ret - _iov[0].iov_len;
            _iov[1].iov_base =
                static_cast<char*>(_iov[1].iov_base) + fileWritten;
            _iov[1].iov_len -= fileWritten;

            // 头部数据已全部发送，清空对应缓冲区
            _writeBuff.RetrieveAll();
            _iov[0].iov_base = nullptr;
            _iov[0].iov_len = 0;
        } else {
            // 头部数据未发送完毕
            LOG_D("Writing connection fd={}, connId={}: header data not fully "
                  "sent",
                  _fd, _connId);

            // 更新头部数据指针和长度
            _iov[0].iov_base = static_cast<char*>(_iov[0].iov_base) + ret;
            _iov[0].iov_len -= ret;
            _writeBuff.Retrieve(ret);
        }

        // 如果没有数据需要发送了，退出循环
        if (_iov[0].iov_len + _iov[1].iov_len == 0) {
            break;
        }

        // 在非ET模式下，或者当写入超过一定大小时，暂停写入，等待下次EPOLLOUT事件
        // 这样可以防止长时间占用CPU
        constexpr size_t MAX_WRITE_PER_CALL = 4 * 1024 * 1024; // 4MB
        if (!isET || totalWritten > MAX_WRITE_PER_CALL) {
            LOG_D("Writing connection fd={}, connId={}: "
                  "one go write reached limit or non-ET mode, waiting for next "
                  "write event",
                  _fd, _connId);
            break;
        }
    }

    LOG_D("Writing connection fd={}, connId={}: remaining bytes to write after "
          "sending={}",
          _fd, _connId, _iov[0].iov_len + _iov[1].iov_len);
    return totalWritten;
}

bool Conn::process() {
    // 1. 如果读缓冲区为空，直接返回false，不进行后续处理
    if (_readBuff.ReadableBytes() <= 0) {
        // 降级为调试信息，这是正常情况，不需要作为警告输出
        LOG_D("处理连接 fd={}, connId={}: 读缓冲区为空", _fd, _connId);
        return false;
    }

    _request.Init();
    LOG_D("处理连接 fd={}, connId={}: 初始化请求", _fd, _connId);

    // 2. 解析HTTP请求
    bool parseSuccess = _request.parse(_readBuff);

    // 在处理响应前确保清除之前可能存在的文件映射
    _response.UnmapFile();

    if (parseSuccess) {
        LOG_D("处理连接 fd={}, connId={}: 解析成功, 请求路径={}", _fd, _connId,
              _request.path());
        _response.Init(staticDir, _request.path(), _request.IsKeepAlive(), 200);
    } else {
        LOG_W("处理连接 fd={}, connId={}: 解析失败, 请求路径={}", _fd, _connId,
              _request.path().c_str());
        _response.Init(staticDir, _request.path(), false, 400);
    }

    LOG_D("处理连接 fd={}, connId={}: 准备生成响应", _fd, _connId);

    // 3. 生成HTTP响应
    try {
        _response.MakeResponse(_writeBuff);
    } catch (const std::exception& e) {
        LOG_E("生成响应时发生异常: {}, fd={}, connId={}", e.what(), _fd,
              _connId);
        return false;
    }

    // 4. 如果写缓冲区为空，可能是错误情况，返回false
    if (_writeBuff.ReadableBytes() == 0) {
        LOG_D("处理连接 fd={}, connId={}: 写缓冲区为空, 无响应数据", _fd,
              _connId);
        return false;
    }

    // 5. 设置响应头
    LOG_D("处理连接 fd={}, connId={}: 设置响应头缓冲区, 写缓冲区可读字节={}",
          _fd, _connId, _writeBuff.ReadableBytes());

    // 设置iov以便后续的writev调用
    _iovCnt = 0;
    if (_writeBuff.ReadableBytes() > 0) {
        _iov[0].iov_base = _writeBuff.Peek();
        _iov[0].iov_len = _writeBuff.ReadableBytes();
        _iovCnt = 1;

        LOG_D("处理连接 fd={}, connId={}: iov[0]设置 - 基址={:p}, 长度={}", _fd,
              _connId, _iov[0].iov_base, _iov[0].iov_len);
    }

    // 6. 设置文件响应（如果有）
    if (_response.File() && _response.FileLen() > 0) {
        _iov[1].iov_base = _response.File();
        _iov[1].iov_len = _response.FileLen();
        _iovCnt = 2;

        LOG_D("处理连接 fd={}, connId={}: iov[1]设置 - 基址={:p}, 长度={}", _fd,
              _connId, _iov[1].iov_base, _iov[1].iov_len);
    }

    LOG_D("处理连接 fd={}, connId={}: 完成请求处理，总响应大小={}字节", _fd,
          _connId,
          _writeBuff.ReadableBytes() + (_iovCnt == 2 ? _iov[1].iov_len : 0));

    return true;
}

} // namespace zener::http
