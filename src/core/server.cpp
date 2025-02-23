#include "core/server.h"
#include "config/config.h"
#include "database/sql_connector.h"
#include "http/conn.h"
#include "utils/log/logger.h"

#include <asm-generic/socket.h>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <functional>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace zws {
namespace v0 {

WebServer::WebServer(int port, int trigMode, int timeoutMS, bool OptLinger,
                     int sqlPort, const char* sqlUser, const char* sqlPwd,
                     const char* dbName, int connPoolNum, int threadNum,
                     bool openLog, int logLevel, int logQueSize) {

    _srcDir = getcwd(nullptr, 256);
    assert(_srcDir);
    strncat(_srcDir, "/static/", 16);
    http::Conn::userCount.store(0);
    http::Conn::srcDir = _srcDir;

    db::SqlConnector::GetInstance().Init("localhost", sqlPort, sqlUser, dbName,
                                         dbName, connPoolNum);

    initEventMode(trigMode);
    if (!initSocket()) {
        _isClose = true;
    }
    // log
    Logger::Init();

    // 段错误
    if (0) {
        // config
        Config::Init("config.TOML");
        // TODO
        // 检查 log 是否初始化，如果没有，直接不打印 LOG
        // 或者控制是否打印至文件
        // TODO 修改 log 类，使其自动拼接，并且存储一个 path。不然 log.name
        // 需要配置完整的路径
        auto logPath = GET_CONFIG("log.path");
        auto logName = GET_CONFIG("log.name");

        if (mkdir(logPath, 0777) != 0 && errno != EEXIST) {
            LOG_E("Failed to create log directory: ");
        }
        if (!zws::Logger::WriteToFile(logName)) {
            LOG_E("Failed to create log file: {}", logName);
        }
    }

    if (mkdir("logs", 0777) != 0 && errno != EEXIST) {
        LOG_E("Failed to create log directory: ");
    }
    if (!zws::Logger::WriteToFile("logs/test3.log")) {
        LOG_E("Failed to create log file: {}", "logs/test3.log");
    }

    // start log
    LOG_I("=====================Server Init=====================");
    LOG_I("port: {0}, OpenLinger: {1}", port, OptLinger ? "true" : "false");
    LOG_I("Listen Mode: {0}, OpenConn Mode: {1}",
          (_listenEvent & EPOLLET ? "ET" : "LT"),
          (_connEvent & EPOLLET ? "ET" : "LT"));
    LOG_I("LogSys level: {}", logLevel);
    LOG_I("srcDir: {}", http::Conn::srcDir);
    LOG_I("SqlConnPool num: {}, ThreadPool num: {}", connPoolNum, threadNum);
}

WebServer::~WebServer() {
    close(_listenFd);
    _isClose = true;
    free(_srcDir);
    db::SqlConnector::GetInstance().Close();
}

// TODO 我在 Epoller 里也设置了 trigMode 的设置与检查
// 检查是否有问题
void WebServer::initEventMode(int trigMode) {
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
void WebServer::Start() {
    int timeMS = -1; /* epoll wait timeout == -1 无事件将阻塞 */
    if (!_isClose) {
        LOG_I("========== Server start ==========");
        while (!_isClose) {
            if (_timeoutMS > 0) {
#ifdef __V0
                timeMS = _timer->GetNextTick();
#elif
                // TODO
#endif // DEBUG
            }
            int eventCnt = _epoller->Wait(timeMS);
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
                    assert(_users.count(fd) > 0);
                    dealRead(&_users[fd]);
                } else if (events & EPOLLOUT) {
                    // 写
                    assert(_users.count(fd) > 0);
                    dealWrite(&_users[fd]);
                } else {
                    LOG_E("Unexpected event");
                }
            }
        }
    }
}

void WebServer::sendError(int fd, const char* info) {
    assert(fd > 0);
    int ret = send(fd, info, strlen(info), 0);
    if (ret > 0) {
        LOG_W("send error to client {} error !", fd);
    }
    close(fd);
}

void WebServer::closeConn(http::Conn* client) {
    assert(client);
    LOG_I("client {} quit.", client->GetFd());
    _epoller->DelFd(client->GetFd());
    client->Close();
}

