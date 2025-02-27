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

    Logger::Init();

    char cwdBuff[256];
    if (!getcwd(cwdBuff, 256)) { // 接受两个参数：缓冲区 char *buf 和 缓冲区大小
        LOG_E("Failed to get current working directory : {}", strerror(errno));
    }
    assert(cwdBuff);
    _cwd = std::string(cwdBuff);
    _staticDir = _cwd + "/static";
    http::Conn::userCount.store(0);
    http::Conn::staticDir = _staticDir.c_str();

    initEventMode(trigMode); // TODO 在Epoller里已经设置一遍了

    if (!initSocket()) {
        _isClose.store(false); // TODO 此处应退出，但是没有正常退出
    }

    db::SqlConnector::GetInstance().Init(sqlHost, sqlPort, sqlUser, sqlPwd,
                                         dbName, connPoolNum);

    const std::string logDir =
        "logs"; // 此处直接写死日志位置： ${cwd}/logs/xxx.log
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

void Server::Start() {
    int timeMS = -1; // epoll wait timeout 默认不超时，一直阻塞，直到有事件发生
    while (!_isClose.load(std::memory_order_acquire)) {
        if (_timeoutMS > 0) {
            // timeMS = _timeoutMS; // @WTF sb cursor
            timeMS = TimerManagerImpl::GetInstance().GetNextTick();
        }
        const int eventCnt = _epoller->Wait(timeMS);
        if (eventCnt == 0) { // 超时，继续下一轮 ? 会跳过超时任务处理?
            continue;
        }
        for (int i = 0; i < eventCnt; i++) { // 处理事件
            const int fd = _epoller->GetEventFd(i);
            const uint32_t events = _epoller->GetEvents(i);
            if (fd <= 0) {
                LOG_W("Invalid fd: {} from epoll!", fd);
                continue;
            }
            if (fd == _listenFd) {
                dealListen();
            } else if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                if (auto it = _users.find(fd);
                    it != _users.end()) {        // 节约一次查找
                    closeConn(&it->second.conn); // closeConn 内部有 _epoller 和
                                                 // _users 中的删除
                }
            } else if (events & EPOLLIN) { // @Read
                if (auto it = _users.find(fd); it != _users.end()) {
                    dealRead(&it->second.conn);
                } else {                        /* 和原版不一样的处理：
                                                   若 fd 已被关闭或未注册到 _users，但 epoll 仍在监听，
                                                   可能因内核事件队列未清空导致事件重复触发
                                                   这几行可以加到 dealWrite
                                                   等函数内部，但还是在这更清晰 -- 单一职责原则
                                                */
                    if (!_epoller->DelFd(fd)) { // 从epoll中删除并关闭
                        LOG_W("Invalid fd: {} from epoll!", fd);
                    }
                    close(fd);
                }
            } else if (events & EPOLLOUT) { // @Write
                if (auto it = _users.find(fd); it != _users.end()) {
                    dealWrite(&it->second.conn);
                } else {
                    if (!_epoller->DelFd(fd)) {
                        LOG_W("Invalid fd: {} from epoll!", fd);
                    }
                    close(fd);
                }
            } else {
                LOG_E("Unexpected events: {} from epoll!", events);
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
    LOG_I("Server Stop =========================>");
    _isClose.store(true, std::memory_order_release);
    Logger::Flush();
    Logger::Shutdown();
}

void Server::Shutdown(const int timeoutMS) {
    LOG_I("Shutdown initiated ==========================>");
    // -------------------- Phase 1: 准备关闭 --------------------
    _isClose.store(true, std::memory_order_release);
    TimerManagerImpl::GetInstance().Stop();
    // 双重唤醒机制确保 epoll_wait 退出
    constexpr uint64_t wakeValue = 1;
    if (write(_wakeupFd, &wakeValue, sizeof(wakeValue)) == -1) {
        LOG_E("Failed to write wakeup fd: {}", strerror(errno));
    }
    // -------------------- Phase 2: 关闭监听 socket --------------------
    if (_listenFd >= 0) {
        std::lock_guard<std::mutex> lock(_connMutex);
        if (!_epoller->DelFd(_listenFd)) {
            LOG_E("Failed to close listen fd: {}", strerror(errno));
        }
        close(_listenFd);
        _listenFd = -1;
    }
    // -------------------- Phase 3: 关闭现有连接 --------------------
    const auto shutdownStart = std::chrono::steady_clock::now();
    std::vector<std::pair<int, ConnInfo>> fdsToClose;
    { // 缩小锁作用域
        std::lock_guard<std::mutex> lock(_connMutex);
        fdsToClose.reserve(_users.size());
        if (!_users.empty()) {
            LOG_I("Closing {} active connections...", _users.size());
            fdsToClose.reserve(_users.size());
            for (auto& [fd, conn] : _users) {
                if (fd > 0) {
                    // 显式构造 pair<int, ConnInfo>，强制右值引用
                    // fdsToClose.emplace_back(
                    //     std::piecewise_construct,
                    //     std::forward_as_tuple(fd), // 隐式转换 const int → int
                    //     std::forward_as_tuple(std::move(conn)) // 强制触发移动构造
                    // );
                    fdsToClose.emplace_back(fd, std::move(conn));
                }
            }
        }
    }
    // 异步关闭所有连接（避免阻塞主线程）
    std::atomic<size_t> remaining = fdsToClose.size();
    for (auto& [fd, conn] : fdsToClose) {
        closeConnAsync(fd, [&remaining] { --remaining; });
    }
    // 等待连接关闭或超时
    while (remaining > 0) {
        constexpr int CHECK_INTERVAL_MS = 50;
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - shutdownStart)
                .count();
        if (timeoutMS > 0 && elapsed >= timeoutMS) {
            LOG_W("Connection close timeout ({}ms), {} connections remaining",
                  timeoutMS, remaining.load());
            break;
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(CHECK_INTERVAL_MS));
    }
    // -------------------- Phase 4: 关闭线程池 --------------------
    if (_threadpool) {
        const int poolTimeout = std::max(100, timeoutMS / 2); // 至少 100ms
        _threadpool->Shutdown(poolTimeout);
    }
    // -------------------- Phase 5: 清理残留资源 --------------------
    {
        std::lock_guard<std::mutex> lock(_connMutex);
        _users.clear();
    }
    try { // 关闭数据库连接
        db::SqlConnector::GetInstance().Close();
    } catch (const std::exception& e) {
        LOG_E("Database shutdown error: {}", e.what());
    }
    LOG_I("Shutdown completed >>>>>>>>>>>>>>>>>>>>>>>>");
    Logger::Flush();
}

void Server::sendError(int fd, const char* info) {
    assert(fd > 0);
    if (const auto ret = send(fd, info, strlen(info), 0); ret > 0) {
        LOG_E("Send error to client {} error: {}! {}", fd, info,
              strerror(errno));
    }
    close(fd);
}

void Server::closeConn(http::Conn* client) {
    assert(client);
    int fd = client->GetFd();
    LOG_I("Client[{}-{}:{}] closed.", fd, client->GetIP(), client->GetPort());
    uint64_t connId = client->GetConnId();
    if (fd <= 0) {
        LOG_W("Try to close invalid fd : {}, connId : {}! remove from _users!",
              fd, connId);
        _users.erase(fd); //
        return;
    }
    if (_timeoutMS > 0) { // 1. 先从定时器映射中删除该fd关联的定时器
        try {
            TimerManagerImpl::GetInstance().CancelByKey(fd);
        } catch (const std::exception& e) {
            LOG_E("Exception canceling fd {} from timer: connId {}: {}", fd,
                  connId, e.what());
        } catch (...) {
            LOG_E("Unknown Exception canceling fd {} from timer: connId {}", fd,
                  connId);
        }
    }
    if (!_epoller->DelFd(fd)) { // 2. 从epoll中删除文件描述符
        LOG_E("Failed to del fd [{}-{}:{}], connId {} from epoll!", fd,
              client->GetIP(), client->GetPort(), connId);
    }
    client->Close();  // 3. 关闭连接
    _users.erase(fd); // 4. 从_users映射中删除此连接
}

// 实际关闭逻辑（与原有 closeConn 逻辑解耦）
void Server::_closeConnInternal(int fd, const http::Conn& conn) const {
    LOG_I("Closing fd {} (connId={}) asynchronously", fd, conn.GetConnId());
    if (_timeoutMS > 0) {
        try {
            TimerManagerImpl::GetInstance().CancelByKey(fd);
        } catch (const std::exception& e) {
            LOG_E("Timer cancel error: {}", e.what());
        }
    }
    if (!_epoller->DelFd(fd)) {
        LOG_E("Epoll del fd {} failed", fd);
    }
    if (close(fd) == -1) {
        LOG_E("Close fd {} error: {}", fd, strerror(errno));
    }
}

void Server::_closeConnInternal(int fd, http::Conn&& conn) const {
    LOG_I("Closing fd {} (connId={}) asynchronously", fd, conn.GetConnId());
    if (_timeoutMS > 0) {
        try {
            TimerManagerImpl::GetInstance().CancelByKey(fd);
        } catch (const std::exception& e) {
            LOG_E("Timer cancel error: {}", e.what());
        }
    }
    if (!_epoller->DelFd(fd)) {
        LOG_E("Epoll del fd {} failed", fd);
    }
    if (close(fd) == -1) {
        LOG_E("Close fd {} error: {}", fd, strerror(errno));
    }
}

void Server::closeConnAsync(int fd, const std::function<void()>& callback) {
    ConnInfo connInfoCopy;
    {
        std::lock_guard<std::mutex> lock(_connMutex);
        const auto it = _users.find(fd);
        if (it == _users.end())
            return;
        connInfoCopy = std::move(it->second); // 拷贝连接信息
    }
    // 提交到线程池执行关闭操作
    auto connInfoPtr = std::make_shared<ConnInfo>(std::move(connInfoCopy));
    _threadpool->AddTask(
        [this, fd, callback, connInfoPtr]() mutable {
            // 实际关闭操作（无需锁，使用拷贝数据）
            _closeConnInternal(fd, std::move(connInfoPtr->conn));
            {
                std::lock_guard<std::mutex> lock(_connMutex);
                _users.erase(fd); // 安全删除
            }
            if (callback)
                callback();
        });
}

void Server::addClient(int fd, const sockaddr_in& addr) {
    assert(fd > 0);
    /*
        设置 TCP_NODELAY，禁用Nagle算法，减少小数据包延迟
        HTTP 服务器通常需禁用 Nagle 算法，减少小数据包（如请求头、ACK）延迟。
        大文件传输或批量数据场景可保留 Nagle 算法以提升吞吐量
    */
#ifdef __USE_NO_DELAY
    constexpr int optval = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval)) < 0) {
        LOG_W("Failed to set TCP_NODELAY for client fd {}: {}", fd,
              strerror(errno));
    }
