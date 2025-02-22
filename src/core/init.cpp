#include "core/init.h"
// #include "core/server.h"
#include "utils/log/use_spd_log.h"

namespace zws {

void Init() {
    Logger::Init();
    // Server::Init();
}

} // namespace zws