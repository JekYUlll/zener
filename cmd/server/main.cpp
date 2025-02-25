#include "core/server.h"

int main() {
    zws::Logger::Init();

    const auto server = zws::NewServerFromConfig("config.toml");
    zws::ServerGuard guard(server.get(), true);
    guard.Wait();

    return 0;
}
