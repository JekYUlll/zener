#include "utils/log/logger.h"

#include <cstdlib>
#include <iostream>
#include <sys/stat.h>
#include <thread>

static auto log_path = "logs";

int main() {

    zws::Logger::Init();

    if (mkdir(log_path, 0777) != 0 && errno != EEXIST) {
        std::cerr << "Failed to create log directory" << std::endl;
        return EXIT_FAILURE;
    }

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

    // while(true) {
    //     LOG_D("-----Log test: {0}, {1}, {3}", __FUNCTION__, 1, 0.14f, true);
    //     std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    // }

    return EXIT_SUCCESS;
}
