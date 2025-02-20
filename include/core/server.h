#ifndef ZENER_SERVER_H
#define ZENER_SERVER_H

#include "core/restful.h"
#include "database/database.h"
#include "utils/log/logger.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <sys/stat.h>

namespace zws {

#define LOG_PATH "logs"

class Server : public IRestful {
  public:
    Server(const int& port, const db::Database* db) {

        if (mkdir(LOG_PATH, 0777) != 0 && errno != EEXIST) {
            std::cerr << "Failed to create log directory" << std::endl;
            exit(EXIT_FAILURE);
        }

        zws::Logger::Init();
        if (!zws::Logger::WriteToFile("logs/test.log")) {
            std::cerr << "Failed to create log file" << std::endl;
            exit(EXIT_FAILURE);
        }
        LOG_I(R"(
          _______ _ __   ___ _ __ 
         |_  / _ \ '_ \ / _ \ '__|
          / /  __/ | | |  __/ |   
         /___\___|_| |_|\___|_|   
         )");
        LOG_T("Log test: {0}, {1}, {3}", __FUNCTION__, 1, 0.14f, true);
        LOG_D("Log test: {0}, {1}, {3}", __FUNCTION__, 1, 0.14f, true);
        LOG_I("Log test: {0}, {1}, {3}", __FUNCTION__, 1, 0.14f, true);
        LOG_W("Log test: {0}, {1}, {3}", __FUNCTION__, 1, 0.14f, true);
        LOG_E("Log test: {0}, {1}, {3}", __FUNCTION__, 1, 0.14f, true);
        zws::Logger::SetLogFilePath("logs/test2.log");
        LOG_T("Log test: {0}, {1}, {3}", __FUNCTION__, 1, 0.14f, true);
        LOG_D("Log test: {0}, {1}, {3}", __FUNCTION__, 1, 0.14f, true);
        LOG_I("Log test: {0}, {1}, {3}", __FUNCTION__, 1, 0.14f, true);
        LOG_W("Log test: {0}, {1}, {3}", __FUNCTION__, 1, 0.14f, true);
        LOG_E("Log test: {0}, {1}, {3}", __FUNCTION__, 1, 0.14f, true);
    }

    ~Server();

    std::unique_ptr<Server> Default();

    void ListenAndServe(const std::string& address = ":8080");

    void Start();
    void Spin();
    void Stop();
    void Shutdown();

    void GET(const std::string& router, Handler h) override;
    void POST(const std::string& router, Handler h) override;
    void PUT(const std::string& router, Handler h) override;
    void HEAD(const std::string& router, Handler h) override;

    void Handle();
    void Any();

  private:
    int _port;
    std::shared_ptr<db::Database> _db;
    // std::unique_ptr<ThreadPool> m_threadPool;
    // std::unique_ptr<EventLoop> m_loop;
};

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

} // namespace zws

#endif // !ZENER_SERVER_H
