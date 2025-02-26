#include "core/server.h"
#include "config/config.h"
#include "core/epoller.h"
#include "database/sql_connector.h"
#include "http/conn.h"
#include "http/file_cache.h"
#include "task/threadpool_1.h"
#include "task/timer/timer.h"
#include "utils/defer.h"
#include "utils/log/logger.h"

#include <asm-generic/socket.h>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <memory>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace zener {
namespace v0 {

Server::Server(int port, const int trigMode, const int timeoutMS,
               const bool optLinger, const char* sqlHost, const int sqlPort,
               const char* sqlUser, const char* sqlPwd, const char* dbName,
               int connPoolNum, int threadNum, bool openLog, int logLevel,
               int logQueSize)
    : _port(port), _openLinger(optLinger), _timeoutMS(timeoutMS),
      _isClose(false), _threadpool(new ThreadPool(threadNum)),
      _epoller(new Epoller()) {

    // @log init
    Logger::Init();

    // @config init
    // Config::Init("config.toml");

    char cwdBuff[256];
    if (!getcwd(cwdBuff, 256)) { // 接受两个参数：缓冲区 char *buf 和 缓冲区大小
        LOG_E("Failed to get current working directory : {}", strerror(errno));
    }
    assert(cwdBuff);
    _cwd = std::string(cwdBuff);
    // strncat(_srcDir, "/static/", 16); // C 风格太阴间，都改成 std::string
    _staticDir = _cwd + "/static";
    http::Conn::userCount.store(0);
    http::Conn::staticDir = _staticDir.c_str(); // 似乎不太安全？不过 _staticDir
                                                // 之后不会修改了，理论上没问题

    initEventMode(trigMode);

    if (!initSocket()) {
        _isClose = true;
    }

    db::SqlConnector::GetInstance().Init(sqlHost, sqlPort, sqlUser, sqlPwd,
                                         dbName, connPoolNum);
    // TODO
    // 检查 log 是否初始化，如果没有，直接不打印 LOG
    // 或者控制是否打印至文件
    // 此处直接写死日志位置： ${cwd}/logs/xxx.log
    const std::string logDir = "logs";
    const std::string fullLogDir = _cwd + "/" + logDir;
    if (!Logger::WriteToFile(fullLogDir)) {
        LOG_E("Failed to create log file in directory: {}!", fullLogDir);
        return;
    }
    LOG_I("Server Init ===========================>");
    LOG_I(" __________ _   _ _____ ____");
    LOG_I("|__  / ____| \\ | | ____|  _ \\");
    LOG_I("  / /|  _| |  \\| |  _| | |_) |");
    LOG_I(" / /_| |___| |\\  | |___|  _ <");
    LOG_I("/____|_____|_| \\_|_____|_| \\_\\");
    LOG_I("| port: {0}, OpenLinger: {1}", port, optLinger ? "true" : "false");
    LOG_I("| Listen Mode: {0}, OpenConn Mode: {1}",
          (_listenEvent & EPOLLET ? "ET" : "LT"),
          (_connEvent & EPOLLET ? "ET" : "LT"));
    LOG_I("| Log level: {}", logLevel);
    LOG_I("| static path: {}", http::Conn::staticDir);
    LOG_I("| SqlConnPool num: {0}, ThreadPool num: {1}", connPoolNum,
          threadNum);
    LOG_I("| TimerManager: {}", TIMER_MANAGER_TYPE);
}

Server::~Server() {
    close(_listenFd);
    _isClose = true;
    db::SqlConnector::GetInstance().Close();
    LOG_I("=====================Server exited=====================");
    Logger::Flush();
    Logger::Shutdown();
}

// TODO 我在 Epoller 里也设置了 trigMode 的设置与检查
// 检查是否有问题
void Server::initEventMode(const int trigMode) {
    _listenEvent = EPOLLRDHUP;
    _connEvent = EPOLLONESHOT | EPOLLRDHUP;
    switch (trigMode) {
    case 0:
        break;
    case 1:
        _connEvent |= EPOLLET;
        break;
    case 2:
        _listenEvent |= EPOLLET;
        break;
    case 3:
        _listenEvent |= EPOLLET;
        _connEvent |= EPOLLET;
        break;
    default:
        _listenEvent |= EPOLLET;
        _connEvent |= EPOLLET;
        break;
    }
    http::Conn::isET = (_connEvent & EPOLLET);
}

// TODO 改为自己的 timer 实现，替换为红黑树
void Server::Start() {
    int timeMS = -1; // 默认不超时，一直阻塞，直到有事件发生
    if (_timeoutMS > 0) {
        timeMS = _timeoutMS;
    }
    LOG_I(
        "Server Start | Port : {0}, timeout: {1}ms ==========================>",
        _port, _timeoutMS);

    // 删除file_cache清理线程，这不应该影响正常处理
    // 因为文件缓存可以在使用时被检查，而不需要独立线程

    while (!_isClose) {
        if (_timeoutMS > 0) {
            timeMS = _timeoutMS;
        }

        const int eventCnt = _epoller->Wait(timeMS);
        if (eventCnt == 0) {
            // 超时，继续下一轮
            continue;
        }

        for (int i = 0; i < eventCnt; i++) {
            const int fd = _epoller->GetEventFd(i);
            const uint32_t events = _epoller->GetEvents(i);

            // 检查文件描述符的有效性
            if (fd <= 0) {
                LOG_W("从epoll收到无效的文件描述符 {}", fd);
                continue;
            }

            if (fd == _listenFd) {
                dealListen();
            } else if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 检查连接是否在_users映射中
                auto it = _users.find(fd);
                if (it != _users.end()) {
                    closeConn(&it->second.conn);
                } else {
                    // 从epoll中删除并关闭
                    _epoller->DelFd(fd);
                    close(fd);
                }
            } else if (events & EPOLLIN) {
                // 检查连接是否在_users映射中
                auto it = _users.find(fd);
                if (it != _users.end()) {
                    dealRead(&it->second.conn);
                } else {
                    // 从epoll中删除并关闭
                    _epoller->DelFd(fd);
                    close(fd);
                }
            } else if (events & EPOLLOUT) {
                // 检查连接是否在_users映射中
                auto it = _users.find(fd);
                if (it != _users.end()) {
                    dealWrite(&it->second.conn);
                } else {
                    // 从epoll中删除并关闭
                    _epoller->DelFd(fd);
                    close(fd);
                }
            } else {
                LOG_W("未预期的事件");
            }
        }
    }
}

