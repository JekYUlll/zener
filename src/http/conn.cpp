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
    : _fd(-1), _addr({}), _connId(0), _isClose(true), _iovCnt(0), _iov{} {
    // TODO 将 _iovCnt 初始化为0 不知道是否合适
}
/*
 *是否因为智能指针、或者因为move，从而导致其提前析构？
 */
Conn::~Conn() {
    // 最后有时候 userCount 为负数，可能因为有的 conn
    // 析构的同时超时，在线程池里同时 Close 调用了两遍 Close？
    /*
         *如果此处不Close，遇到了malloc(): unsorted double linked list corrupted
         *内存双重释放？也可能是内存越界。
    */
    if (!_isClose) {
        Close();
    }
}

Conn::Conn(Conn&& other) noexcept
    : _fd(other._fd), _addr(other._addr), _connId(other._connId),
      _readBuff(std::move(other._readBuff)),
      _writeBuff(std::move(other._writeBuff)),
      _request(std::move(other._request)),
      _response(std::move(other._response)) {

    LOG_W("Move Conn. id: {}", _connId);
    // 实际上此处只在尝试做 Shutdown 的时候才对 Conn 进行
    // move，错误应该与此无关。
    if (other._isClose) {
        LOG_W("Move a closed Conn! id: {}", other._connId);
    }

    this->_isClose = false; // 是否正确？
    other._fd = -1;
    other._connId = 0;
    other._addr = {};
    // TODO 是否要在此时添加 other 退出的事件？
    // 此处设置为 _isClose 可能会让 other 析构的时候不调用 Close
    other._isClose =
        true; // TODO other
              // 是否会触发析构并且在析构里调用Close？other还有定时器吗？
}

Conn& Conn::operator=(Conn&& other) noexcept {
    if (this != &other) {
        // 释放当前资源
        if (_fd != -1)
            close(_fd);
        _readBuff.RetrieveAll();
        _writeBuff.RetrieveAll();
        // 转移资源
        _fd = other._fd;
        _addr = other._addr;
        _connId = other._connId;
        _readBuff = std::move(other._readBuff);
        _writeBuff = std::move(other._writeBuff);
        _request = std::move(other._request);
        _response = std::move(other._response);
        // 置空原对象
        other._fd = -1;
        other._connId = 0;
        other._addr = {};
    }
    return *this;
}

void Conn::Init(const int sockFd, const sockaddr_in& addr) {
    assert(sockFd > 0);
    userCount.fetch_add(1, std::memory_order_acquire);
    _addr = addr;
    _fd = sockFd;
    // connID由Server设置，此时为0（非法值）
    _writeBuff.RetrieveAll();
    _readBuff.RetrieveAll();
    _isClose = false;
    LOG_I("(fd:{})[{}:{}] in, users count: {}.", _fd, GetIP(), GetPort(),
          static_cast<int>(userCount));
}

/*
    并不负责调用 Epoller 的 DelFd (_epoller 是 Server 的成员变量)
    也不负责从 _users 里删除 fd
*/
void Conn::Close() {
    // TODO 此处 debug 下会触发断言
    assert(_fd > 0);
    _response.UnmapFile();
    if (!_isClose) {
        _isClose = true;
        // 如果在closeConn里调用Close(即真正正确的实现):
        // 有时候userCount会变成负数，说明Close调用得比Init多
        userCount.fetch_sub(1, std::memory_order_release);
        if (_fd > 0) { // 确保只关闭有效的文件描述符
            close(_fd);
            LOG_I("(fd:{})[{}:{}] quit, users count: {}.", _fd, GetIP(),
                  GetPort(), static_cast<int>(userCount));
        } else {
            LOG_W("Client with invalid fd={} quit, userCount:{}!", _fd,
                  static_cast<int>(userCount));
            _fd = -1; // 关闭后将fd设为-1，防止重复关闭
        }
    }
}

ssize_t Conn::Read(int* saveErrno) {
    if constexpr (true) {
        ssize_t len = -1;
        ssize_t totalLen = 0;
        constexpr int maxIterations =
            8; // 限制最大读取次数，防止一直读取导致其他连接饥饿 TODO
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
                *saveErrno = ECONNRESET; // 连接关闭
                break;
            } else {
                // len < 0, 出错情况
                if (*saveErrno == EAGAIN || *saveErrno == EWOULDBLOCK) {
                    // 非阻塞模式下，所有数据已读完
                    // 和外层的 onRead 中重复处理了
                    break;
                }
                return len;
            }
        } while (isET &&
                 (iterations <
                  maxIterations)); // ET模式需要循环读取，但添加最大迭代次数限制
        return totalLen > 0 ? totalLen : len;
    } else { // 原方案 简单处理
        ssize_t len = -1;
        do {
            len = _readBuff.ReadFd(_fd, saveErrno);
            if (len <= 0) {
                break;
            }
        } while (isET);
        return len;
    }
}