#endif // !__USE_NO_DELAY

    uint64_t connId = _nextConnId.fetch_add(
        1, std::memory_order_relaxed); // 为新连接生成唯一ID
    _users[fd].conn.Init(fd, addr);
    _users[fd].connId = connId;
    _users[fd].conn.SetConnId(connId);
    LOG_I("New connection established: fd={}, connId={}", fd, connId);

    if (_timeoutMS > 0) { // 在定时器回调中使用连接ID进行校验
        TimerManagerImpl::GetInstance().ScheduleWithKey(
            fd, _timeoutMS, 0, [this, fd, connId]() {
                // 检查文件描述符和连接ID都匹配，防止文件描述符重用导致的错误关闭
                if (!_isClose.load(std::memory_order_acquire) &&
                    _users.count(fd) > 0 && _users[fd].connId == connId) {
                    closeConn(&_users[fd].conn);
                }
            });
    }
    /*
        在 epoll 开始监听 fd 前，必须确保 fd 处于非阻塞模式。
        若顺序颠倒，可能在 epoll_wait 返回事件后，执行阻塞式读写，导致线程卡死
        （尤其在单线程 Reactor 中）
     */
    if (setFdNonblock(fd) == -1) {
        LOG_E("Error setFdNonblock: {}! {}", fd, strerror(errno));
        _users.erase(fd);
        close(fd);
        return;
    }
    if (!_epoller->AddFd(fd, EPOLLIN | _connEvent)) {
        LOG_E("Failed to add client fd {} to epoll!", fd);
        _users.erase(fd);
        close(fd);
    } else {
        LOG_D("Added fd {} to epoll with events {}.", fd, EPOLLIN | _connEvent);
        LOG_I("Client [{}-{}:{}] in.", fd, _users[fd].conn.GetIP(),
              _users[fd].conn.GetPort());
    }
}

