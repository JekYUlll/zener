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

Conn::Conn() : _fd(-1), _addr({0}), _isClose(true), _iovCnt(-1), _iov{} {
    // 将 _iovCnt 初始化为-1 不知道是否合适
}

Conn::~Conn() { Close(); }

void Conn::init(int fd, const sockaddr_in& addr) {
    assert(fd > 0);
    ++userCount;
    _addr = addr;
    _fd = fd;
    _writeBuff.RetrieveAll();
    _readBuff.RetrieveAll();
    _isClose = false;
    LOG_I("Client [{0} - {1} {2}] in, userCount: {3}", _fd, GetIP(), GetPort(),
          userCount.load());
}

void Conn::Close() {
    _response.UnmapFile();
    if (!_isClose) {
        _isClose = true;
        --userCount;
        close(_fd);
        LOG_I("Client [{0} - {1} {2}] quit, userCount: {3}", _fd, GetIP(), GetPort(),
              userCount.load());
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
    ssize_t len = -1;
    do {
        len = writev(_fd, _iov, _iovCnt);
        if (len <= 0) {
            *saveErrno = errno;
            break;
        }
        if (_iov[0].iov_len + _iov[1].iov_len == 0) {
            break; // 传输结束
        } else if (static_cast<size_t>(len) > _iov[0].iov_len) {
            _iov[1].iov_base = static_cast<uint8_t*>(_iov[1].iov_base) +
                               (len - _iov[0].iov_len);
            _iov[1].iov_len -= (len - _iov[0].iov_len);
            if (_iov[0].iov_len) {
                _writeBuff.RetrieveAll();
                _iov[0].iov_len = 0;
            }
        } else {
            _iov[0].iov_base = static_cast<uint8_t*>(_iov[0].iov_base) + len;
            _iov[0].iov_len -= len;
            _writeBuff.RetrieveAll();
        }
    } while (isET || ToWriteBytes() > 10240);
    return len;
}

bool Conn::process() {
    _request.Init();
    if (_readBuff.ReadableBytes() <= 0) {
        return false;
    } else if (_request.parse(_readBuff)) {
        LOG_D("{}", _request.path().c_str());
        _response.Init(staticDir, _request.path(), _request.IsKeepAlive(), 200);
    } else {
        _response.Init(staticDir, _request.path(), false, 400);
    }
    _response.MakeResponse(_writeBuff);
    // 响应头
    // 原代码此处用了 const_cast<char*>(writeBuff_.Peek()) 有点抽象
    _iov[0].iov_base = _writeBuff.Peek();
    _iov[0].iov_len = _writeBuff.ReadableBytes();
    _iovCnt = 1;
    // 文件
    if (_response.File() && _response.FileLen() > 0) {
        _iov[1].iov_base = _response.File();
        _iov[1].iov_len = _response.FileLen();
        _iovCnt = 2;
    }
    LOG_D("Filesize: {0}, {1} to {2}.", _response.FileLen(), _iovCnt,
          ToWriteBytes());
    return true;
}

} // namespace zener::http