ssize_t Conn::Write(int* saveErrno) {
#ifdef _ORIGIN_CONN
    // ssize_t len = -1;
    // do {
    //     len = writev(_fd, _iov, _iovCnt);
    //     if (len <= 0) {
    //         *saveErrno = errno;
    //         break;
    //     }
    //     if(_iov[0].iov_len + _iov[1].iov_len == 0) {
    //         break;
    //     } // 传输结束
    //     else if (static_cast<size_t>(len) > _iov[0].iov_len) {
    //         _iov[1].iov_base = static_cast<uint8_t *>(_iov[1].iov_base) +
    //         (len - _iov[0].iov_len); _iov[1].iov_len -= (len -
    //         _iov[0].iov_len); if(_iov[0].iov_len) {
    //             _writeBuff.RetrieveAll();
    //             _iov[0].iov_len = 0;
    //         }
    //     } else {
    //         _iov[0].iov_base = static_cast<uint8_t *>(_iov[0].iov_base) +
    //         len; _iov[0].iov_len -= len; _writeBuff.Retrieve(len);
    //     }
    // } while (isET || ToWriteBytes() > 10240);
    // return len;
#endif // !_ORIGIN_CONN
    // TODO 暂时换成上面简单的实现
    if constexpr (true) {
        ssize_t ret = 0;
        ssize_t totalWritten = 0;
        if (ToWriteBytes() == 0) {
            return totalWritten;
        }
        // 最多循环写两次
        constexpr int MAX_ATTEMPTS = 2;
        for (int attempt = 0; attempt < MAX_ATTEMPTS; ++attempt) {
            ret = writev(_fd, _iov, _iovCnt);
            if (ret < 0) {
                // 如果是EAGAIN，在ET模式下应继续尝试
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    *saveErrno = errno;
                    return totalWritten;
                }
                // 其他错误则直接返回
                *saveErrno = errno;
                LOG_E("fd={}: write error, {}", _fd, strerror(errno));
                return -1;
            }
            // 累计已写入的数据量
            totalWritten += ret;
            // 更新iov结构以反映已写入的数据
            if (static_cast<size_t>(ret) > _iov[0].iov_len) {
                // 头部数据全部发送完毕，开始发送文件数据
                const size_t fileWritten = ret - _iov[0].iov_len;
                _iov[1].iov_base =
                    static_cast<char*>(_iov[1].iov_base) + fileWritten;
                _iov[1].iov_len -= fileWritten;
                // 头部数据已全部发送，清空对应缓冲区
                _writeBuff.RetrieveAll();
                _iov[0].iov_base = nullptr;
                _iov[0].iov_len = 0;
            } else {
                // 头部数据未发送完毕
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
            if (constexpr ssize_t MAX_WRITE_PER_CALL = 4 * 1024 * 1024;
                !isET || totalWritten > MAX_WRITE_PER_CALL) {
                break;
            }
        }
        return totalWritten;
    }
}

// TODO 将返回值重构为 ProcessResult
Conn::ProcessResult Conn::Process() {
    if constexpr (false) { // 原版实现
        // _request.Init();
        // if(_readBuff.ReadableBytes() <= 0) {
        //     return false;
        // }
        // else if(_request.parse(_readBuff)) {
        //     LOG_D("Request Path: {}.", request_.Path().c_str());
        //     _response.Init(staticDir, _request.Path(),
        //     _request.IsKeepAlive(), 200);
        // } else {
        //     _response.Init(staticDir, _request.Path(), false, 400);
        // }
        // _response.MakeResponse(_writeBuff);
        // /* 响应头 */
        // _iov[0].iov_base = const_cast<char*>(_writeBuff.Peek());
        // _iov[0].iov_len = _writeBuff.ReadableBytes();
        // _iovCnt = 1;
        // /* 文件 */
        // if(_response.FileLen() > 0  && _response.File()) {
        //     _iov[1].iov_base = _response.File();
        //     _iov[1].iov_len = _response.FileLen();
        //     _iovCnt = 2;
        // }
        // LOG_D("filesize:{}, {} to {}.", _response.FileLen() , _iovCnt,
        // ToWriteBytes()); return true;
    } else {
        // 1. 如果读缓冲区为空，不进行后续处理
        if (_readBuff.ReadableBytes() <= 0) {
            LOG_D("fd={}: buffer is empty.", _fd);
            return ProcessResult::NEED_MORE_DATA;
        }
        _request.Init();
        // 2. 解析HTTP请求
        const bool parseSuccess = _request.parse(_readBuff);
        _response.UnmapFile(); // 清除之前可能存在的文件映射
        if (parseSuccess) {
            _response.Init(staticDir, _request.Path(), _request.IsKeepAlive(),
                           200);
        } else {
            LOG_W("fd={}: parse failed, request Path:{}", _fd,
                  _request.Path().c_str());
            _response.Init(staticDir, _request.Path(), false, 400);
        }
        // 3. 生成HTTP响应
        try {
            _response.MakeResponse(_writeBuff);
        } catch (const std::exception& e) {
            LOG_E("fd={}: make response failed, {}", _fd, e.what());
            return ProcessResult::ERROR;
        }
        // 4. 如果写缓冲区为空，可能是错误情况，返回false
        if (_writeBuff.ReadableBytes() == 0) {
            LOG_W("fd={}: buffer is empty.", _fd);
            return ProcessResult::ERROR;
        }
        // 5. 设置iov结构（响应头+文件）
        _iovCnt = 0;
        if (_writeBuff.ReadableBytes() > 0) {
            _iov[0].iov_base = _writeBuff.Peek();
            _iov[0].iov_len = _writeBuff.ReadableBytes();
            _iovCnt = 1;
        }
        // 6. 设置文件响应（如果有）
        if (_response.File() && _response.FileLen() > 0) {
            _iov[1].iov_base = _response.File();
            _iov[1].iov_len = _response.FileLen();
            _iovCnt = 2;
        }
        LOG_D("filesize:{}, {} to {}.", _response.FileLen(), _iovCnt,
              ToWriteBytes());
        return ProcessResult::OK;
    }
}

} // namespace zener::http
