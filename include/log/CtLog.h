//
// Created by JekYUlll on 2024/9/23.
//

#ifndef CTLOG_H
#define CTLOG_H

// #include "CtEngine.h"

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "spdlog/spdlog.h"
#include "spdlog/common.h"

namespace cte {
    class  CtLog {
    public:
        CtLog() = delete;
        CtLog(const CtLog&) = delete;
        CtLog &operator=(const CtLog&) = delete;

        static void Init();

        static spdlog::logger* GetLoggerInstance() {
            assert(sLoggerInstance && "Logger instance is null, have not execute AdLog::Init().");
            return sLoggerInstance.get();
        }

    private:
        static std::shared_ptr<spdlog::logger> sLoggerInstance;

    };

#define LOG_T(...) SPDLOG_LOGGER_TRACE(cte::CtLog::GetLoggerInstance(), __VA_ARGS__)
#define LOG_D(...) SPDLOG_LOGGER_DEBUG(cte::CtLog::GetLoggerInstance(), __VA_ARGS__)
#define LOG_I(...) SPDLOG_LOGGER_INFO(cte::CtLog::GetLoggerInstance(), __VA_ARGS__)
#define LOG_W(...) SPDLOG_LOGGER_WARN(cte::CtLog::GetLoggerInstance(), __VA_ARGS__)
#define LOG_E(...) SPDLOG_LOGGER_ERROR(cte::CtLog::GetLoggerInstance(), __VA_ARGS__)
}

#endif //CTLOG_H
