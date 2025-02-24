// #include "core/server.h"
#include "config/config.h"
#include "core/server.h"
#include "utils/log/logger.h"

#include <cassert>
#include <cstdlib>
#include <iostream>

int main() {

    const auto server = zws::NewServerFromConfig("config.toml");
    zws::ServerGuard guard(server.get());
    // assert(server);
    // server->Start();

    std::cin.get();

    return EXIT_SUCCESS;
}
