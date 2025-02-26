#include "http/conn.h"
#include "utils/log/logger.h"

#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
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
          userCount.load());
}

void Conn::Close() {
    _response.UnmapFile();
    if (!_isClose) {
        _isClose = true;
        --userCount;
        close(_fd);
        LOG_I("Client {0} [{1}:{2}] (connId={3}) quit, userCount: {4}", _fd,
              GetIP(), GetPort(), _connId, userCount.load());
    }
}

ssize_t Conn::read(int* saveErrno) {
    ssize_t len = -1;
    do {
        len = _readBuff.ReadFd(_fd, saveErrno);
        if (len <= 0) {
            break;
        }
    } while (isET); // 如果是边缘触发，需要多次读
    return len;
}

ssize_t Conn::write(int* saveErrno) {
    LOG_D("写入连接 fd={}, connId={}: 开始发送数据, 剩余待写字节={}", _fd,
          _connId, ToWriteBytes());
    ssize_t ret = 0;
    ssize_t totalWritten = 0;

    if (ToWriteBytes() == 0) {
        return totalWritten;
    }

    LOG_D("写入连接 fd={}, connId={}: 开始发送, iovCnt={}", _fd, _connId,
          _iovCnt);

    // 打印iov情况
    for (int i = 0; i < _iovCnt; ++i) {
        LOG_D("写入连接 fd={}, connId={}: iov[{}] - 基址={:p}, 长度={}", _fd,
              _connId, i, _iov[i].iov_base, _iov[i].iov_len);
    }

    do {
        ret = writev(_fd, _iov, _iovCnt);

        if (ret < 0) {
            // 如果是EAGAIN，在ET模式下应继续尝试
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                *saveErrno = errno;
                LOG_D("写入连接 fd={}, connId={}: 暂时无法写入 (EAGAIN), "
                      "已写入 {} 字节",
                      _fd, _connId, totalWritten);
                return totalWritten;
            }

            // 其他错误则直接返回
            *saveErrno = errno;
            LOG_E("写入连接 fd={}, connId={}: 写入错误, errno={}, 错误信息={}",
                  _fd, _connId, errno, strerror(errno));
            return -1;
        }

        // 累计已写入的数据量
        totalWritten += ret;
        LOG_D("写入连接 fd={}, connId={}: 单次发送 {} 字节, 累计已发送 {} 字节",
              _fd, _connId, ret, totalWritten);

        // 更新iov结构以反映已写入的数据
        if (static_cast<size_t>(ret) > _iov[0].iov_len) {
            // 头部数据全部发送完毕，开始发送文件数据
            LOG_D(
                "写入连接 fd={}, connId={}: 头部数据发送完毕，开始处理文件数据",
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
            LOG_D("写入连接 fd={}, connId={}: 头部数据未发送完毕", _fd,
                  _connId);

            // 更新头部数据指针和长度
            _iov[0].iov_base = static_cast<char*>(_iov[0].iov_base) + ret;
            _iov[0].iov_len -= ret;
            _writeBuff.Retrieve(ret);
        }

        // 如果没有数据需要发送了，退出循环
        if (ToWriteBytes() == 0) {
            break;
        }

        // 在非ET模式下，或者当写入超过一定大小时，暂停写入，等待下次EPOLLOUT事件
        // 这样可以防止长时间占用CPU
        constexpr size_t MAX_WRITE_PER_CALL = 4 * 1024 * 1024; // 4MB
        if (!isET || totalWritten > MAX_WRITE_PER_CALL) {
            LOG_D("写入连接 fd={}, connId={}: "
                  "单次发送达到上限或非ET模式，等待下次写入事件",
                  _fd, _connId);
            break;
        }
    } while (true);

    LOG_D("写入连接 fd={}, connId={}: 发送后剩余待写字节={}", _fd, _connId,
          ToWriteBytes());
    return totalWritten;
}

bool Conn::process() {
    _request.Init();
    LOG_D("处理连接 fd={}, connId={}: 初始化请求", _fd, _connId);

    if (_readBuff.ReadableBytes() <= 0) {
        LOG_W("处理连接 fd={}, connId={}: 读缓冲区为空", _fd, _connId);
        return false;
    } else if (_request.parse(_readBuff)) {
        LOG_D("{}", _request.path().c_str());
        LOG_D("处理连接 fd={}, connId={}: 解析成功，请求路径={}", _fd, _connId,
              _request.path().c_str());

        // 在处理响应前确保清除之前可能存在的文件映射
        _response.UnmapFile();
        _response.Init(staticDir, _request.path(), _request.IsKeepAlive(), 200);
    } else {
        LOG_W("处理连接 fd={}, connId={}: 解析失败，请求路径={}", _fd, _connId,
              _request.path().c_str());
        _response.Init(staticDir, _request.path(), false, 400);
    }

    LOG_D("处理连接 fd={}, connId={}: 准备生成响应", _fd, _connId);

    try {
        _response.MakeResponse(_writeBuff);
    } catch (const std::exception& e) {
        LOG_E("生成响应时发生异常: {}, fd={}, connId={}", e.what(), _fd,
              _connId);
        return false;
    }

    // 响应头
    LOG_D("处理连接 fd={}, connId={}: 设置响应头缓冲区，写缓冲区可读字节数={}",
          _fd, _connId, _writeBuff.ReadableBytes());

    // 确保iov设置正确
    _iovCnt = 0;
    if (_writeBuff.ReadableBytes() > 0) {
        _iov[0].iov_base = _writeBuff.Peek();
        _iov[0].iov_len = _writeBuff.ReadableBytes();
        _iovCnt = 1;

        LOG_D("处理连接 fd={}, connId={}: iov[0]设置 - 基址={:p}, 长度={}", _fd,
              _connId, _iov[0].iov_base, _iov[0].iov_len);
    }

    // 文件
    if (_response.File() && _response.FileLen() > 0) {
        LOG_D(
            "处理连接 fd={}, connId={}: 设置文件缓冲区，文件指针={:p}, 长度={}",
            _fd, _connId, (void*)_response.File(), _response.FileLen());

        _iov[1].iov_base = _response.File();
        _iov[1].iov_len = _response.FileLen();
        _iovCnt = 2;
    } else {
        LOG_D("处理连接 fd={}, connId={}: 无文件或文件长度为0，File指针={:p}, "
              "长度={}",
              _fd, _connId, (void*)_response.File(), _response.FileLen());

        // 确保设置为空，防止使用未初始化的内存
        _iov[1].iov_base = nullptr;
        _iov[1].iov_len = 0;
    }

    LOG_D("处理连接 fd={}, connId={}: 文件大小={}, iov计数={}, 总写入字节={}",
          _fd, _connId, _response.FileLen(), _iovCnt, ToWriteBytes());
    return true;
}

} // namespace zener::http
