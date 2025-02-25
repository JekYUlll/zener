#include "utils/log/logger.h"

#include <thread>
#include <spdlog/logger.h>

int main() {
    zener::Logger::Init();
    LOG_I("Hardware Concurrency: {0}, {1}",
          static_cast<size_t>(std::thread::hardware_concurrency()), __FUNCTION__);
}

