#include "core/server.h"
#include "config/config.h"
#include "core/epoller.h"
#include "database/sql_connector.h"
#include "http/conn.h"
#include "task/threadpool_1.h"
#include "task/timer/heaptimer.h"
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
#include <sys/epoll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace zws {
namespace v0 {

Server::Server(int port, const int trigMode, const int timeoutMS,
               const bool optLinger, const int sqlPort, const char* sqlUser,
               const char* sqlPwd, const char* dbName, int connPoolNum,
               int threadNum, bool openLog, int logLevel, int logQueSize)
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

    db::SqlConnector::GetInstance().Init("localhost", sqlPort, sqlUser, dbName,
                                         dbName, connPoolNum);

    initEventMode(trigMode);

    if (!initSocket()) {
        _isClose = true;
    }
    // TODO
    // 检查 log 是否初始化，如果没有，直接不打印 LOG
    // 或者控制是否打印至文件
    // TODO 修改 log 类，使其自动拼接，并且存储一个 dir。 log.name
    // 此处是手动拼接的
    // 需要配置完整的路径
    const auto logDir = GET_CONFIG("log.dir");   // 使用 dir 而不是 path
    const auto logName = GET_CONFIG("log.name"); // "test.log"
    // 使用绝对路径，确保日志文件创建在正确的位置
    const std::string logPath = _cwd + "/" + logDir + "/" + logName;
    if (mkdir((_cwd + "/" + logDir).c_str(), 0777) != 0 && errno != EEXIST) {
        LOG_E("Failed to create log directory: {}", _cwd + "/" + logDir);
        return;
    }
    if (!Logger::WriteToFile(logPath)) {
        LOG_E("Failed to create log file: {}", logPath);
        return;
    }
    // if (!zws::Logger::WriteToFile(fullLogPath)) {
    //     LOG_E("Failed to create log file: {}!", fullLogPath);
    // }

    // init info
    LOG_I("=====================Server Init=====================");
    LOG_I("port: {0}, OpenLinger: {1}", port, optLinger ? "true" : "false");
    LOG_I("Listen Mode: {0}, OpenConn Mode: {1}",
          (_listenEvent & EPOLLET ? "ET" : "LT"),
          (_connEvent & EPOLLET ? "ET" : "LT"));
    LOG_I("LogSys level: {}", logLevel);
    LOG_I("srcDir: {}", http::Conn::staticDir);
    LOG_I("SqlConnPool num: {0}, ThreadPool num: {1}", connPoolNum, threadNum);
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
void Server::initEventMode(int trigMode) {
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
    int timeMS = -1; /* epoll wait timeout == -1 无事件将阻塞 */
    if (!_isClose) {
        LOG_I("Server start at {}",
              _port); // 此处{}里写成 1，出了个 spdlog 的运行时 bug
        while (!_isClose) {
            if (_timeoutMS > 0) {
#ifdef __V0
                timeMS = HeapTimerManager::GetInstance().GetNextTick();
#elif
                // TODO
#endif // __V0
            }

            // 限制timeMS最大值，确保即使没有事件，epoll_wait也能定期返回检查_isClose标志
            if (timeMS < 0 || timeMS > 1000) {
                timeMS = 1000; // 最多阻塞1秒
            }

            const int eventCnt = _epoller->Wait(timeMS);

            // 检查是否需要退出
            if (_isClose) {
                break;
            }

            for (int i = 0; i < eventCnt; i++) {
                /* 处理事件 */
                int fd = _epoller->GetEventFd(i);
                uint32_t events = _epoller->GetEvents(i);
                if (fd == _listenFd) {
                    dealListen();
                } else if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    // 关闭
                    if (_users.count(fd) > 0) {
                        closeConn(&_users[fd]);
                    } else {
                        LOG_W("文件描述符 {} 不存在于 _users "
                              "中，但收到了关闭事件",
                              fd);
                        // 确保从epoll中移除
                        _epoller->DelFd(fd);
                    }
                } else if (events & EPOLLIN) {
                    // 读
                    if (_users.count(fd) > 0) {
                        dealRead(&_users[fd]);
                    } else {
                        LOG_W("文件描述符 {} 不存在于 _users "
                              "中，但收到了读取事件",
                              fd);
                        // 确保从epoll中移除
                        _epoller->DelFd(fd);
                    }
                } else if (events & EPOLLOUT) {
                    // 写
                    if (_users.count(fd) > 0) {
                        dealWrite(&_users[fd]);
                    } else {
                        LOG_W("文件描述符 {} 不存在于 _users "
                              "中，但收到了写入事件",
                              fd);
                        // 确保从epoll中移除
                        _epoller->DelFd(fd);
                    }
                } else {
                    LOG_E("unexpected event: {}", events);
                }

                // 再次检查是否需要退出
                if (_isClose) {
                    break;
                }
            }
        }
    }
}