int Server::setFdNonblock(const int fd) {
    assert(fd > 0);
    /*
        fcntl(fd, F_GETFD, 0) 返回的是 文件描述符标志（如 FD_CLOEXEC）
        而 O_NONBLOCK 属于 ​文件状态标志**，二者的位域完全不同
        源码疑似有误：
        return fcntl(fd, F_SETFL, fcntl(fd, F_GETFD, 0) | O_NONBLOCK);
     */
    // 1. 获取当前文件状态标志（使用 F_GETFL）
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    // 2. 设置非阻塞模式（更新文件状态标志）
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    // return fcntl(fd, F_SETFL, fcntl(fd, F_GETFD, 0) | O_NONBLOCK);
}

void Server::dealListen() {
    struct sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    if (_listenEvent & EPOLLET) { // ET 模式：必须处理所有就绪连接
        while (true) {
            const int fd = accept(
                _listenFd, reinterpret_cast<struct sockaddr*>(&addr), &len);
            if (fd <= 0)
                break;
            if (http::Conn::userCount >= MAX_FD) { // 检查服务器是否已满
                sendError(fd, "Server busy!");
                LOG_W("Clients full! Current user count: {}.",
                      http::Conn::userCount.load());
                return;
            }
            addClient(fd, addr);
        }
    } else { // LT 模式：限制单次 accept 数量
        const int maxAccept =
            std::min(50, MAX_FD - http::Conn::userCount.load());
        for (int i = 0; i < maxAccept; ++i) {
            const int fd = accept(
                _listenFd, reinterpret_cast<struct sockaddr*>(&addr), &len);
            if (fd <= 0)
                break;
            if (http::Conn::userCount >= MAX_FD) { // 检查服务器是否已满
                sendError(fd, "Server busy!");
                LOG_W("Clients full! Current user count: {}.",
                      http::Conn::userCount.load());
                return;
            }
            addClient(fd, addr);
        }
    }
}

