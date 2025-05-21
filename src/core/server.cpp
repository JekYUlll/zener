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

    char cwdBuff[256];           // 程序所在路径
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
            false,
            std::
                memory_order_release);

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
    LOG_T("🚀--------------------------------+--");
    LOG_I("|   __________ _   _ _____ ____");
    LOG_I("|  |__  / ____| \\ | | ____|  _ \\");
    LOG_I("|    / /|  _| |  \\| |  _| | |_) |");
    LOG_I("|   / /_| |___| |\\  | |___|  _ <");
    LOG_I("|  /____|_____|_| \\_|_____|_| \\_\\");
    LOG_T("🚀--------------------------------+--");
    LOG_I("| 󰩟 port: {}, OpenLinger: {}", port, optLinger ? "true" : "false");
    LOG_I("|  Listen Mode: {}, OpenConn Mode: {}",
          (_listenEvent & EPOLLET ? "ET" : "LT"),
          (_connEvent & EPOLLET ? "ET" : "LT"));
    LOG_I("|  static path: {}", http::Conn::staticDir);
    LOG_I("| 󰰙 SqlConnPool num: {}, ThreadPool num: {}", connPoolNum, threadNum);
    LOG_I("| 󰔛 TimerManager: {}", TIMER_MANAGER_TYPE);
    LOG_T("-------------------------------------+--");
}

Server::~Server() {
    close(_listenFd);
    _isClose.store(true, std::memory_order_release);
    db::SqlConnector::GetInstance().Close();
    LOG_I("Server exited.");
    Logger::Flush();
    Logger::Shutdown();
}

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
void Server::Run() {
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
            if (fd <= 0 || fd > MAX_FD) {
                LOG_W("Invalid fd: {} from epoll!", fd);
                continue;
            }
            if (fd == _listenFd) { // @Listen
                dealListen();
            } else if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                /*
                 *TODO 想出来的比较阴间的操作：
                 *如果不存在于 _users map 中（只会在错误下出现）：
                 *会调用 ConnInfo 的默认构造函数
                 *则 conn 会是 nullptr，connId 是 0
                 *但下次调用会查找到对象，且为默认初始化的不合法对象。
                 *所以在其他地方要进行判断。
                 */
                readLocker.lock();
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
                } else {
                    /* 和原版不一样的处理：
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
///@todo 能否检测重复关闭？
///@thread 安全
void Server::closeConn(http::Conn* client) {
    assert(client);
    if (!client) {
        LOG_W("Client is null!");
        return;
    }
    std::unique_lock writeLocker(_connMutex, std::defer_lock);
    int fd = client->GetFd();
    assert(fd > 0);
    uint64_t connId = client->GetConnId();
    if (fd <= 0 || fd > MAX_FD || connId == 0) {
        LOG_W("Closing invalid fd: {}, connId: {}! remove from _users.", fd,
              connId);
        writeLocker.lock();
        _users.erase(fd); // *
        writeLocker.unlock();
        return;
    }
    if constexpr (false) {
        /*
         * TODO 删除定时器里预订的超时关闭操作，因为此函数会在此刻提前删除？
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
    if (!_epoller->DelFd(fd)) {
        LOG_E("Failed to del fd {}, connId {} from epoll!", fd, connId);
    }
    writeLocker.lock();
    if (const auto it = _users.find(fd); it != _users.end()) {
        it->second.active.store(false, std::memory_order_release);
        it->second.connId = 0;
        /*
         *是否需要？ 原方案里没有close
         *如果此处close ，25万请求中会有100左右失败
         */
        if constexpr (false) {
            close(fd);
        }
        /*
            因为 _users 的值是 ConnInfo 的实例，
            所以会调用 ConnInfo 的析构函数，析构函数里调用 Close
            原方案里是不将其从 _users 中删除，而是手动调用 Close
            下一次 addClient 在此处重新调用 _users[fd].Init()
            相当于生成一个新的连接
        */
        _users.erase(fd);
    }
    writeLocker.unlock();

    /*
        ERROR 严重 CLOSE问题
       *  之前一直忘了调用 Close() --> 调用之后反而宕机 不是段错误，而是信号
          此处调用 client->Close() 之后连接反而不会退出，导致迅速占满，程序退出
          -- 添加了 Conn 析构函数里对 Close 的判断，发现果然都是在析构函数里退出
              而不是在定时器里设置的超时任务里。可能是计时器实现有误
              也可能是 move 或者 传递智能指针的裸指针，导致 conn
       提前析构，而裸指针还存在 加上 IsClosed() 检查，出现严重的[server.cpp:470
       ] - Duplicate fd 2515 detected! 2025/03/01
          把触发的地方，将重复的fd从_users里erase掉，报错解决。
          但是每次bench会有几个请求失败，怀疑占用的性能也更高。最后users
       count会减成负数
    */
    if constexpr (false) {
        if (client) {
            if (!client->IsClosed()) {
                client->Close();
            }
        }
    }
}