void Server::Stop() {
    close(_listenFd);
    db::SqlConnector::GetInstance().Close();
    _isClose = true;
    LOG_I("=====================Server stop=====================");
    Logger::Flush();
    Logger::Shutdown();
}

void Server::Shutdown(int timeout_ms) {
    // 1. 设置关闭标志，停止接受新连接
    LOG_I("=====================Server graceful shutdown "
          "initiated=====================");
    _isClose = true;
    close(_listenFd);
    LOG_I("Server shutting down, stopping to accept new connections");

    // 2. 等待所有连接关闭或超时
    int wait_time = 0;
    const int check_interval = 100; // 每100ms检查一次
    int initial_conn_count = _users.size();

    if (initial_conn_count > 0) {
        LOG_I("Waiting for {} active connections to close", initial_conn_count);

        while (!_users.empty() && wait_time < timeout_ms) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(check_interval));
            wait_time += check_interval;

            // 每秒打印一次剩余连接数
            if (wait_time % 1000 == 0) {
                LOG_I("Still waiting: {} connections remaining, elapsed: {}ms",
                      _users.size(), wait_time);
            }
        }

        // 记录关闭情况
        if (!_users.empty()) {
            LOG_W("Server shutdown timed out, {} connections still active",
                  _users.size());

            // 强制关闭剩余连接 - 修复复制问题
            // 不要复制整个映射，而是复制所有键（文件描述符）
            std::vector<int> fds_to_close;
            for (const auto& [fd, _] : _users) {
                fds_to_close.push_back(fd);
            }

            // 使用复制的文件描述符访问原始 _users 中的连接
            for (int fd : fds_to_close) {
                if (_users.count(fd) > 0) { // 确保连接仍然存在
                    closeConn(&_users[fd]);
                }
            }
        } else {
            LOG_I("All {} connections closed successfully", initial_conn_count);
        }
    } else {
        LOG_I("No active connections to close");
    }

    // 3. 关闭数据库连接和日志
    db::SqlConnector::GetInstance().Close();
    LOG_I("=====================Server shutdown complete=====================");
    Logger::Flush();
    Logger::Shutdown();
}

void Server::sendError(int fd, const char* info) {
    assert(fd > 0);
    if (const int ret = send(fd, info, strlen(info), 0); ret > 0) {
        LOG_W("send error to client {} error!", fd);
    }
    close(fd);
}

void Server::closeConn(http::Conn* client) const {
    assert(client);
    int fd = client->GetFd();
    LOG_I("client {} quit.", fd);

    // 1. 先从定时器映射中删除该fd关联的定时器
    if (_timeoutMS > 0) {
        try {
            HeapTimerManager::GetInstance().CancelByKey(fd);
        } catch (const std::exception& e) {
            LOG_E("Exception when canceling timer for fd {}: {}", fd, e.what());
        } catch (...) {
            LOG_E("Unknown exception when canceling timer for fd {}", fd);
        }
    }

    // 2. 从epoll中删除文件描述符
    if (!_epoller->DelFd(fd)) {
        LOG_E("Failed to delete client fd {}!", fd);
    }

    // 3. 关闭连接
    client->Close();

    // 4. 从_users映射中删除此连接
    // 由于closeConn是const方法，需要进行const_cast来修改_users
    auto& users = const_cast<std::unordered_map<int, http::Conn>&>(_users);
    users.erase(fd);
}

void Server::addClient(int fd, const sockaddr_in& addr) {
    assert(fd > 0);
    _users[fd].init(fd, addr);
    if (_timeoutMS > 0) {
#ifdef __V0
        HeapTimerManager::GetInstance().ScheduleWithKey(
            fd, _timeoutMS, 0, [this, fd]() {
                if (_users.count(fd) > 0) {
                    closeConn(&_users[fd]);
                }
            });
#elif
// TODO
#endif // !__V0
        if (!_epoller->AddFd(fd, EPOLLIN | _connEvent)) {
            LOG_E("Failed to add client fd {}!", fd);
        }
        SetFdNonblock(fd);
        LOG_I("Client {} in!", _users[fd].GetFd());
    }
}

void Server::dealListen() {
    struct sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    do {
        const int fd =
            accept(_listenFd, reinterpret_cast<struct sockaddr*>(&addr), &len);
        if (fd <= 0) {
            return;
        } else if (http::Conn::userCount >= MAX_FD) {
            sendError(fd, "server busy!");
            LOG_W("clients full!");
            return;
        }
        addClient(fd, addr);
    } while (_listenEvent & EPOLLET);
}