void Server::Stop() {
    if (close(_listenFd) != 0) {
        LOG_E("Failed to close listen fd {0} : {1}", _listenFd,
              strerror(errno));
    }
    db::SqlConnector::GetInstance().Close();
    _isClose = true;
    LOG_I("Server Stop =========================>");
    Logger::Flush();
    Logger::Shutdown();
}

void Server::Shutdown(const int timeoutMS) {
    LOG_I("服务器关闭==========================>");

    try {
        // 首先标记服务器为已关闭状态，确保所有线程都知道要退出
        _isClose = true;

        // 停止定时器管理器
#ifdef __USE_MAPTIMER
        try {
            LOG_I("正在停止MAP定时器管理器...");
            zener::rbtimer::TimerManager::GetInstance().Stop();
        } catch (const std::exception& e) {
            LOG_E("停止MAP定时器时出错: {}", e.what());
        }
#else
        try {
            LOG_I("正在停止HEAP定时器管理器...");
            zener::v0::TimerManager::GetInstance().Stop();
        } catch (const std::exception& e) {
            LOG_E("停止HEAP定时器时出错: {}", e.what());
        }
#endif

        // 关闭监听socket，阻止新连接
        if (_listenFd >= 0) {
            // 从epoll中删除监听socket
            _epoller->DelFd(_listenFd);

            int fd = _listenFd;
            _listenFd = -1; // 先置为-1，避免重复关闭
            close(fd);
            LOG_I("成功关闭监听socket (fd={})", fd);
        }

        // 关闭所有现有连接
        auto startTime = std::chrono::steady_clock::now();
        int remainingCount = _users.size();

        if (remainingCount > 0) {
            LOG_I("正在关闭{}个活动连接...", remainingCount);

            // 复制所有文件描述符到一个向量中，防止在迭代过程中修改映射
            std::vector<int> fdsToClose;
            fdsToClose.reserve(_users.size());
            for (const auto& [fd, _] : _users) {
                if (fd > 0) {
                    fdsToClose.push_back(fd);
                }
            }

            // 循环关闭所有连接
            for (int fd : fdsToClose) {
                auto it = _users.find(fd);
                if (it != _users.end()) {
                    closeConn(&it->second.conn);
                }

                // 检查是否已经超时
                auto now = std::chrono::steady_clock::now();
                auto timeElapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - startTime)
                        .count();

                if (timeoutMS > 0 && timeElapsed >= timeoutMS) {
                    LOG_W("连接关闭操作超时 ({}ms)，还剩{}个连接", timeoutMS,
                          _users.size());
                    break;
                }
            }

            // 如果仍有连接未关闭，强制清除
            if (!_users.empty()) {
                LOG_W("强制清除{}个剩余连接", _users.size());
                _users.clear();
            }
        } else {
            LOG_I("没有需要关闭的活动连接");
        }

        // 关闭线程池
        if (_threadpool) {
            LOG_I("正在关闭线程池...");
            const int poolTimeoutMS =
                timeoutMS > 0 ? std::min(timeoutMS / 2, 3000) : 3000;
            _threadpool->Shutdown(poolTimeoutMS);
        }

        // 关闭数据库连接
        try {
            LOG_I("正在关闭数据库连接...");
            db::SqlConnector::GetInstance().Close();
        } catch (const std::exception& e) {
            LOG_E("关闭数据库连接时出错: {}", e.what());
        }

        LOG_I("服务器关闭完成");
        Logger::Flush();
    } catch (const std::exception& e) {
        LOG_E("服务器关闭异常: {}", e.what());
    } catch (...) {
        LOG_E("服务器关闭遇到未知异常");
    }
}

