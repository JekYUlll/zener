// #include "core/server.h"
#include "config/config.h"
#include "core/init.h"
#include "core/server.h"
#include "utils/log/logger.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

int main(void) {

    zws::Logger::Init();

    auto server = zws::NewServerFromConfig("config.toml");

    zws::Config::Print();

    server->Start();

    return EXIT_SUCCESS;
}
