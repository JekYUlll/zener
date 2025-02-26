#ifndef ZENER_SERVER_H
#define ZENER_SERVER_H

#include "core/epoller.h"
#include "http/conn.h"
#include "task/threadpool_1.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <memory>
#include <netinet/in.h>
#include <thread>
#include <unordered_map>

namespace zener {
namespace v0 {

class Server {
  public:
    Server(int port, int trigMode, int timeoutMS, bool optLinger,
           const char* sqlHost, int sqlPort, const char* sqlUser,
           const char* sqlPwd, const char* dbName, int connPoolNum,
           int threadNum, bool openLog, int logLevel = -1, int logQueSize = -1);

    ~Server();

    void Start();
    void Stop();
    void Shutdown(int timeoutMS = 5000); // 优雅退出

    [[nodiscard]] bool IsClosed() const { return _isClose; }

  private:
    bool initSocket();
    void initEventMode(int trigMode);
    void addClient(int fd, const sockaddr_in& addr) const;

    void dealListen() const;
    void dealRead(http::Conn* client) const;
    void dealWrite(http::Conn* client) const;

    static void sendError(int fd, const char* info);
    void extentTime(const http::Conn* client) const;
    void closeConn(http::Conn* client) const;

    void onRead(http::Conn* client) const;
    void onWrite(http::Conn* client) const;
    void onProcess(http::Conn* client) const;

    static constexpr int MAX_FD = 65535;

    static int setFdNonblock(int fd);

    int _port;
    bool _openLinger;
    int _timeoutMS;
    bool _isClose;
    int _listenFd{};
    std::string _cwd{};       // 工作目录
    std::string _staticDir{}; // 静态资源目录

    uint32_t _listenEvent{};
    uint32_t _connEvent{};

    std::unique_ptr<ThreadPool> _threadpool;
    std::unique_ptr<Epoller> _epoller;
    mutable std::unordered_map<int, http::Conn> _users;
};

} // namespace v0

std::unique_ptr<v0::Server> NewServerFromConfig(const std::string& configPath);

class ServerGuard {
  public:
    explicit ServerGuard(v0::Server* srv)
        : _srv(srv), _setupSignalHandlers(false), _shouldExit(false) {
        _thread = std::thread([this] { _srv->Start(); });
    }

    // 带有信号处理的构造函数
    ServerGuard(v0::Server* srv, bool setupSignalHandlers)
        : _srv(srv), _setupSignalHandlers(setupSignalHandlers),
          _shouldExit(false) {
        if (_setupSignalHandlers) {
            // 设置静态指针以便信号处理函数访问
            _instance = this;
            // 使用更稳定的sigaction替代signal
            struct sigaction sa{};
            sa.sa_handler = SignalHandler;
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = 0;
            sigaction(SIGINT, &sa, nullptr);
            sigaction(SIGTERM, &sa, nullptr);
        }
        _thread = std::thread([this] { _srv->Start(); });
    }

    ~ServerGuard() {
        Shutdown();
        if (_thread.joinable()) {
            _thread.join();
        }
        // 清除静态指针
        if (_setupSignalHandlers && _instance == this) {
            _instance = nullptr;
        }
        Logger::Shutdown();
    }

    // 等待服务器线程结束，允许超时和中断
    void Wait() {
        const int CHECK_INTERVAL_MS = 100;

        while (!_shouldExit) {
            // 检查服务器线程是否已经结束
            if (!_thread.joinable() || _srv->IsClosed()) {
                break;
            }

            // 定期唤醒以检查_shouldExit标志
            std::this_thread::sleep_for(
                std::chrono::milliseconds(CHECK_INTERVAL_MS));
        }

        // 如果收到退出信号或者服务器已经关闭，确保关闭服务器
        if (_shouldExit && !_srv->IsClosed()) {
            _srv->Shutdown();
        }

        // 等待服务器线程结束
        if (_thread.joinable()) {
            _thread.join();
        }
    }

    // 手动触发服务器关闭
    void Shutdown() {
        _shouldExit = true;
        if (_srv && !_srv->IsClosed()) {
            _srv->Shutdown();
        }
    }

  private:
    // 信号处理函数
    static void SignalHandler(int sig) {
        if (_instance) {
            LOG_I("Signal received: {}, shutting server...", sig);
            // 只设置标志，避免在信号处理函数中调用复杂函数
            _instance->_shouldExit = true;
        }
    }

    v0::Server* _srv;
    std::thread _thread;
    bool _setupSignalHandlers;
    std::atomic<bool> _shouldExit; // 线程安全的退出标志

    // 静态成员用于信号处理
    static ServerGuard* _instance;
};

// 在类外部定义静态成员
inline ServerGuard* ServerGuard::_instance = nullptr;

} // namespace zener

// class Server : public IRestful {
//   public:
//     Server(const int& port, const db::Database* db);
//     ~Server();

//     std::unique_ptr<Server> Default();

//     void ListenAndServe(const std::string& address = ":8080");

//     void Start();
//     void Spin();
//     void Stop();
//     void Shutdown();

//     void GET(const std::string& router, Handler h) override;
//     void POST(const std::string& router, Handler h) override;
//     void PUT(const std::string& router, Handler h) override;
//     void HEAD(const std::string& router, Handler h) override;

//     void Handle();
//     void Any();

//   private:
//     int _port;
//     std::shared_ptr<db::Database> _db;
//     // std::unique_ptr<ThreadPool> m_threadPool;
//     // std::unique_ptr<EventLoop> m_loop;
// };

// 类似Go的 http.FileServer
// server.ServerStatic("/static", "./public");
// 可定制缓存策略
// server.ServeStatic("/assets", "./dist", {
//     .max_age = 3600, // 缓存 1 小时
//     .enable_etag = true // 启用 ETag 验证
// });

// 链式配置 Builder 模式
// server.Config()
//     .SetThreadPoolSize(4)    // 线程池
//     .SetTimeout(5000)        // 超时时间(ms)
//     .EnableCompression()     // 启用gzip压缩
//     .SetLogger(MyLogger);    // 自定义日志

// 扩展接口（Hooks）
// 生命周期钩子
// server.OnStartup([]() {
//     LOG_INFO << "Server starting...";
// });

// server.OnShutdown([]() {
//     LOG_INFO << "Server shutting down...";
// });

// // 自定义协议支持
// server.AddProtocolHandler("websocket", WebSocketHandler);

#endif // !ZENER_SERVER_H