void Server::sendError(int fd, const char* info) {
    assert(fd > 0);
    if (const auto ret = send(fd, info, strlen(info), 0); ret > 0) {
        LOG_W("Send error to client {0} error! {2}", fd, info, strerror(errno));
    }
    close(fd);
}

void Server::closeConn(http::Conn* client) const {
    assert(client);
    int fd = client->GetFd();
    uint64_t connId = client->GetConnId();

    // 检查文件描述符的有效性
    if (fd <= 0) {
        LOG_W("尝试关闭无效连接 fd={}, connId={}，仅从用户映射中删除", fd,
              connId);
        // 仍然需要从用户映射中删除
        _users.erase(fd);
        return;
    }

    LOG_I("关闭连接: fd={}, connId={}", fd, connId);

    // 1. 先从定时器映射中删除该fd关联的定时器
    if (_timeoutMS > 0) {
        try {
            // 使用TimerManagerImpl而不是具体实现类
            TimerManagerImpl::GetInstance().CancelByKey(fd);
        } catch (const std::exception& e) {
            LOG_E("取消fd {}的定时器时发生异常, connId {}: {}", fd, connId,
                  e.what());
        } catch (...) {
            LOG_E("取消fd {}的定时器时发生未知异常, connId {}", fd, connId);
        }
    }

    // 2. 从epoll中删除文件描述符
    if (!_epoller->DelFd(fd)) {
        LOG_E("从epoll中删除客户端fd [{0} - {1} {2}], connId {3} 失败!", fd,
              client->GetIP(), client->GetPort(), connId);
    } else {
        LOG_D("成功从epoll中移除fd {} (connId={})", fd, connId);
    }

    // 3. 关闭连接
    client->Close();

    // 4. 从_users映射中删除此连接
    _users.erase(fd);
    LOG_D("从用户映射中移除连接: fd={}, connId={}", fd, connId);
}

