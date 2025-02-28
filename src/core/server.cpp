#include "core/server.h"
#include "config/config.h"
#include "core/epoller.h"
#include "database/sql_connector.h"
#include "http/conn.h"
#include "task/threadpool_1.h"
#include "task/timer/timer.h"
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
        _isClose.store(
            false, std::memory_order_release); // TODO
                    // 此处应退出，但是没有正常退出，应该直接触发ServerGuard的信号？
        throw std::runtime_error("Failed to initialize listen socket.");
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
    _isClose.store(true, std::memory_order_release);
    db::SqlConnector::GetInstance().Close();
    LOG_I("=====================Server exited=====================");
    Logger::Flush();
    Logger::Shutdown();
}

// TODO 我在 Epoller 里也设置了 trigMode 的设置与检查
    ///@thread 安全
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

    ///@thread 单线程
void Server::Start() {
    /*
     * TODO 加读锁
     * 设计失误：如何对 _users 加锁？WebServer 里用的 users map，但无需加锁
     * 多个线程都会访问 _users ，主循环中也会访问。如果对主循环加锁，会导致很多线程阻塞
     * 合理的设计是：只在主循环中访问 _users
     */
    std::shared_lock readLocker(_connMutex, std::defer_lock);
    int timeMS = -1; // epoll wait timeout 默认不超时，一直阻塞，直到有事件发生
    while (!_isClose.load(std::memory_order_acquire)) {
        if (_timeoutMS > 0) {
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
                readLocker.lock();
                /*
                 *TODO 想出来的比较阴间的操作：
                 *如果不存在于 _users map 中（只会在错误下出现）：
                 *会调用 ConnInfo 的默认构造函数
                 *则 conn 会是 nullptr，connId 是 0
                 *但下次调用会查找到对象，且为默认初始化的不合法对象。
                 *所以在其他地方要进行判断。
                */
                const auto conn = _users[fd].conn.get();
                readLocker.unlock();
                assert(conn);
                if (conn) {
                    closeConn(conn); // closeConn内部有_epoller和_users中的删除
                }
            } else if (events & EPOLLIN) { // @Read
                readLocker.lock();
                const auto conn = _users[fd].conn.get();
                readLocker.unlock();
                assert(conn);
                if (conn) {
                    dealRead(conn);
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
                readLocker.lock();
                const auto conn = _users[fd].conn.get();
                readLocker.unlock();
                assert(conn);
                if (conn) {
                    dealWrite(conn);
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

    ///@thread 安全
void Server::Stop() {
    if (close(_listenFd) != 0) {
        LOG_E("Failed to close listen fd {0} : {1}", _listenFd,
              strerror(errno));
        _listenFd = -1;
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
    _isClose.store(
        true,
        std::memory_order_release); // TODO 关闭后Start不再循环，不进行新的读写
    TimerManagerImpl::GetInstance().Stop();
    // 双重唤醒机制确保 epoll_wait 退出
    constexpr uint64_t wakeValue = 1;
    if (write(_wakeupFd, &wakeValue, sizeof(wakeValue)) == -1) {
        LOG_E("Failed to write wakeup fd: {}", strerror(errno));
    }
    // -------------------- Phase 2: 关闭监听 socket --------------------
    if (close(_listenFd) != 0) {
        LOG_E("Close listen fd {0} : {1}", _listenFd, strerror(errno));
        _listenFd = -1;
    }
    const auto shutdownStart = std::chrono::steady_clock::now();
    std::vector<std::pair<int, ConnInfo>> fdsToClose;
    { // -------------------- Phase 3: 关闭现有连接 --------------------
        std::unique_lock locker(_connMutex);
        if (!_users.empty()) {
            LOG_I("Closing {} active connections...", _users.size());
            fdsToClose.reserve(_users.size());
            for (auto& [fd, conn] : _users) {
                if (fd > 0) {
                    fdsToClose.emplace_back(
                        std::piecewise_construct,  // 显式构造 pair<int,
                                                   // ConnInfo>，强制右值引用
                        std::forward_as_tuple(fd), // 隐式转换 const int → int
                        std::forward_as_tuple(
                            std::move(conn)) // 强制触发移动构造
                    );
                }
            }
            _users.clear(); // TODO 好像不太合适
        }
    }
    /*
        异步关闭所有连接（避免阻塞主线程）
        此处 remaining 如果是局部变量，若 closeConnAsync
       是异步操作（如提交到线程池） 需确保回调执行时 remaining 未被销毁
    */
    auto remaining = std::make_shared<std::atomic<size_t>>(fdsToClose.size());
    for (auto& [fd, conn] : fdsToClose) {
        closeConnAsync(fd, [remaining] { remaining->fetch_sub(1); });
    }
    // 等待连接关闭或超时
    while (remaining->load(std::memory_order_acquire) > 0) {
        constexpr int CHECK_INTERVAL_MS = 50;
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - shutdownStart)
                .count();
        if (timeoutMS > 0 && elapsed >= timeoutMS) { // 超时
            LOG_W("Connection close timeout ({}ms), {} connections remaining.",
                  timeoutMS, remaining->load());
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
    { // -------------------- Phase 5: 清理残留资源 --------------------
        std::unique_lock lock(_connMutex);
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

    ///@thread 安全
void Server::sendError(int fd, const char* info) {
    assert(fd > 0);
    if (const auto ret = send(fd, info, strlen(info), 0); ret > 0) {
        LOG_E("Send error to client {} error: {}! {}", fd, info,
              strerror(errno));
    }
    close(fd);
}

///@param client 调用的时候使用 connInfo.conn.get() 传入
///@notice 在release下不进行 client 的空指针判断，需要在调用的时候在外面判空
///@important 内部有锁
///@todo 能否检测重复关闭？对 client 的操作是否线程安全？
///@thread 安全
void Server::closeConn(const http::Conn* client) {
    assert(client);
    std::unique_lock writeLocker(_connMutex, std::defer_lock);
    int fd = client->GetFd();
    uint64_t connId = client->GetConnId();
    if (fd <= 0) {
        LOG_W("Closing invalid fd: {}, connId: {}! remove from _users.", fd,
              connId);
        writeLocker.lock();
            _users.erase(fd); // *
        writeLocker.unlock();
        return;
    }
    if constexpr (false) {
        /*
         * 删除定时器里预订的超时关闭操作，因为此函数会在此刻提前删除？
         * 实际上似乎脱裤子放屁，因为定时器里注册的本来就是此函数
         * 注册之后在本函数里又给 Cancel
         * 掉了，导致计时器一直是空的？所以连接占满，超时关闭失效？
         */
        if (_timeoutMS > 0) {
            try {
                TimerManagerImpl::GetInstance().CancelByKey(connId);
            } catch (const std::exception& e) {
                LOG_E("Exception canceling fd {} from timer: connId {}: {}", fd,
                      connId, e.what());
            } catch (...) {
                LOG_E(
                    "Unknown Exception canceling fd {} from timer: connId {}!",
                    fd, connId);
            }
        }
    }
    if (!_epoller->DelFd(fd)) { // 2. 从epoll中删除文件描述符
        LOG_E("Failed to del fd [{}-{}:{}], connId {} from epoll!", fd,
              client->GetIP(), client->GetPort(), connId);
    }
    writeLocker.lock();
    if (const auto it = _users.find(fd); it != _users.end()) {
        it->second.active.store(false, std::memory_order_release);
        it->second.connId = 0;
        // close(fd); // TODO 是否需要？
        _users.erase(fd);
    }
    writeLocker.unlock();
}

///@intro 没必要传入fd，传入 conn 就够了
///@praram 右值的conn
///@thread 安全
void Server::_closeConnInternal(http::Conn&& client) const {
    auto fd = client.GetFd();
    auto connId = client.GetConnId();
    assert(fd > 0);
    assert(connId > 0);
    LOG_T("Closing fd {} (connId={}) asynchronously.", fd, connId);
    if (_timeoutMS > 0) {
        try { // 取消计时器里注册的超时关闭任务，因为此时就要关闭了
            // TODO 严重bug：此处是否又把自己取消了？
            TimerManagerImpl::GetInstance().CancelByKey(connId);
        } catch (const std::exception& e) {
            LOG_E("Timer cancel error: {}", e.what());
        }
    }
    if (!_epoller->DelFd(fd)) {
        LOG_E("Epoll del fd {} failed, {}", fd, strerror(errno));
    }
    if (close(fd) == -1) {
        LOG_E("Close fd {} error: {}", fd, strerror(errno));
    }
}

    ///@thread 安全
void Server::closeConnAsync(int fd, const std::function<void()>& callback) {
    ConnInfo connInfoCopy{};
    { // TODO 检查 _users 中的删除流程是否正确
        std::unique_lock locker(_connMutex);
        const auto it = _users.find(fd);
        if (it == _users.end()) {
            return;
        }
        connInfoCopy = std::move(it->second); // 拷贝连接信息
    }
    auto connInfoPtr = std::make_shared<ConnInfo>(std::move(connInfoCopy));
    _threadpool->AddTask( // 提交到线程池执行关闭操作
                          // TODO 检查定时器实现。此任务有key吗？
        [this, fd, callback, connInfoPtr]() mutable {
            _closeConnInternal(std::move(*connInfoPtr->conn));
            {
                std::unique_lock lk(_connMutex);
                _users.erase(fd);
            }
            if (callback)
                callback();
        });
}

    ///@thread 安全
void Server::addClient(int fd, const sockaddr_in& addr) {
    assert(fd > 0);
    std::unique_lock writeLock(_connMutex, std::defer_lock);
#ifdef __USE_NO_DELAY
    if (setNoDelay(fd) < 0) { // TODO 对不同作用的socket应该设置不同行为
        LOG_W("Failed to set TCP_NODELAY for client fd {}: {}", fd,
              strerror(errno));
    }
#endif // !__USE_NO_DELAY
    uint64_t connId = _nextConnId.fetch_add(
        1, std::memory_order_relaxed); // 为新连接生成唯一ID
    try {
        /*
            conn初始化 防止出错被覆盖
        */
        writeLock.lock();
        auto [it, inserted] = _users.try_emplace(fd);
        assert(inserted);
        if (!inserted) {
            /*
             * TODO 多次触发 未解决 倒是不会宕机
             * fd 被快速复用，而旧的连接信息未及时从 _users 中清理干净
             * deepseek说必须立即关闭 fd！否则会导致 文件描述符泄漏
             * 测了一下发现并不会
             */
            LOG_E("Duplicate fd {} detected!", fd);
            close(fd); // ?
            return;
        }
        auto& connInfo = it->second;
        auto conn = std::make_unique<http::Conn>();
        conn->SetConnId(connId);
        conn->Init(fd, addr);
        connInfo.connId = connId;
        connInfo.conn = std::move(conn);
        writeLock.unlock();

    } catch (const std::exception& e) {
        LOG_E("Add client exception. fd:{}. id:{}. {}", fd, connId, e.what());
    }
    /*
     * @设置超时取消 此处传入 connId 和 fd
     * 在定时器回调中使用 connId 进行校验
     */
    if (_timeoutMS > 0) {
        TimerManagerImpl::GetInstance().ScheduleWithKey(
            fd, _timeoutMS, 0, [this, fd, connId]() {
                // 检查文件描述符和连接ID都匹配，防止文件描述符重用导致的错误关闭
                const http::Conn* conn = nullptr;
                {
                    std::shared_lock readLock(_connMutex, std::defer_lock);
                    if (!_isClose.load() && _users.count(fd) > 0 && _users[fd].connId == connId) {
                        conn = _users[fd].conn.get();
                    }
                }
                if (conn) { closeConn(conn); }
            });
    }
    /*
        在 epoll 开始监听 fd 前，必须确保 fd 处于非阻塞模式。
        若顺序颠倒，可能在 epoll_wait 返回事件后，执行阻塞式读写，导致线程卡死
        （尤其在单线程 Reactor 中）
     */
    if (setFdNonblock(fd) == -1) {
        LOG_E("Error setFdNonblock: {}! {}", fd, strerror(errno));
        writeLock.lock();
        _users.erase(fd);
        writeLock.unlock();
        close(fd);
        return;
    }
    if (!_epoller->AddFd(fd, EPOLLIN | _connEvent)) {
        LOG_E("Failed to add client fd {} to epoll!", fd);
        writeLock.lock();
        _users.erase(fd);
        writeLock.unlock();
        close(fd);
    } else {
        assert(_users[fd].conn); // 可能会访问空指针？
        // TODO 线程不安全
        LOG_I("Client [{}-{}:{}] in.", fd, _users[fd].conn->GetIP(),
              _users[fd].conn->GetPort());
    }
}

    ///@thread 安全
int Server::setFdNonblock(const int fd) {
    assert(fd > 0);
    /*
        fcntl(fd, F_GETFD, 0) 返回的是 文件描述符标志（如 FD_CLOEXEC）
        而 O_NONBLOCK 属于 文件状态标志**，二者的位域完全不同
        源码疑似有误：
        return fcntl(fd, F_SETFL, fcntl(fd, F_GETFD, 0) | O_NONBLOCK);
     */
    // 1. 获取当前文件状态标志（使用 F_GETFL）
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
        return -1;
    // 2. 设置非阻塞模式（更新文件状态标志）
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

    ///@thread 安全
int Server::setNoDelay(const int fd) {
    constexpr int optval = 1;
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));
}

    ///@thread 安全
bool Server::checkServerNotFull(const int fd) {
    if (http::Conn::userCount.load(std::memory_order_release) >= MAX_FD) {
        sendError(fd, "Server busy!");
        LOG_W("Clients full! Current user count: {}.",
              http::Conn::userCount.load());
        return false;
    }
    return true;
}

void Server::dealListen() {
    struct sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    if (_listenEvent & EPOLLET) { // ET 模式：必须处理所有就绪连接
        while (!_isClose.load(std::memory_order_acquire)) {
            const int fd = accept(
                _listenFd, reinterpret_cast<struct sockaddr*>(&addr), &len);
            if (fd <= 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break; // 没有更多连接可接受
                }
                LOG_E("Listen and accept error: {}", strerror(errno));
                break;
            }
            if (_isClose.load(std::memory_order_acquire)) {
                close(fd); // TODO check
                break;
            }
            if (checkServerNotFull(fd)) {
                addClient(fd, addr);
            }
        }
    } else { // LT 模式：限制单次 accept 数量
        const int maxAccept =
            std::min(50, MAX_FD - http::Conn::userCount.load());
        for (int i = 0; i < maxAccept; ++i) {
            const int fd = accept(
                _listenFd, reinterpret_cast<struct sockaddr*>(&addr), &len);
            if (fd <= 0) {
                if (errno == EAGAIN)
                    break;
                LOG_E("Listen and accept error: {}", strerror(errno));
                break;
            }
            if (checkServerNotFull(fd)) {
                addClient(fd, addr);
            }
        }
    }
}

void Server::dealRead(http::Conn* client) {
    assert(client);
    int fd = client->GetFd();
    assert(fd > 0);
    if (fd <= 0) {
        LOG_W("Invalid fd {}!", fd);
        return;
    }
    if (_users.count(fd) == 0) {
        LOG_W("Fd {} is not in _users!", fd);
        return;
    }
    extentTime(client); // TODO 两种计时器处理差异的本质所在
    _threadpool->AddTask([this, client] {
        onRead(client);
    });
}

void Server::dealWrite(http::Conn* client) {
    assert(client);
    int fd = client->GetFd();
    if (fd <= 0) {
        LOG_E("Invalid fd {}!", fd);
        return;
    }
    if (_users.count(fd) == 0) {
        LOG_E("Fd {} not found in _users!", fd);
        return;
    }
    extentTime(client);
    _threadpool->AddTask([this, client] {
        onWrite(client);
    });
}

void Server::extentTime(const http::Conn* client) {
    assert(client);
    std::shared_lock locker(this->_connMutex, std::defer_lock);
    if (_timeoutMS <= 0) {
        return;
    }
    int fd = client->GetFd();
    if (fd <= 0) {
        LOG_W("Invalid fd:{}!", fd);
        return;
    }
    locker.lock();
    if (_users.count(fd) == 0) {
        LOG_W("Fd not found in _users!", fd);
        return;
    }
    uint64_t connId = _users.at(fd).connId;
    locker.unlock();
    /*
        使用ScheduleWithKey，确保每个文件描述符只有一个定时器
        webserver 11 里只调用了一个 timer_->adjust
    */
    TimerManagerImpl::GetInstance().ScheduleWithKey(
        fd, _timeoutMS, 0, [this, fd, connId]() {
            if (_isClose) {
                LOG_D("Timer callback aborted: server is closing.");
                return;
            }
            const http::Conn* conn = nullptr;
            {
                std::shared_lock lk(this->_connMutex);
                if (_users[fd].connId != connId) {
                    LOG_D("ConnId mismatch for fd {} (expected {}, found {}).",
                          fd, connId, _users[fd].connId);
                    return;
                }
                conn = _users[fd].conn.get();
            }
            if(conn) {
                this->closeConn(conn);
            }
        });
}

void Server::onRead(http::Conn* client) {
    assert(client);
    int fd = client->GetFd();
    if (fd <= 0) {
        LOG_W("Invalid fd {}!", fd);
        return;
    }
    {
        std::shared_lock locker(_connMutex);
        if (_users.count(fd) == 0) {
            LOG_W("Fd {} not found in _users map!", fd);
            return;
        }
        if (uint64_t connId = client->GetConnId(); _users.at(fd).connId != connId) {
            LOG_W("Fd {} has mismatched connId (expected {}, got {}).", fd,
                _users.at(fd).connId, connId);
            return;
        }
    }
    ssize_t ret = -1;
    int readErrno = 0;
    ret = client->Read(&readErrno);
    if (ret <= 0 && readErrno != EAGAIN) { // 读取出错或连接关闭
        closeConn(client);
        return;
    }
    if (ret > 0) { // 只有当确实读取到数据时才处理请求
        onProcess(client);
    } else {
        // 如果没有数据，重新注册 EPOLLIN 事件 --原版没有这一步处理
        if (!_epoller->ModFd(fd, _connEvent | EPOLLIN)) {
            LOG_E("Failed to mod fd {} for EPOLLIN!", fd);
        }
    }
}

void Server::onProcess(http::Conn* client) {
    assert(client);
    int fd = client->GetFd();
    if (fd <= 0) {
        LOG_W("Invalid fd {}!", fd);
        return;
    }
    {
        std::shared_lock locker(_connMutex);
        if (uint64_t connId = client->GetConnId(); _users.at(fd).connId != connId) {
            LOG_W("Fd {} has mismatched connId (expected {}, got {}).",
            fd, _users.at(fd).connId, connId);
            return;
        }
    }
    if (client->Process()) {
        if (!_epoller->ModFd(fd, _connEvent | EPOLLOUT)) {
            LOG_E("Failed to mod fd {}!", fd);
        }
    } else {
        if (!_epoller->ModFd(fd, _connEvent | EPOLLIN)) { // ?
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
    uint64_t connId = client->GetConnId();
    {
        std::shared_lock locker(_connMutex);
        if (_users.at(fd).connId != connId) {
            LOG_W("{}: fd {} has mismatched connId (expected {}, got {})",
                  __FUNCTION__, fd, _users.at(fd).connId, connId);
            return;
        }
    }
    int ret = -1;
    int writeErrno = 0;
    ret = static_cast<int>(client->Write(&writeErrno));
    extentTime(client);
    if (client->ToWriteBytes() == 0) { // 传输完成 TODO 长连接的其他处理
        if (client->IsKeepAlive()) {
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