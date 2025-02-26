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
    int timeMS = -1; // -1 means wait indefinitely
    if (!_isClose) {
        LOG_I("========== Server start ==========");
        LOG_I("listen fd: {}, thread pool: {:p}", _listenFd,
              (void*)_threadpool.get());
    }

    // 创建一个后台线程，定期清理文件缓存
    _threadpool->AddTask([this]() {
        const int FILE_CACHE_CLEANUP_INTERVAL_SEC = 300; // 每5分钟清理一次
        while (!_isClose) {
            // 清理超过120秒未使用的文件缓存
            try {
                LOG_I("开始执行定期文件缓存清理...");
                zener::http::FileCache::GetInstance().CleanupCache(120);
                LOG_I("文件缓存清理完成");
            } catch (const std::exception& e) {
                LOG_E("文件缓存清理异常: {}", e.what());
            }
            // 等待一段时间再次清理
            std::this_thread::sleep_for(
                std::chrono::seconds(FILE_CACHE_CLEANUP_INTERVAL_SEC));
        }
    });

    while (!_isClose) {
        if (_timeoutMS > 0) {
            timeMS = _timeoutMS;
        }

        const int eventCnt = _epoller->Wait(timeMS);
        if (eventCnt == 0) {
            // TODO log timeout & timer tick
            continue;
        }

        for (int i = 0; i < eventCnt; i++) {
            const int fd = _epoller->GetEventFd(i);
            const uint32_t events = _epoller->GetEvents(i);

            if (fd == _listenFd) {
                dealListen();
            } else if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                assert(_users.count(fd) > 0);
                closeConn(&_users[fd].conn);
            } else if (events & EPOLLIN) {
                assert(_users.count(fd) > 0);
                dealRead(&_users[fd].conn);
            } else if (events & EPOLLOUT) {
                assert(_users.count(fd) > 0);
                dealWrite(&_users[fd].conn);
            } else {
                LOG_W("Unexpected event");
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
    LOG_I("Server shutdown==========================>");

    try {
        // 首先标记服务器为已关闭状态，确保所有线程都知道要退出
        _isClose = true;

        // 停止定时器管理器，这很重要，确保定时器线程能快速退出
#ifdef __USE_MAPTIMER
        try {
            LOG_I("停止MAP定时器管理器...");
            zener::rbtimer::TimerManager::GetInstance().Stop();
        } catch (const std::exception& e) {
            LOG_E("停止MAP定时器时出错: {}", e.what());
        }
#else
        try {
            LOG_I("停止HEAP定时器管理器...");
            zener::v0::TimerManager::GetInstance().Stop();
        } catch (const std::exception& e) {
            LOG_E("停止HEAP定时器时出错: {}", e.what());
        }
#endif

        // 关闭监听socket，阻止新连接
        if (_listenFd >= 0) {
            int fd = _listenFd;
            _listenFd = -1; // 先置为-1，避免重复关闭
            close(fd);
            LOG_I("关闭监听socket(fd={})成功", fd);
        }

        // 优先关闭所有连接，使用较短的超时时间
        const int connTimeoutMS =
            timeoutMS > 0 ? std::min(timeoutMS / 2, 2000) : 2000;
        // 计算连接关闭超时时间点
        auto connStart = std::chrono::high_resolution_clock::now();
        auto connTimeout = std::chrono::milliseconds(connTimeoutMS);
        auto connEnd = connStart + connTimeout;

        // 关闭所有现有连接
        if (!_users.empty()) {
            LOG_I("正在关闭 {} 个活跃连接...", _users.size());
            // 复制fd列表，避免关闭过程中修改map
            std::vector<int> fds;
            fds.reserve(_users.size());
            for (const auto& [fd, _] : _users) {
                fds.push_back(fd);
            }

            for (int fd : fds) {
                if (_users.count(fd) > 0) {
                    closeConn(&_users[fd].conn);
                }

                // 检查超时，避免连接关闭过程阻塞太久
                if (std::chrono::high_resolution_clock::now() > connEnd) {
                    LOG_W("连接关闭操作超时({}ms)，还有{}个连接未处理",
                          connTimeoutMS, _users.size());
                    break;
                }
            }

            // 如果仍有连接未关闭，强制清空
            if (!_users.empty()) {
                LOG_W("强制清空剩余的{}个连接", _users.size());
                _users.clear();
            }
        } else {
            LOG_I("没有活跃连接需要关闭");
        }

        // 关闭线程池
        if (_threadpool) {
            LOG_I("正在关闭线程池...");
            // 给线程池一半的超时时间，但不超过3秒
            const int poolTimeoutMS =
                timeoutMS > 0 ? std::min(timeoutMS / 2, 3000) : 3000;
            _threadpool->Shutdown(poolTimeoutMS);
        }

        // 关闭数据库连接
        try {
            LOG_I("关闭数据库连接...");
            db::SqlConnector::GetInstance().Close();
        } catch (const std::exception& e) {
            LOG_E("关闭数据库连接时出错: {}", e.what());
        }

    } catch (const std::exception& e) {
        LOG_E("服务器关闭过程中发生异常: {}", e.what());
    } catch (...) {
        LOG_E("服务器关闭过程中发生未知异常");
    }

    LOG_I("服务器关闭完成");
    Logger::Flush();
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

    LOG_I("Closing connection: fd={}, connId={}", fd, connId);

    // 1. 先从定时器映射中删除该fd关联的定时器
    if (_timeoutMS > 0) {
        try {
            // 使用TimerManagerImpl而不是具体实现类
            TimerManagerImpl::GetInstance().CancelByKey(fd);
        } catch (const std::exception& e) {
            LOG_E("Exception when canceling timer for fd {}, connId {}: {}", fd,
                  connId, e.what());
        } catch (...) {
            LOG_E("Unknown exception when canceling timer for fd {}, connId {}",
                  fd, connId);
        }
    }

    // 2. 从epoll中删除文件描述符
    if (!_epoller->DelFd(fd)) {
        LOG_E("Failed to delete client fd [{0} - {1} {2}], connId {3}!", fd,
              client->GetIP(), client->GetPort(), connId);
    }

    // 3. 关闭连接
    client->Close();

    // 4. 从_users映射中删除此连接
    _users.erase(fd);
    LOG_D("Connection removed from users map: fd={}, connId={}", fd, connId);
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

    if (!_epoller->AddFd(fd, EPOLLIN | _connEvent)) {
        LOG_E("Failed to add client fd {}!", fd);
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
            LOG_D("Reached maximum accept count per call ({}), will continue "
                  "in next cycle",
                  MAX_ACCEPT_PER_CALL);
            break;
        }
    } while (_listenEvent & EPOLLET);
}