void Server::addClient(int fd, const sockaddr_in& addr) const {
    assert(fd > 0);
    // 为新连接生成唯一ID
    uint64_t connId = _nextConnId.fetch_add(1, std::memory_order_relaxed);

    // 为客户端连接设置TCP选项
    int optval = 1;
    // 设置TCP_NODELAY，禁用Nagle算法，减少小数据包延迟
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval)) < 0) {
        LOG_W("Failed to set TCP_NODELAY for client fd {}: {}", fd,
              strerror(errno));
    }

    // 增大接收缓冲区
    int recvBufSize = 64 * 1024; // 64KB
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &recvBufSize,
                   sizeof(recvBufSize)) < 0) {
        LOG_W("Failed to set receive buffer for client fd {}: {}", fd,
              strerror(errno));
    }

    // 增大发送缓冲区
    int sendBufSize = 64 * 1024; // 64KB
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sendBufSize,
                   sizeof(sendBufSize)) < 0) {
        LOG_W("Failed to set send buffer for client fd {}: {}", fd,
              strerror(errno));
    }

    // 初始化连接
    _users[fd].conn.init(fd, addr);
    _users[fd].connId = connId;

    // 同时在连接对象中存储ID，方便跟踪
    _users[fd].conn.SetConnId(connId);

    LOG_I("New connection established: fd={}, connId={}", fd, connId);

    // 在定时器回调中使用连接ID进行校验
    TimerManagerImpl::GetInstance().ScheduleWithKey(
        fd, _timeoutMS, 0, [this, fd, connId]() {
            // 检查文件描述符和连接ID都匹配，防止文件描述符重用导致的错误关闭
            if (!_isClose && _users.count(fd) > 0 &&
                _users[fd].connId == connId) {
                closeConn(&_users[fd].conn);
            }
        });

    // 先设置非阻塞再添加到epoll，防止触发后数据读不出来
    setFdNonblock(fd);

    // 添加到epoll中监听可读事件
    if (!_epoller->AddFd(fd, EPOLLIN | _connEvent)) {
        LOG_E("Failed to add client fd {} to epoll!", fd);
        // 关闭连接，避免资源泄漏
        _users.erase(fd);
        close(fd);
    } else {
        LOG_D("Successfully added fd {} to epoll with events {}", fd,
              EPOLLIN | _connEvent);
    }
}

void Server::dealListen() const {
    struct sockaddr_in addr{};
    socklen_t len = sizeof(addr);

    // 每次最多接受的新连接数量，防止一次性接受太多连接导致其他任务饥饿
    constexpr int MAX_ACCEPT_PER_CALL = 50;
    int acceptCount = 0;

    do {
        const int fd =
            accept(_listenFd, reinterpret_cast<struct sockaddr*>(&addr), &len);
        if (fd <= 0) {
            return;
        }

        // 检查服务器是否已满
        if (http::Conn::userCount >= MAX_FD) {
            sendError(fd, "Server busy!");
            LOG_W("Clients full! Current user count: {}",
                  http::Conn::userCount.load());
            return;
        }

        addClient(fd, addr);

        // 增加接受连接计数
        acceptCount++;

        // 如果达到单次最大接受数量，本轮不再接受新连接
        if (acceptCount >= MAX_ACCEPT_PER_CALL) {
            LOG_D("Reached maximum accept count per call ({}), will "
                  "continue "
                  "in next cycle",
                  MAX_ACCEPT_PER_CALL);
            break;
        }
    } while (_listenEvent & EPOLLET);
}

void Server::dealRead(http::Conn* client) const {
    assert(client);
    int fd = client->GetFd();

    // 检查文件描述符的有效性
    if (fd <= 0) {
        LOG_W("dealRead called with invalid fd {}", fd);
        return;
    }

    // 检查连接是否在_users映射中
    if (_users.count(fd) == 0) {
        LOG_W("dealRead called for fd {} but not found in _users", fd);
        return;
    }

    extentTime(client);

    // 获取当前连接的ID
    uint64_t connId = client->GetConnId();

#ifdef __V0
    // 直接使用fd和connId标识连接，不再尝试复制Conn对象
    _threadpool->AddTask([this, fd, connId] {
        // 检查服务器是否已关闭
        if (_isClose) {
            LOG_W("Thread pool task aborted: server is closing");
            return;
        }

        // 检查连接是否仍然存在且ID匹配
        if (_users.count(fd) == 0) {
            LOG_W("Thread pool task aborted: fd {} no longer in _users map",
                  fd);
            return;
        }

        if (_users[fd].connId != connId) {
            LOG_W("Thread pool task aborted: connId mismatch for fd {} "
                  "(expected {}, found {})",
                  fd, connId, _users[fd].connId);
            return;
        }

        // 确保文件描述符仍然有效
        if (_users[fd].conn.GetFd() > 0) {
            onRead(&_users[fd].conn);
        } else {
            LOG_W("Thread pool task found invalid fd in conn object for fd "
                  "{}, connId {}",
                  fd, connId);
        }
    });
#elif
// TODO 线程池替换
#endif // __V0
}