void Server::dealRead(const http::Conn* client) { // 无比猛烈的异常检查
    assert(client);
    int fd = client->GetFd();
    if (fd <= 0) {
        LOG_D("{} called with invalid fd {}!", __FUNCTION__, fd);
        return;
    }
    if (_users.count(fd) == 0) {
        LOG_D("{} called for fd {} but not found in _users", __FUNCTION__, fd);
        return;
    }
    extentTime(client);
    uint64_t connId = client->GetConnId();
#ifdef __V0
    // 进一步验证文件描述符并检查是否已关闭
    if (_isClose.load(std::memory_order_acquire)) {
        LOG_D("Server is closing, skip adding read task for fd {}", fd);
        return;
    }
    _threadpool->AddTask([this, fd, connId] {
        try {
            if (_isClose.load(std::memory_order_acquire)) {
                LOG_D("Thread pool task aborted: server is closing.");
                return;
            }
            if (_users.count(fd) == 0) { // 检查连接是否仍然存在且ID匹配
                LOG_D(
                    "Thread pool task aborted: fd {} no longer in _users map.",
                    fd);
                return;
            }
            if (_users[fd].connId != connId) {
                LOG_D("Thread pool task aborted: connId mismatch for fd {} "
                      "(expected {}, found {}).",
                      fd, connId, _users[fd].connId);
                return;
            }
            if (_users[fd].conn.GetFd() > 0) { // 确保文件描述符仍然有效
                onRead(&_users[fd].conn);
            } else {
                LOG_D("Thread pool task found invalid fd in conn object for fd "
                      "{}, connId {}.",
                      fd, connId);
            }
        } catch (const std::exception& e) {
            LOG_E("Exception in dealRead thread pool task: {}!", e.what());
        } catch (...) {
            LOG_E("Unknown exception in dealRead thread pool task!");
        }
    });
#elif
// TODO 线程池替换
#endif // __V0
}

void Server::dealWrite(const http::Conn* client) {
    assert(client);
    int fd = client->GetFd();
    if (fd <= 0) {
        LOG_E("{} called with invalid fd {}!", __FUNCTION__, fd);
        return;
    }
    if (_users.count(fd) == 0) {
        LOG_E("{} called for fd {} but not found in _users!", __FUNCTION__, fd);
        return;
    }
    extentTime(client);
    uint64_t connId = client->GetConnId();
#ifdef __V0
    if (_isClose.load(std::memory_order_acquire)) {
        LOG_D("Server is closing, skip adding write task for fd {}.", fd);
        return;
    }
    _threadpool->AddTask([this, fd, connId] {
        try {
            if (_isClose) {
                LOG_D("Thread pool task aborted: server is closing.");
                return;
            }
            if (_users.count(fd) == 0) {
                LOG_D(
                    "Thread pool task aborted: fd {} no longer in _users map.",
                    fd);
                return;
            }
            if (_users[fd].connId != connId) {
                LOG_D("Thread pool task aborted: connId mismatch for fd {} "
                      "(expected {}, found {}).",
                      fd, connId, _users[fd].connId);
                return;
            }
            if (_users[fd].conn.GetFd() > 0) {
                onWrite(&_users[fd].conn);
            } else {
                LOG_D("Thread pool task found invalid fd in conn object for fd "
                      "{}, connId {}.",
                      fd, connId);
            }
        } catch (const std::exception& e) {
            LOG_E("Exception in dealWrite thread pool task: {}!", e.what());
        } catch (...) {
            LOG_E("Unknown exception in dealWrite thread pool task!");
        }
    });
#elif
// TODO 线程池替换
#endif // __V0
}

