#include "core/server.h"
#include "utils/log/use_spd_log.h"
#include <csignal>
#include <filesystem>
#include <iostream>

int main() {
    try {
        // 初始化日志系统
        zener::Logger::Init();

        // 创建日志目录
        std::filesystem::path logDir = "logs";
        if (!std::filesystem::exists(logDir)) {
            std::filesystem::create_directories(logDir);
        }

        // 配置日志轮转，最大文件大小50MB，保留10个历史文件
        if (!zener::Logger::WriteToFileWithRotation("logs", "server")) {
            std::cerr << "配置日志轮转失败，将只使用控制台输出!" << std::endl;
        }

        std::cout << "启动Zener服务器..." << std::endl;

        // 从配置文件创建服务器
        const auto server = zener::NewServerFromConfig("config.toml");
        if (!server) {
            std::cerr << "创建服务器失败，请检查配置文件!" << std::endl;
            return 1;
        }

        // 创建ServerGuard并开启信号处理
        std::cout << "服务器创建成功，正在启动..." << std::endl;
        zener::ServerGuard guard(server.get(), true);

        // 等待服务器退出信号
        std::cout << "服务器已启动，按Ctrl+C停止..." << std::endl;
        guard.Wait();

        std::cout << "服务器已关闭" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "服务器发生严重错误: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "服务器发生未知严重错误" << std::endl;
        return 1;
    }
}