void Server::dealWrite(http::Conn* client) const {
    assert(client);
    int fd = client->GetFd();

    // 检查文件描述符的有效性
    if (fd <= 0) {
        LOG_W("dealWrite called with invalid fd {}", fd);
        return;
    }

    // 检查连接是否在_users映射中
    if (_users.count(fd) == 0) {
        LOG_W("dealWrite called for fd {} but not found in _users", fd);
        return;
    }

    extentTime(client);

    // 获取当前连接的ID
    uint64_t connId = client->GetConnId();

#ifdef __V0
    // 直接使用fd和connId标识连接，不再尝试复制Conn对象
    _threadpool->AddTask([this, fd, connId] {
        // 检查服务器是否已关闭
        if (_isClose) {
            LOG_W("Thread pool task aborted: server is closing");
            return;
        }

        // 检查连接是否仍然存在且ID匹配
        if (_users.count(fd) == 0) {
            LOG_W("Thread pool task aborted: fd {} no longer in _users map",
                  fd);
            return;
        }

        if (_users[fd].connId != connId) {
            LOG_W("Thread pool task aborted: connId mismatch for fd {} "
                  "(expected {}, found {})",
                  fd, connId, _users[fd].connId);
            return;
        }

        // 确保文件描述符仍然有效
        if (_users[fd].conn.GetFd() > 0) {
            onWrite(&_users[fd].conn);
        } else {
            LOG_W("Thread pool task found invalid fd in conn object for fd "
                  "{}, connId {}",
                  fd, connId);
        }
    });
#elif
// TODO 线程池替换
#endif // __V0
}

void Server::onWrite(http::Conn* client) const {
    assert(client);
    int fd = client->GetFd();

    // 检查文件描述符的有效性
    if (fd <= 0) {
        LOG_W("onWrite called with invalid fd {}", fd);
        return;
    }

    // 检查连接是否仍然在用户映射中
    if (_users.count(fd) == 0) {
        LOG_W("onWrite: fd {} not found in _users map", fd);
        return;
    }

    // 获取当前连接ID并验证匹配
    uint64_t connId = client->GetConnId();
    if (_users.at(fd).connId != connId) {
        LOG_W("onWrite: fd {} has mismatched connId (expected {}, got {})", fd,
              _users.at(fd).connId, connId);
        return;
    }

    int ret = -1;
    int writeErrno = 0;

    ret = client->write(&writeErrno);

    // 扩展连接超时时间，防止大文件传输中连接被关闭
    extentTime(client);

    if (client->ToWriteBytes() == 0) { // 传输完成
        if (client->IsKeepAlive()) {
            // 如果是保持连接的，重新准备接收新的请求
            onProcess(client);
            return;
        }
        // 非保持连接，传输完成后关闭
        closeConn(client);
        return;
    }

    // 还有数据需要发送
    if (ret < 0) {
        if (writeErrno == EAGAIN || writeErrno == EWOULDBLOCK) {
            // 内核发送缓冲区已满，需要等待可写事件
            if (!_epoller->ModFd(fd, _connEvent | EPOLLOUT)) {
                LOG_E("修改fd {}为EPOLLOUT失败!", fd);
            }
            return;
        }
        // 其他错误，关闭连接
        LOG_E("写入错误 fd={}, connId={}, errno={}", fd, connId, writeErrno);
        closeConn(client);
        return;
    }

    // ret >= 0 但还有数据需要发送，继续注册EPOLLOUT事件
    if (client->ToWriteBytes() > 0) {
        if (!_epoller->ModFd(fd, _connEvent | EPOLLOUT)) {
            LOG_E("修改fd {}为EPOLLOUT失败!", fd);
        }
        LOG_D("文件数据部分发送: fd={}, connId={}, 已发送={}, 剩余={}", fd,
              connId, ret, client->ToWriteBytes());
        return;
    }
}

void Server::extentTime(const http::Conn* client) const {
    assert(client);
    if (_timeoutMS <= 0) {
        return; // 如果超时未设置，直接返回
    }

    int fd = client->GetFd();

    // 检查文件描述符的有效性
    if (fd <= 0) {
        LOG_W("extentTime called with invalid fd {}", fd);
        return;
    }

    // 查找当前连接的ID
    if (_users.count(fd) == 0) {
        LOG_W("extentTime called for fd {} but not found in _users", fd);
        return;
    }

    uint64_t connId = _users.at(fd).connId;

    // 使用ScheduleWithKey，确保每个文件描述符只有一个定时器
    // 增加额外的检查，确保定时器回调执行时连接仍然有效
    TimerManagerImpl::GetInstance().ScheduleWithKey(
        fd, _timeoutMS, 0, [this, fd, connId]() {
            // 先检查服务器是否已关闭
            if (_isClose) {
                LOG_D("Timer callback aborted: server is closing");
                return;
            }

            // 检查文件描述符和连接ID都匹配
            if (_users.count(fd) == 0) {
                LOG_D("Timer callback: connection with fd {} no longer exists",
                      fd);
                return;
            }

            if (_users[fd].connId != connId) {
                LOG_D("Timer callback: connId mismatch for fd {} (expected {}, "
                      "found {})",
                      fd, connId, _users[fd].connId);
                return;
            }

            // 检查文件描述符是否仍然有效
            if (_users[fd].conn.GetFd() <= 0) {
                LOG_W("Timer callback: invalid fd in conn object for fd {}, "
                      "connId {}",
                      fd, connId);
                _users.erase(fd);
                return;
            }

            // 所有检查通过，可以安全关闭连接
            this->closeConn(&this->_users[fd].conn);
        });
}

