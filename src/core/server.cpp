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
#include <unistd.h>

namespace zws {
namespace v0 {

Server::Server(int port, const int trigMode, const int timeoutMS,
               const bool optLinger, const int sqlPort, const char* sqlUser,
               const char* sqlPwd, const char* dbName, int connPoolNum,
               int threadNum, bool openLog, int logLevel, int logQueSize)
    : _port(port), _openLinger(optLinger), _timeoutMS(timeoutMS),
      _isClose(false), _timer(new HeapTimer()),
      _threadpool(new ThreadPool(threadNum)), _epoller(new Epoller()) {

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
                timeMS = _timer->GetNextTick();
#elif
                // TODO
#endif // __V0
            }
            const int eventCnt = _epoller->Wait(timeMS);
            for (int i = 0; i < eventCnt; i++) {
                /* 处理事件 */
                int fd = _epoller->GetEventFd(i);
                uint32_t events = _epoller->GetEvents(i);
                if (fd == _listenFd) {
                    dealListen();
                } else if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    // 关闭
                    assert(_users.count(fd) > 0);
                    closeConn(&_users[fd]);
                } else if (events & EPOLLIN) {
                    // 读
                    // 此处触发，abort -- 2025/02/24 20:36
                    assert(_users.count(fd) > 0);
                    dealRead(&_users[fd]);
                } else if (events & EPOLLOUT) {
                    // 写
                    assert(_users.count(fd) > 0);
                    dealWrite(&_users[fd]);
                } else {
                    LOG_E("unexpected event: {}", events);
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

void Server::sendError(int fd, const char* info) {
    assert(fd > 0);
    if (const int ret = send(fd, info, strlen(info), 0); ret > 0) {
        LOG_W("send error to client {} error!", fd);
    }
    close(fd);
}

void Server::closeConn(http::Conn* client) const {
    assert(client);
    LOG_I("client {} quit.", client->GetFd());
    if (!_epoller->DelFd(client->GetFd())) {
        LOG_E("Failed to delete client fd {}!", client->GetFd());
    }
    client->Close();
}

void Server::addClient(int fd, const sockaddr_in& addr) {
    assert(fd > 0);
    _users[fd].init(fd, addr);
    if (_timeoutMS > 0) {
#ifdef __V0
        _timer->Add(fd, _timeoutMS,
                    [this, capture0 = &_users[fd]] { closeConn(capture0); });
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
        _timer->Adjust(client->GetFd(), _timeoutMS);
#elif
// TODO timer 替换
#endif // __V0
    }
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

    return server;
}

} // namespace zws