#ifndef ZENER_LOGGER_H
#define ZENER_LOGGER_H

// TODO
// 封装一个总的 logger 类，按编译选项设置是否使用 spdlog

#ifdef __USE_SPDLOG
#include "use_spd_log.h"
#else
#include "_logger.h"
#endif

#ifdef __USE_SPDLOG
namespace zener {

class GLogger {

  private:
    Logger* loggerInstance;

  public:
};

#ifdef DEBUG
#define LOG_D(...)                                                             \
    ZENER_LOG_LOGGER_CALL(zener::Logger::GetLoggerInstance(),                  \
                          spdlog::level::debug, __VA_ARGS__)
#else
#define LOG_D(...)                                                             \
    do {                                                                       \
    } while (0) // 在非DEBUG模式下不执行任何操作
#endif

#ifdef NO_LOG
#define LOG_I(...)                                                             \
    do {                                                                       \
    } while (0)
#define LOG_W(...)                                                             \
    do {                                                                       \
    } while (0)
#define LOG_E(...)                                                             \
    do {                                                                       \
    } while (0)
#define LOG_T(...)                                                             \
    do {                                                                       \
    } while (0)

#else
#define LOG_I(...)                                                             \
    ZENER_LOG_LOGGER_CALL(zener::Logger::GetLoggerInstance(),                  \
                          spdlog::level::info, __VA_ARGS__)
#define LOG_W(...)                                                             \
    ZENER_LOG_LOGGER_CALL(zener::Logger::GetLoggerInstance(),                  \
                          spdlog::level::warn, __VA_ARGS__)
#define LOG_E(...)                                                             \
    ZENER_LOG_LOGGER_CALL(zener::Logger::GetLoggerInstance(),                  \
                          spdlog::level::err, __VA_ARGS__)
#define LOG_T(...)                                                             \
    ZENER_LOG_LOGGER_CALL(zener::Logger::GetLoggerInstance(),                  \
                          spdlog::level::trace, __VA_ARGS__)
#endif

} // namespace zener
#else
#include "_logger.h"
#define LOG_T(...) std::cout << __VA_ARGS__ << std::endl
#endif // !USE_SPDLOG

#endif // !ZENER_LOGGER_H