#include "core/server.h"
#include <csignal>
#include <iostream>

int main() {
    try {
        // 初始化日志系统
        zener::Logger::Init();
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