void Server::dealRead(http::Conn* client) const {
    assert(client);
    extentTime(client);
#ifdef __V0
    _threadpool->AddTask([this, client] { onRead(client); });
#elif
// TODO 线程池替换
#endif // __V0
}

void Server::dealWrite(http::Conn* client) const {
    assert(client);
    extentTime(client);
#ifdef __V0
    _threadpool->AddTask([this, client] { onWrite(client); });
#elif
// TODO 线程池替换
#endif // __V0
}

void Server::extentTime(http::Conn* client) const {
    assert(client);
#ifdef __V0
    if (_timeoutMS > 0) {
        // 使用ScheduleWithKey，确保每个文件描述符只有一个定时器
        HeapTimerManager::GetInstance().ScheduleWithKey(
            client->GetFd(), _timeoutMS, 0, [this, fd = client->GetFd()]() {
                // 由于lambda可能在Server对象销毁后执行，需要安全处理
                if (!_isClose && _users.count(fd) > 0) {
                    const_cast<Server*>(this)->closeConn(
                        &const_cast<Server*>(this)->_users[fd]);
                }
            });
    }
#elif
// TODO timer 替换
#endif // __V0
}

// 干什么用？
void Server::onRead(http::Conn* client) const {
    assert(client);
    int ret = -1;
    int readErrno = 0;
    ret = client->read(&readErrno);
    if (ret <= 0 && readErrno != EAGAIN) {
        closeConn(client);
        return;
    }
    onProcess(client);
}

void Server::onProcess(http::Conn* client) const {
    if (client->process()) {
        if (!_epoller->ModFd(client->GetFd(), _connEvent | EPOLLOUT)) {
            LOG_E("Failed to mod fd {}!", client->GetFd());
        }
    } else {
        if (!_epoller->ModFd(client->GetFd(), _connEvent | EPOLLIN)) {
            LOG_E("Failed to mod fd {}!", client->GetFd());
        }
    }
}

void Server::onWrite(http::Conn* client) const {
    assert(client);
    int ret = -1;
    int writeErrno = 0;
    ret = client->write(&writeErrno);
    if (client->ToWriteBytes() == 0) { // 传输完成
        if (client->IsKeepAlive()) {
            onProcess(client);
            return;
        }
    } else if (ret < 0) {
        if (writeErrno == EAGAIN) { // 继续传输
            if (!_epoller->ModFd(client->GetFd(), _connEvent | EPOLLOUT)) {
                LOG_E("Failed to mod fd {}!", client->GetFd());
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
    struct linger optLinger = {0};
    if (_openLinger) {
        optLinger.l_onoff = 1;
        optLinger.l_linger = 1;
    }

    _listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (_listenFd < 0) {
        LOG_E("Create socket error!, port: {0}, {1}", _port, strerror(errno));
        return false;
    }

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

    ret = listen(_listenFd, 6); // 此处设置
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
    SetFdNonblock(_listenFd);
    LOG_I("Server port: {}.", _port);
    return true;
}

int Server::SetFdNonblock(const int fd) {
    assert(fd > 0);
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFD, 0) | O_NONBLOCK);
}

} // namespace v0

std::unique_ptr<v0::Server> NewServerFromConfig(const std::string& configPath) {
    if (!Config::Init(configPath)) {
        LOG_E("Failed to initialize config from {}", configPath);
        return nullptr;
    }
    // TODO 更改 config 的接口，获取的时候通过不同函数直接转换为 int 或者 uint
    // strtol 是 C 语言标准库中的一个函数，用于将字符串转换为长整型（long
    // int）数值
    const auto appPort =
        static_cast<unsigned int>(atoi(zws::GET_CONFIG("app.port").c_str()));
    const auto trig = atoi(zws::GET_CONFIG("app.trig").c_str());
    const auto timeout = atoi(zws::GET_CONFIG("app.timeout").c_str());
    const auto host = zws::GET_CONFIG("mysql.host");
    const auto sqlPort =
        static_cast<unsigned int>(atoi(zws::GET_CONFIG("mysql.port").c_str()));
    const auto sqlUser = zws::GET_CONFIG("mysql.user");
    const auto sqlPassword = zws::GET_CONFIG("mysql.password");
    const auto database = zws::GET_CONFIG("mysql.database");

    auto server = std::make_unique<v0::Server>(
        appPort, trig, timeout, false, sqlPort, sqlUser.c_str(),
        sqlPassword.c_str(), database.c_str(), 12, 6, true, 1, 1024);
    assert(server);
    return server;
}

} // namespace zws