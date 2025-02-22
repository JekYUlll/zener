// #include "core/server.h"
#include "core/init.h"
#include "utils/log/logger.h"

#include <cstdlib>
#include <iostream>
#include <sys/stat.h>
#include <thread>

static const char* log_path = "logs";

int main(void) {

    // WebServer server(
    //     1316, 3, 60000, false,             /* 端口 ET模式 timeoutMs 优雅退出
    //     */ 3306, "root", "root", "webserver", /* Mysql配置 */ 12, 6, true, 1,
    //     1024);             /* 连接池数量 线程池数量 日志开关 日志等级
    //     日志异步队列容量 */
    // server.Start();

    // auto server = new zws::Server(8080, nullptr);

    zws::Logger::Init();

    if (mkdir(log_path, 0777) != 0 && errno != EEXIST) {
        std::cerr << "Failed to create log directory" << std::endl;
        return EXIT_FAILURE;
    }

    LOG_I("Hardware Concurrency: {0}, {1}",
          static_cast<size_t>(std::thread::hardware_concurrency()),
          __FUNCTION__);

    if (!zws::Logger::WriteToFile("logs/test.log")) {
        std::cerr << "Failed to create log file" << std::endl;
        return EXIT_FAILURE;
    }

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

    return EXIT_SUCCESS;
}