void WebServer::addClient(int fd, sockaddr_in addr) {
    assert(fd > 0);
    _users[fd].init(fd, addr);
    if (_timeoutMS > 0) {
#ifdef __V0
        _timer->Add(fd, _timeoutMS,
                    std::bind(&WebServer::closeConn, this, &_users[fd]));
#elif
// TODO
#endif // !__V0
        _epoller->AddFd(fd, EPOLLIN | _connEvent);
        SetFdNonblock(fd);
        LOG_I("Client {} in!", _users[fd].GetFd());
    }
}

void WebServer::dealListen() {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    do {
        int fd = accept(_listenFd, (struct sockaddr*)&addr, &len);
        if (fd <= 0) {
            return;
        } else if (http::Conn::userCount >= MAX_FD) {
            sendError(fd, "Server busy!");
            LOG_W("Clients full!");
            return;
        }
        addClient(fd, addr);
    } while (_listenEvent & EPOLLET);
}

void WebServer::dealRead(http::Conn* client) {
    assert(client);
    extentTime(client);
#ifdef __V0
    _threadpool->AddTask(std::bind(&WebServer::onRead, this, client));
#elif
// TODO 线程池替换
#endif // DEBUG
}

void WebServer::dealWrite(http::Conn* client) {
    assert(client);
    extentTime(client);
#ifdef __V0
    _threadpool->AddTask(std::bind(&WebServer::onWrite, this, client));
#elif
// TODO 线程池替换
#endif // DEBUG
}

void WebServer::extentTime(http::Conn* client) {
    assert(client);
#ifdef __V0
    if (_timeoutMS > 0) {
        _timer->Adjust(client->GetFd(), _timeoutMS);
#elif
// TODO timer 替换
#endif // DEBUG
    }
}

// 干什么用？
void WebServer::onRead(http::Conn* client) {
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

void WebServer::onProcess(http::Conn* client) {
    if (client->process()) {
        _epoller->ModFd(client->GetFd(), _connEvent | EPOLLOUT);
    } else {
        _epoller->ModFd(client->GetFd(), _connEvent | EPOLLIN);
    }
}

void WebServer::onWrite(http::Conn* client) {
    assert(client);
    int ret = -1;
    int writeErrno = 0;
    ret = client->write(&writeErrno);
    if (client->ToWriteBytes() == 0) {
        // 传输完成
        if (client->IsKeepAlive()) {
            onProcess(client);
            return;
        }
    } else if (ret < 0) {
        if (writeErrno == EAGAIN) {
            // 继续传输
            _epoller->ModFd(client->GetFd(), _connEvent | EPOLLOUT);
            return;
        }
    }
    closeConn(client);
}

/* Create listenFd */
bool WebServer::initSocket() {
    int ret = 0;
    struct sockaddr_in addr;
    if (_port > 65535 || _port < 1024) {
        LOG_E("port: {} is not valid!", _port);
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
        LOG_E("create socket error!, port: {0}, {1}", _port, strerror(errno));
        return false;
    }

    ret = setsockopt(_listenFd, SOL_SOCKET, SO_LINGER, &optLinger,
                     sizeof(optLinger));
    if (ret < 0) {
        close(_listenFd);
        LOG_E("init linger error! port: {0}, {1}", _port, strerror(errno));
        return false;
    }

    int optval = 1;
    // 端口复用，只有最后一个套接字会正常接收数据
    ret = setsockopt(_listenFd, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval,
                     sizeof(int));
    if (ret == -1) {
        LOG_E("set socket setsockopt error! {1}, {2}, {3}", __FILE__, __LINE__,
              strerror(errno));
        close(_listenFd);
        return false;
    }

    ret = bind(_listenFd, (struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0) {
        LOG_E("bind port: {0} error! {1}", _port, strerror(errno));
        close(_listenFd);
        return false;
    }

    ret = listen(_listenFd, 6); // 此处设置
    if (ret < 0) {
        LOG_E("listen port: {0} error!, {1}", _port, strerror(errno));
        close(_listenFd);
        return false;
    }

    ret = _epoller->AddFd(_listenFd, _listenEvent | EPOLLIN);
    if (ret == 0) {
        LOG_E("add listen error! {0} {1} {2}", __FILE__, __LINE__,
              strerror(errno));
        close(_listenFd);
        return false;
    }
    SetFdNonblock(_listenFd);
    LOG_I("server port: {}", _port);
    return true;
}

int WebServer::SetFdNonblock(int fd) {
    assert(fd > 0);
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFD, 0) | O_NONBLOCK);
}

} // namespace v0
} // namespace zws