void Server::extentTime(const http::Conn* client) {
    assert(client);
    if (_timeoutMS <= 0) {
        return;
    }
    int fd = client->GetFd();
    if (fd <= 0) {
        LOG_W("{} called with invalid fd {}!", __FUNCTION__, fd);
        return;
    }
    if (_users.count(fd) == 0) {
        LOG_W("{} called for fd {} but not found in _users!", __FUNCTION__, fd);
        return;
    }
    uint64_t connId = _users.at(fd).connId;
    /*
        使用ScheduleWithKey，确保每个文件描述符只有一个定时器
        增加额外的检查，确保定时器回调执行时连接仍然有效
        webserver 11 里调用了一个 timer_->adjust
    */
    TimerManagerImpl::GetInstance().ScheduleWithKey(
        fd, _timeoutMS, 0, [this, fd, connId]() {
            if (_isClose) {
                LOG_D("Timer callback aborted: server is closing.");
                return;
            }
            if (_users.count(fd) == 0) {
                LOG_D("Timer callback: connection with fd {} no longer exists.",
                      fd);
                return;
            }
            if (_users[fd].connId != connId) {
                LOG_D("Timer callback: connId mismatch for fd {} (expected {}, "
                      "found {}).",
                      fd, connId, _users[fd].connId);
                return;
            }
            if (_users[fd].conn.GetFd() <= 0) {
                LOG_W("Timer callback: invalid fd in conn object for fd {}, "
                      "connId {}.",
                      fd, connId);
                _users.erase(fd);
                return;
            }
            this->closeConn(&this->_users[fd].conn);
        });
}

void Server::onRead(http::Conn* client) {
    assert(client);
    int fd = client->GetFd();
    if (fd <= 0) {
        LOG_W("{} called with invalid fd {}!", __FUNCTION__, fd);
        return;
    }
    if (_users.count(fd) == 0) {
        LOG_W("{}: fd {} not found in _users map!", __FUNCTION__, fd);
        return;
    }
    if (uint64_t connId = client->GetConnId(); _users.at(fd).connId != connId) {
        LOG_W("{}: fd {} has mismatched connId (expected {}, got {}).",
              __FUNCTION__, fd, _users.at(fd).connId, connId);
        return;
    }
    int ret = -1;
    int readErrno = 0;
    ret = client->Read(&readErrno);
    if (ret <= 0 && readErrno != EAGAIN) { // 读取出错或连接关闭
        closeConn(client);
        return;
    }
    if (ret > 0) { // 只有当确实读取到数据时才处理请求
        onProcess(client);
    } else {
        { // 如果没有数据，重新注册 EPOLLIN 事件 --原版没有这一步处理
            if (!_epoller->ModFd(fd, _connEvent | EPOLLIN))
                LOG_E("Failed to mod fd {} for EPOLLIN!", fd);
        }
    }
}

void Server::onProcess(http::Conn* client) {
    assert(client);
    int fd = client->GetFd();
    if (fd <= 0) {
        LOG_W("{} called with invalid fd {}!", __FUNCTION__, fd);
        return;
    }
    if (_users.count(fd) == 0) {
        LOG_W("{}: fd {} not found in _users map!", __FUNCTION__, fd);
        return;
    }
    if (uint64_t connId = client->GetConnId(); _users.at(fd).connId != connId) {
        LOG_W("{}: fd {} has mismatched connId (expected {}, got {}).",
              __FUNCTION__, fd, _users.at(fd).connId, connId);
        return;
    }
    if (client->Process()) {
        if (!_epoller->ModFd(fd, _connEvent | EPOLLOUT)) {
            LOG_E("Failed to mod fd {}!", fd);
        }
    } else {
        if (!_epoller->ModFd(fd, _connEvent | EPOLLIN)) {
            LOG_E("Failed to mod fd {}!", fd);
        }
    }
}

