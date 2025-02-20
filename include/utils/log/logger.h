#ifndef ZENER_LOGGER_H
#define ZENER_LOGGER_H

#include "common.h"

#ifdef __USE_SPDLOG
#include "use_spd_log.h"
namespace zws {

class GLogger {

  private:
    Logger* loggerInstance;

  public:
};

#define LOG_T(...)                                                             \
    ZENER_LOG_LOGGER_CALL(zws::Logger::GetLoggerInstance(),                    \
                          spdlog::level::trace, __VA_ARGS__)
#define LOG_D(...)                                                             \
    ZENER_LOG_LOGGER_CALL(zws::Logger::GetLoggerInstance(),                    \
                          spdlog::level::debug, __VA_ARGS__)
#define LOG_I(...)                                                             \
    ZENER_LOG_LOGGER_CALL(zws::Logger::GetLoggerInstance(),                    \
                          spdlog::level::info, __VA_ARGS__)
#define LOG_W(...)                                                             \
    ZENER_LOG_LOGGER_CALL(zws::Logger::GetLoggerInstance(),                    \
                          spdlog::level::warn, __VA_ARGS__)
#define LOG_E(...)                                                             \
    ZENER_LOG_LOGGER_CALL(zws::Logger::GetLoggerInstance(),                    \
                          spdlog::level::err, __VA_ARGS__)
} // namespace zws
#else
#include "_logger.h"
#define LOG_T(...) std::cout << __VA_ARGS__ << std::endl
#endif // !USE_SPDLOG

#endif // !ZENER_LOGGER_H