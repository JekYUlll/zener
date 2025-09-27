#include "core/server.h"

int main() {
    zener::Logger::Init();

    const auto server = zener::NewServerFromConfig("config.toml");
    try {
        server->Run();
    } catch (const std::exception &) {
        //
    }

    return 0;
}