void Server::onRead(http::Conn* client) const {
    assert(client);
    int fd = client->GetFd();

    // 检查文件描述符的有效性
    if (fd <= 0) {
        LOG_W("onRead called with invalid fd {}", fd);
        return;
    }

    // 检查连接是否仍然在用户映射中
    if (_users.count(fd) == 0) {
        LOG_W("onRead: fd {} not found in _users map", fd);
        return;
    }

    // 获取当前连接ID并验证匹配
    uint64_t connId = client->GetConnId();
    if (_users.at(fd).connId != connId) {
        LOG_W("onRead: fd {} has mismatched connId (expected {}, got {})", fd,
              _users.at(fd).connId, connId);
        return;
    }

    int ret = -1;
    int readErrno = 0;
    ret = client->read(&readErrno);

    // 读取出错或连接关闭
    if (ret <= 0 && readErrno != EAGAIN) {
        closeConn(client);
        return;
    }

    // 只有当确实读取到数据时才处理请求
    if (ret > 0) {
        onProcess(client);
    } else {
        // 如果没有数据，重新注册EPOLLIN事件
        if (!_epoller->ModFd(fd, _connEvent | EPOLLIN)) {
            LOG_E("Failed to mod fd {} for EPOLLIN!", fd);
        }
    }
}

void Server::onProcess(http::Conn* client) const {
    assert(client);
    int fd = client->GetFd();

    // 检查文件描述符的有效性
    if (fd <= 0) {
        LOG_W("onProcess called with invalid fd {}", fd);
        return;
    }

    // 检查连接是否仍然在用户映射中
    if (_users.count(fd) == 0) {
        LOG_W("onProcess: fd {} not found in _users map", fd);
        return;
    }

    // 获取当前连接ID并验证匹配
    uint64_t connId = client->GetConnId();
    if (_users.at(fd).connId != connId) {
        LOG_W("onProcess: fd {} has mismatched connId (expected {}, got {})",
              fd, _users.at(fd).connId, connId);
        return;
    }

    if (client->process()) {
        if (!_epoller->ModFd(fd, _connEvent | EPOLLOUT)) {
            LOG_E("Failed to mod fd {}!", fd);
        }
    } else {
        if (!_epoller->ModFd(fd, _connEvent | EPOLLIN)) {
            LOG_E("Failed to mod fd {}!", fd);
        }
    }
}

