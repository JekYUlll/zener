#ifndef ZENER_SERVER_H
#define ZENER_SERVER_H

#include "core/epoller.h"
#include "http/conn.h"
#include "task/threadpool_1.h"
#include "task/timer/heaptimer.h"

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <netinet/in.h>
#include <sys/stat.h>
#include <unordered_map>

namespace zws {
namespace v0 {

class WebServer {
  public:
    WebServer(int port, int trigMode, int timeoutMS, bool OptLinger,
              int sqlPort, const char* sqlUser, const char* sqlPwd,
              const char* dbName, int connPoolNum, int threadNum, bool openLog,
              int logLevel, int logQueSize);

    ~WebServer();

    void Start();

  private:
    bool initSocket();
    void initEventMode(int trigMode);
    void addClient(int fd, sockaddr_in addr);

    void dealListen();
    void dealRead(http::Conn* client);
    void dealWrite(http::Conn* client);

    void sendError(int fd, const char* info);
    void extentTime(http::Conn* client);
    void closeConn(http::Conn* client);

    void onRead(http::Conn* client);
    void onWrite(http::Conn* client);
    void onProcess(http::Conn* client);

    static const int MAX_FD = 65535;

    static int SetFdNonblock(int fd);

    int _port;
    bool _openLinger;
    int _timeoutMS;
    bool _isClose;
    int _listenFd;
    char* _srcDir;

    uint32_t _listenEvent;
    uint32_t _connEvent;

    std::unique_ptr<v0::HeapTimer> _timer;

    std::unique_ptr<v0::ThreadPool> _threadpool;

    std::unique_ptr<Epoller> _epoller;

    std::unordered_map<int, http::Conn> _users;
};

} // namespace v0

std::unique_ptr<v0::WebServer>
NewServerFromConfig(const std::string& configPath);

} // namespace zws

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
