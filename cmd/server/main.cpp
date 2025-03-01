#include "core/server.h"

int main() {
    zener::Logger::Init();

    const auto server = zener::NewServerFromConfig("config.toml");
    server->Run();
    return 0;
}
