#include "core/server.h"
#include "utils/log/use_spd_log.h"
#include <csignal>
#include <filesystem>
#include <iostream>

std::atomic<bool> g_ShutdownFlag{false};

void HandleConsoleInput() {
        std::string cmd;
        while (!g_ShutdownFlag) {
                std::cout << "Enter 'exit' to shutdown server: ";
                std::getline(std::cin, cmd);
                if (cmd == "exit") {
                        g_ShutdownFlag = true;
                        break;
                }
        }
}

int main() {
        try {
                zener::Logger::Init();

                const auto server = zener::NewServerFromConfig("config.toml");

                zener::ServerGuard guard(server.get(), true);
                // -------------------- 控制台输入监听线程 --------------------
                std::thread consoleThread(HandleConsoleInput);
                consoleThread.detach();
                // -------------------- 主线程阻塞等待退出条件 --------------------
                while (!g_ShutdownFlag && !guard.ShouldExit()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }

                // -------------------- 触发优雅关闭 --------------------
                guard.Shutdown();

                return 0;
        } catch (const std::exception& e) {
                std::cerr << "Fatal error: " << e.what() << std::endl;
                return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
}