/* Create listenFd */
bool Server::initSocket() {
    int ret = 0;
    struct sockaddr_in addr{};
    if (_port > 65535 || _port < 1024) {
        LOG_E("Port: {} is invalid!", _port);
        return false;
    }
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(_port);
    /*
    struct linger 通常与套接字选项 SO_LINGER 一起使用，通过 setsockopt
    函数设置，以指定在关闭套接字时如何处理未发送的数据或未确认的传输
        struct linger {
            int l_onoff;    // 0=禁用SO_LINGER，非0=启用
            int l_linger;   // 超时时间（单位：秒）
        };
    */
    struct linger optLinger = {};
    if (_openLinger) {
        optLinger.l_onoff = 1;
        optLinger.l_linger = 1;
    }

    _listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (_listenFd < 0) {
        LOG_E("Create socket error!, port: {0}, {1}", _port, strerror(errno));
        return false;
    }

    // 设置Linger选项
    ret = setsockopt(_listenFd, SOL_SOCKET, SO_LINGER, &optLinger,
                     sizeof(optLinger));
    if (ret < 0) {
        close(_listenFd);
        LOG_E("Init linger error! port: {0}, {1}", _port, strerror(errno));
        return false;
    }

    constexpr int optval = 1;
    // 端口复用，只有最后一个套接字会正常接收数据
    ret = setsockopt(_listenFd, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval,
                     sizeof(int));
    if (ret == -1) {
        LOG_E("Set socket error! {}", strerror(errno));
        close(_listenFd);
        return false;
    }

    // 增大接收缓冲区大小
    constexpr int recvBufSize = 64 * 1024; // 64KB
    ret = setsockopt(_listenFd, SOL_SOCKET, SO_RCVBUF, &recvBufSize,
                     sizeof(recvBufSize));
    if (ret < 0) {
        LOG_W("Failed to set receive buffer size: {}", strerror(errno));
        // 不中断继续, 仅警告
    }

    // 增大发送缓冲区大小
    constexpr int sendBufSize = 64 * 1024; // 64KB
    ret = setsockopt(_listenFd, SOL_SOCKET, SO_SNDBUF, &sendBufSize,
                     sizeof(sendBufSize));
    if (ret < 0) {
        LOG_W("Failed to set send buffer size: {}", strerror(errno));
        // 不中断继续, 仅警告
    }

    // 设置TCP_NODELAY，禁用Nagle算法，减少小数据包延迟
    ret = setsockopt(_listenFd, IPPROTO_TCP, TCP_NODELAY, &optval,
                     sizeof(optval));
    if (ret < 0) {
        LOG_W("Failed to set TCP_NODELAY: {}", strerror(errno));
        // 不中断继续, 仅警告
    }

    ret = bind(_listenFd, reinterpret_cast<struct sockaddr*>(&addr),
               sizeof(addr));
    if (ret < 0) {
        LOG_E("Bind port: {0} error! {1}", _port, strerror(errno));
        close(_listenFd);
        return false;
    }

    // 增加监听队列长度，使用SOMAXCONN系统默认最大值
    ret = listen(_listenFd, SOMAXCONN);
    if (ret < 0) {
        LOG_E("Listen port: {0} error!, {1}", _port, strerror(errno));
        close(_listenFd);
        return false;
    }

    LOG_I("Socket initialized with optimized settings - Listen "
          "backlog: {}, "
          "RecvBuf: {}KB, SendBuf: {}KB",
          SOMAXCONN, recvBufSize / 1024, sendBufSize / 1024);

    ret = _epoller->AddFd(_listenFd, _listenEvent | EPOLLIN);
    if (ret == 0) {
        LOG_E("Add listen fd : {0} error! {1}", _listenFd, strerror(errno));
        close(_listenFd);
        return false;
    }
    if (setFdNonblock(_listenFd) < 0) {
        LOG_E("Failed to set fd {}! {}", _listenFd, strerror(errno));
    }
    return true;
}

int Server::setFdNonblock(const int fd) {
    assert(fd > 0);
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFD, 0) | O_NONBLOCK);
}

} // namespace v0

// TODO 如果没有配置文件，生成一份默认配置文件
std::unique_ptr<v0::Server> NewServerFromConfig(const std::string& configPath) {
    if (!Config::Init(configPath)) {
        LOG_E("Failed to initialize config from {}!", configPath);
        return nullptr;
    }

    const auto appPort =
        static_cast<unsigned int>(atoi(zener::GET_CONFIG("app.port").c_str()));
    const auto trig = atoi(zener::GET_CONFIG("app.trig").c_str());
    const auto timeout = atoi(zener::GET_CONFIG("app.timeout").c_str());
    const auto sqlHost = zener::GET_CONFIG("mysql.host").c_str();
    const auto sqlPort = static_cast<unsigned int>(
        atoi(zener::GET_CONFIG("mysql.port").c_str()));
    const auto sqlUser = zener::GET_CONFIG("mysql.user");
    const auto sqlPassword = zener::GET_CONFIG("mysql.password");
    const auto database = zener::GET_CONFIG("mysql.database");
    const auto sqlPoolSize = atoi(zener::GET_CONFIG("mysql.poolSize").c_str());
    const auto threadPoolSize =
        atoi(zener::GET_CONFIG("thread.poolSize").c_str());

    auto server = std::make_unique<v0::Server>(
        appPort, trig, timeout, false, sqlHost, sqlPort, sqlUser.c_str(),
        sqlPassword.c_str(), database.c_str(), sqlPoolSize, threadPoolSize,
        true);
    assert(server);
    return server;
}

} // namespace zener