void Server::onWrite(http::Conn* client) {
    assert(client);
    int fd = client->GetFd();
    if (fd <= 0) {
        LOG_W("{} called with invalid fd {}!", __FUNCTION__, fd);
        return;
    }
    if (_users.count(fd) == 0) {
        LOG_W("{}: fd {} not found in _users map!", __FUNCTION__, fd);
        return;
    }
    uint64_t connId = client->GetConnId();
    if (_users.at(fd).connId != connId) {
        LOG_W("{}: fd {} has mismatched connId (expected {}, got {})",
              __FUNCTION__, fd, _users.at(fd).connId, connId);
        return;
    }
    int ret = -1;
    int writeErrno = 0;
    ret = client->Write(&writeErrno);
    extentTime(client);
    if (client->ToWriteBytes() == 0) { // 传输完成 TODO 长连接的其他处理？
        if (client
                ->IsKeepAlive()) { // 如果是 keep alive 的，重新准备接收新的请求
            onProcess(client);
            return;
        }
        closeConn(client);
        return;
    }
    if (ret < 0) { // 以下均为新加 TODO check
        if (writeErrno == EAGAIN || writeErrno == EWOULDBLOCK) {
            // 内核发送缓冲区已满，需要等待可写事件
            if (!_epoller->ModFd(fd, _connEvent | EPOLLOUT)) {
                LOG_E("Failed to adjust fd {} EPOLLOUT!", fd);
            }
            return;
        }
        LOG_E("Write err: fd:{}, connId:{}, errno:{}.", fd, connId, writeErrno);
        closeConn(client);
        return;
    }
    // ret >= 0 但还有数据需要发送，继续注册EPOLLOUT事件
    if (client->ToWriteBytes() > 0) {
        if (!_epoller->ModFd(fd, _connEvent | EPOLLOUT)) {
            LOG_E("Failed to adjust fd {} EPOLLOUT!", fd);
        }
        LOG_D(
            "Send part of file: fd:{}, connId:{}, already send {}, remains {}.",
            fd, connId, ret, client->ToWriteBytes());
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
        - 启用 Linger（l_linger=0）	快速回收端口，避免 TIME_WAIT
    占用资源（需权衡数据丢失风险）。
        - 文件传输或数据库服务禁用Linger, 确保数据完整传输，避免 RST
    导致数据丢失。
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
#ifdef USE_TCP_NODELAY
    // 设置TCP_NODELAY，禁用Nagle算法，减少小数据包延迟
    ret = setsockopt(_listenFd, IPPROTO_TCP, TCP_NODELAY, &optval,
                     sizeof(optval));
    if (ret < 0) {
        LOG_W("Failed to set TCP_NODELAY: {}",
              strerror(errno)); // 不中断继续, 仅警告
    }
#endif
    ret = bind(_listenFd, reinterpret_cast<struct sockaddr*>(&addr),
               sizeof(addr));
    if (ret < 0) {
        LOG_E("Bind port: {0} error! {1}", _port, strerror(errno));
        close(_listenFd);
        return false;
    }
    /*
     * backlog
     * 参数指定了套接字监听队列的预期最大长度，用于存放已完成三次握手但尚未被
     * accept() 处理的连接
     */
    ret = listen(_listenFd, SOMAXCONN); // 使用系统默认最大值，Linux内核会取
                                        // backlog 与 SOMAXCONN 的较小值
    if (ret < 0) {
        LOG_E("Listen port: {0} error!, {1}", _port, strerror(errno));
        close(_listenFd);
        return false;
    }
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


    ServerGuard::ServerGuard(v0::Server* srv, const bool useSignals)
        : _srv(srv), _useSignals(useSignals) {
        if (_useSignals) {
            SetupSignalHandlers();
        }
        _thread = std::thread([this] {
            _srv->Start();
            std::unique_lock<std::mutex> lock(_mutex);
            _cv.wait(lock, [this] { return _shouldExit.load(); });
        });
    }

    ServerGuard::~ServerGuard() {
        Shutdown();
        if (_thread.joinable()) {
            _thread.join();
        }
    }

    void ServerGuard::Shutdown() {
        std::lock_guard<std::mutex> lock(_mutex);
        _shouldExit = true;
        _cv.notify_all();
        _srv->Shutdown();
    }

    void ServerGuard::SetupSignalHandlers() {
        struct sigaction sa{};
        sa.sa_handler = SignalHandler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);
    }

    void ServerGuard::SignalHandler(int sig) {
        constexpr char msg[] = "Signal received\n";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);
        if (_instance) {
            _instance->Shutdown();
        }
    }

} // namespace zener