///@intro 弃用
///@change 没必要传入fd，传入 conn 就够了
///@praram 右值的conn
///@thread 安全
void Server::_closeConnInternal(http::Conn&& client) const {
    auto fd = client.GetFd();
    assert(fd > 0);
    if (fd <= 0) {
        return;
    }
    auto connId = client.GetConnId();
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
///@intro 弃用
void Server::closeConnAsync(int fd, const std::function<void()>& callback) {
    ConnInfo connInfoCopy{};
    {
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
    if (fd <= 0) {
        LOG_E("Invalid fd: {}!", fd);
        return;
    }
    std::unique_lock writeLock(_connMutex, std::defer_lock);
    if (setNoDelay(fd) < 0) { // NO_DELAY
        LOG_W("Failed to set TCP_NODELAY for client fd {}: {}", fd,
              strerror(errno));
    }
    uint64_t connId = _nextConnId.fetch_add(
        1, std::memory_order_acquire); // 为新连接生成唯一ID
    try {
        /*
            把 fd 添加进 _users 表，并且生成对应的 conn
            conn初始化 防止出错被覆盖
        */
        if constexpr (true) {
            writeLock.lock();
            // 当键存在时不执行操作，否则插入
            auto [it, inserted] = _users.try_emplace(fd);
            assert(inserted);
            if (!inserted) {
                /*
                 * TODO ERROR 严重
                 * TODO 重新连接仍然被占用，导致无法连接
                 * fd 被快速复用，而旧的连接信息未及时从 _users 中清理干净
                 * deepseek说必须立即关闭 fd！否则会导致 文件描述符泄漏
                 * 测了一下发现并不会
                 * 是否在什么地方忘了从 _users 里 erase？
                 * 如果在 closeConn 里调用 client->Close，这里就会阻塞触发
                 */
                LOG_E("Duplicate fd {} detected!", fd);
                _users.erase(fd); // new
                close(fd);        // 没啥影响
                return;
            }
            auto& connInfo = it->second;
            auto conn = std::make_unique<http::Conn>();
            /*
             *TODO 此处为业务id赋值
             *实际上添加计时器等，用的是外层 ConnInfo 里的。这里似乎意义不明
             */
            conn->SetConnId(connId);
            conn->Init(fd, addr);
            connInfo.connId = connId;
            connInfo.conn = std::move(conn);
            writeLock.unlock();
        } else {
            // 测试使用简单逻辑 使用.at()会抛异常
            // 以下逻辑直接卡死，0连接
            writeLock.lock();
            _users[fd].connId = connId;
            _users[fd].conn->Init(fd, addr);
            _users[fd].conn->SetConnId(connId);
            writeLock.unlock();
        }

    } catch (const std::exception& e) {
        LOG_E("Add client exception. fd:{}. id:{}. {}", fd, connId, e.what());
    }
    /*
     * 设置超时取消 此处传入 connId 和 fd
     * 在定时器回调中使用 connId 进行校验
     */
    if (_timeoutMS > 0) {
        TimerManagerImpl::GetInstance().ScheduleWithKey(
            fd, _timeoutMS, 0, [this, fd, connId]() {
                /*
                 * 检查文件描述符和连接ID都匹配
                 * 防止文件描述符重用导致的错误关闭
                 */
                assert(fd > 0);
                assert(connId > 0);
                http::Conn* conn = nullptr;
                {
                    std::shared_lock readLock(_connMutex, std::defer_lock);
                    if (!_isClose.load() && _users.count(fd) > 0 &&
                        _users[fd].connId == connId) {
                        conn = _users[fd].conn.get();
                    }
                }
                if (conn) {
                    // 包含删除 _users 表中的 fd
                    closeConn(conn);
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
    }
    LOG_T("Set client({}) id:{}.", fd, connId);
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
    if (http::Conn::userCount.load(std::memory_order_acquire) >= MAX_FD) {
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

///@thread 安全
void Server::dealRead(http::Conn* client) {
    assert(client);
    if (!client) {
        return;
    }
    int fd = client->GetFd();
    assert(fd > 0);
    if (fd <= 0) {
        LOG_W("Invalid fd {}!", fd);
        return;
    }
    std::shared_lock readLocker(_connMutex, std::defer_lock);
    readLocker.lock();
    const size_t ret = _users.count(fd);
    readLocker.unlock();
    if (ret == 0) {
        LOG_W("Fd {} is not in _users!", fd);
        return;
    }
    extentTime(client); // TODO 两种计时器处理差异的本质所在
    // client 是值捕获。引用捕获很容易崩溃
    _threadpool->AddTask([this, client] { onRead(client); });
}

///@thread 安全
void Server::dealWrite(http::Conn* client) {
    assert(client);
    if (!client) {
        return;
    }
    int fd = client->GetFd();
    if (fd <= 0) {
        LOG_E("Invalid fd {}!", fd);
        return;
    }
    std::shared_lock readLocker(_connMutex, std::defer_lock);
    readLocker.lock();
    const size_t ret = _users.count(fd);
    readLocker.unlock();
    if (ret == 0) {
        LOG_E("Fd {} not found in _users!", fd);
        return;
    }
    extentTime(client);
    _threadpool->AddTask([this, client] { onWrite(client); });
}

///@thread 安全
void Server::extentTime(http::Conn* client) {
    assert(client);
    if (!client) {
        return;
    }
    std::shared_lock readLocker(this->_connMutex, std::defer_lock);
    if (_timeoutMS <= 0) {
        return;
    }
    int fd = client->GetFd();
    if (fd <= 0) {
        LOG_W("Invalid fd:{}!", fd);
        return;
    }
    readLocker.lock();
    if (_users.count(fd) == 0) {
        LOG_W("Fd not found in _users!", fd);
        return;
    }
    uint64_t connId = _users.at(fd).connId;
    readLocker.unlock();
    /*
        使用ScheduleWithKey，确保每个文件描述符只有一个定时器
        webserver 11 里只调用了一个 timer_->adjust
    */
    // TODO 此处可能有重复关闭的问题？又添加了一个新的定时器
    // 没有细看Timer实现，不知道原本的有没有取消
    // TimerManagerImpl::GetInstance().CancelByKey(connId); // TODO ??
    TimerManagerImpl::GetInstance().ScheduleWithKey(
        fd, _timeoutMS, 0, [this, fd, connId]() {
            if (_isClose.load(std::memory_order_acquire)) {
                LOG_D("Timer callback aborted: server is closing.");
                return;
            }
            http::Conn* conn = nullptr;
            {
                std::shared_lock lk(this->_connMutex);
                if (_users[fd].connId != connId) {
                    LOG_D("ConnId mismatch for fd {} (expected {}, found {}).",
                          fd, connId, _users[fd].connId);
                    return;
                }
                conn = _users[fd].conn.get();
            }
            if (conn) {
                if (conn->IsClosed()) {
                    this->closeConn(conn);
                }
            }
        });
}

///@err 错误处理
/// 仅打印
void Server::handleReadError(http::Conn* client, int err) {
    const int fd = client->GetFd();
    switch (err) {
    case ECONNRESET:
        LOG_W("Connection reset by peer: fd={}", fd);
        break;
    case EBADF:
        LOG_W("Invalid fd={} detected", fd);
        break;
    default:
        LOG_W("Unknown error {} on fd={}", err, fd);
    }
    closeConn(client);
}

///@thread 工作线程在线程池里调用
void Server::onRead(http::Conn* client) {
    assert(client);
    const int fd = client->GetFd();
    if (!checkFdAndMatchId(client)) {
        LOG_W("Not match!");
        return;
    }
    int readErrno = 0;
    if (const ssize_t ret = client->Read(&readErrno); ret == 0) {
        /*
        经常收到 104 应该是ECONNRESET，即对端重置了连接
        read返回0表示对端正常关闭连接（发送了FIN），此时应视为正常关闭，而不是错误
        */
        LOG_D("Shutdown on fd={}.", fd); // 对端关闭
        closeConn(client);
        return;
    } else if (ret < 0) {
        /*
            EWOULDBLOCK 和 EAGAIN 的值一样，本质上是同一个错误
            非阻塞模式下，数据尚未准备好，需等待下次事件通知
            原版放在 onProcess 中处理
            此实现在 client->Read 里也进行了处理，但是没有调整fd。
            放在handleEagain中具体处理。
        */
        if (readErrno == EAGAIN || readErrno == EWOULDBLOCK) {
            LOG_D("Shutdown on fd={}.", fd);
            if (!_epoller->ModFd(fd, EPOLLIN)) {
                LOG_E("Failed to re-arm EPOLLIN on fd={}!", fd);
                closeConn(client);
            }
        }
        handleReadError(client, readErrno);
        return;
    }
    // ret > 0 只有当确实读取到数据时才处理请求
    onProcess(client);
}

///@thread 工作线程在线程池里调用
void Server::onProcess(http::Conn* client) {
    assert(client);
    if (!checkFdAndMatchId(client)) {
        LOG_E("Not match id!");
        return;
    }
    int fd = client->GetFd();
    switch (auto result = client->Process()) {
    case http::Conn::ProcessResult::NEED_MORE_DATA:
        /* 重新注册EPOLLIN */
        if (!_epoller->ModFd(fd, _connEvent | EPOLLIN)) {
            LOG_E("Failed to mod fd {}! {}", fd, strerror(errno));
            closeConn(client);
        }
        break;
    case http::Conn::ProcessResult::OK:
        /* 处理成功，注册EPOLLOUT等待可写事件*/
        if (!_epoller->ModFd(fd, _connEvent | EPOLLOUT)) {
            LOG_E("Failed to mod fd {}! {}", fd, strerror(errno));
            closeConn(client);
        }
        break;
    case http::Conn::ProcessResult::RETRY_LATER:
        // TODO
        break;
    case http::Conn::ProcessResult::ERROR:
        LOG_W("Failed to process fd {}! {}", fd, strerror(errno));
        closeConn(client);
        break;
    }
}

///@thread 工作线程在线程池里调用
void Server::onWrite(http::Conn* client) {
    assert(client);
    int fd = client->GetFd();
    if (!checkFdAndMatchId(client)) {
        LOG_E("Not match id!");
        return;
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
        LOG_E("Write err: fd:{}, connId:{}, errno:{}.", fd, client->GetConnId(),
              writeErrno);
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

bool Server::checkFdAndMatchId(const http::Conn* client) const {
    int fd = client->GetFd();
    assert(fd > 0);
    if (fd <= 0 || fd > MAX_FD) {
        LOG_W("Invalid fd {}!", fd);
        return false;
    }
    if (client->IsClosed()) {
        LOG_W("Conn closed!");
        return false;
    }
    {
        std::shared_lock locker(_connMutex);
        if (_users.count(fd) == 0) {
            LOG_W("No such fd {} in _users!", fd);
            return false;
        }
        if (uint64_t connId = client->GetConnId();
            connId == 0 || _users.at(fd).connId != connId) {
            LOG_W("Fd {} has mismatched connId (expected {}, got {}).", fd,
                  _users.at(fd).connId, connId);
            return false;
        }
    }
    return true;
}

} // namespace v0

std::unique_ptr<v0::Server> NewServerFromConfig(const std::string& configPath) {
    // TODO 如果没有配置文件，生成一份默认配置文件
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