void Server::dealRead(http::Conn* client) const {
    assert(client);
    extentTime(client);
    int fd = client->GetFd();

    // 获取当前连接的ID
    uint64_t connId = 0;
    if (_users.count(fd) > 0) {
        connId = _users[fd].connId;
    } else {
        LOG_W("dealRead called for fd {} but not found in _users", fd);
        return;
    }

#ifdef __V0
    _threadpool->AddTask([this, fd, connId] {
        // 检查连接是否仍然存在且ID匹配
        if (!_isClose && _users.count(fd) > 0 && _users[fd].connId == connId) {
            onRead(&_users[fd].conn);
        }
    });
#elif
// TODO 线程池替换
#endif // __V0
}

void Server::dealWrite(http::Conn* client) const {
    assert(client);
    extentTime(client);
    int fd = client->GetFd();

    // 获取当前连接的ID
    uint64_t connId = 0;
    if (_users.count(fd) > 0) {
        connId = _users[fd].connId;
    } else {
        LOG_W("dealWrite called for fd {} but not found in _users", fd);
        return;
    }

#ifdef __V0
    _threadpool->AddTask([this, fd, connId] {
        // 检查连接是否仍然存在且ID匹配
        if (!_isClose && _users.count(fd) > 0 && _users[fd].connId == connId) {
            onWrite(&_users[fd].conn);
        }
    });
#elif
// TODO 线程池替换
#endif // __V0
}

void Server::extentTime(const http::Conn* client) const {
    assert(client);
    if (_timeoutMS > 0) {
        int fd = client->GetFd();

        // 查找当前连接的ID
        uint64_t connId = 0;
        if (_users.count(fd) > 0) {
            connId = _users[fd].connId;
        } else {
            LOG_W("extentTime called for fd {} but not found in _users", fd);
            return;
        }

        // 使用ScheduleWithKey，确保每个文件描述符只有一个定时器
        TimerManagerImpl::GetInstance().ScheduleWithKey(
            client->GetFd(), _timeoutMS, 0, [this, fd, connId]() {
                // 检查文件描述符和连接ID都匹配
                if (!_isClose && _users.count(fd) > 0 &&
                    _users[fd].connId == connId) {
                    this->closeConn(&this->_users[fd].conn);
                }
            });
    }
}

// 干什么用？
void Server::onRead(http::Conn* client) const {
    assert(client);
    int ret = -1;
    int readErrno = 0;
    int fd = client->GetFd();
    ret = client->read(&readErrno);
    if (ret <= 0 && readErrno != EAGAIN) {
        closeConn(client);
        return;
    }
    onProcess(client);
}

void Server::onProcess(http::Conn* client) const {
    assert(client);
    int fd = client->GetFd();

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

void Server::onWrite(http::Conn* client) const {
    assert(client);
    int ret = -1;
    int writeErrno = 0;
    int fd = client->GetFd();

    ret = client->write(&writeErrno);
    if (client->ToWriteBytes() == 0) { // 传输完成
        if (client->IsKeepAlive()) {
            onProcess(client);
            return;
        }
    } else if (ret < 0) {
        if (writeErrno == EAGAIN) { // 继续传输
            if (!_epoller->ModFd(fd, _connEvent | EPOLLOUT)) {
                LOG_E("Failed to mod fd {}!", fd);
            }
            return;
        }
    }
    closeConn(client);
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

    LOG_I("Socket initialized with optimized settings - Listen backlog: {}, "
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