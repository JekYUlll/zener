#include "core/server.h"

#include <cassert>
#include <cstdlib>
#include <iostream>

int main() {
    zws::Logger::Init(); // 先初始化日志
    const auto server = zws::NewServerFromConfig("config.toml");
    // zws::ServerGuard guard(server.get());
    assert(server);
    server->Start();

    std::cin.get();

    return EXIT_SUCCESS;
}
