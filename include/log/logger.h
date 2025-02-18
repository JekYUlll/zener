#ifndef ZENER_LOGGER_H
#define ZENER_LOGGER_H

#include "common.h"

#ifdef USE_SPDLOG
#include "use_spd_log.h"
namespace zws {
#define LOG_T(...)                                                             \
    ZWS_LOG_LOGGER_CALL(zws::Logger::GetLoggerInstance(),                      \
                        spdlog::level::trace, __VA_ARGS__)
#define LOG_D(...)                                                             \
    ZWS_LOG_LOGGER_CALL(zws::Logger::GetLoggerInstance(),                      \
                        spdlog::level::debug, __VA_ARGS__)
#define LOG_I(...)                                                             \
    ZWS_LOG_LOGGER_CALL(zws::Logger::GetLoggerInstance(), spdlog::level::info, \
                        __VA_ARGS__)
#define LOG_W(...)                                                             \
    ZWS_LOG_LOGGER_CALL(zws::Logger::GetLoggerInstance(), spdlog::level::warn, \
                        __VA_ARGS__)
#define LOG_E(...)                                                             \
    ZWS_LOG_LOGGER_CALL(zws::Logger::GetLoggerInstance(), spdlog::level::err,  \
                        __VA_ARGS__)
} // namespace zws
#else
#include "_logger.h"
#endif // !USE_SPDLOG

#endif // !ZENER_LOGGER_H