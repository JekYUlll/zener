#include "core/server.h"

int main() {
    zener::Logger::Init();

    const auto server = zener::NewServerFromConfig("config.toml");
    zener::ServerGuard guard(server.get(), true);
    guard.Wait();

    return 0;
}
