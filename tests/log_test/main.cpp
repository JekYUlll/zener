#include "utils/log/logger.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-pragmas"
#pragma clang diagnostic ignored "-Wunused-lambda-capture"


namespace fs = std::filesystem;
static auto log_path = "logs";

int main() {
    zws::Logger::Init();

    try {
        fs::create_directories(log_path);
    } catch (const std::exception& e) {
        std::cerr << "Failed to create log directory: " << e.what()
                  << std::endl;
        return EXIT_FAILURE;
    }

    if (!zws::Logger::WriteToFile("logs/test.log")) {
        std::cerr << "Failed to create log file" << std::endl;
        return EXIT_FAILURE;
    }

    // 测试不同级别的日志
    LOG_T("这是一条 TRACE 级别的日志");
    LOG_D("这是一条 DEBUG 级别的日志");
    LOG_I("这是一条 INFO 级别的日志");
    LOG_W("这是一条 WARN 级别的日志");
    LOG_E("这是一条 ERROR 级别的日志");

    // 测试格式化
    LOG_I("格式化测试 - 整数: {}, 浮点数: {:.3f}, 字符串: {}", 42, 3.14159,
          "hello");

    // 测试多线程
    std::thread t1([]() {
        for (int i = 0; i < 5; ++i) {
            LOG_I("线程1打印第{}条日志", i + 1);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    std::thread t2([]() {
        for (int i = 0; i < 5; ++i) {
            LOG_D("线程2打印第{}条日志", i + 1);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    // 测试持续打印
    int count = 0;
    const auto start = std::chrono::steady_clock::now();

    while (true) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed =
            std::chrono::duration_cast<std::chrono::seconds>(now - start)
                .count();

        if (elapsed >= 10) { // 运行10秒后退出
            break;
        }

        LOG_I("持续打印测试 - 计数: {}, 已运行: {}秒", ++count, elapsed);
        std::this_thread::sleep_for(
            std::chrono::milliseconds(500)); // 每500ms打印一次
    }

    t1.join();
    t2.join();

    LOG_I("测试完成，共打印 {} 条持续日志", count);
    return EXIT_SUCCESS;
}

#pragma clang diagnostic pop