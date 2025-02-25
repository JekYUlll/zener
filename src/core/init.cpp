#include "core/init.h"
// #include "core/server.h"
#include "utils/log/use_spd_log.h"

namespace zener {

void Init() {
    Logger::Init();
    // Server::Init();
}

} // namespace zener