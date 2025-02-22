#include "utils/log/use_spd_log.h"
#include "spdlog/async.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"

#include <atomic>
#include <iostream>
#include <memory>
#include <mutex>

namespace zws {

Logger Logger::sLoggerInstance{};
static std::shared_ptr<spdlog::logger> sSpdLogger{};

std::atomic<bool> Logger::_bWriteToFile{false};
std::string Logger::_sLogFileName{};
std::mutex Logger::_fileNameMutex;

static std::mutex sLoggerMutex;

Logger::Logger() {}

static std::string sFirstInitFile;
static int sFirstInitLine;
static bool sInitialized = false;

void Logger::Init() {
    std::lock_guard<std::mutex> lock(sLoggerMutex);

    if (sInitialized) {
        if (sSpdLogger) {
#ifdef DEBUG
            sSpdLogger->warn(
                "Logger already initialized at {}:{}, current call from {}:{}",
                sFirstInitFile, sFirstInitLine, __FILE__, __LINE__);
#endif // DEBUG
        }
        return;
    }
    sInitialized = true;
    sFirstInitFile = __FILE__;
    sFirstInitLine = __LINE__;
    auto new_logger =
        spdlog::stdout_color_mt<spdlog::async_factory>("async_logger");
    new_logger->set_level(spdlog::level::trace);
    new_logger->set_pattern("%^%H:%M:%S:%e [%P-%t] [%1!L] [%20s:%-4#] - %v%$");
    std::atomic_store(&sSpdLogger, new_logger);
}

// 将日志写入文件
// @param sFileName: 日志文件名
// @return: 是否成功写入文件
bool Logger::WriteToFile(std::string_view sFileName) {
    std::lock_guard<std::mutex> lock(sLoggerMutex);
    if (!sSpdLogger) {
        std::cerr << "Logger not initialized!" << std::endl;
        return false;
    }
    try {
        _bWriteToFile.store(true, std::memory_order_release);
        {
            std::lock_guard<std::mutex> fileNameLock(_fileNameMutex);
            _sLogFileName = sFileName;
        }
        auto logger = std::atomic_load(&sSpdLogger);
        // 获取已有的控制台sink
        auto console_sink = logger->sinks().front();
        // 创建文件sink
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            std::string(sFileName));
        // 创建新的logger,同时输出到控制台和文件
        auto new_logger = std::make_shared<spdlog::logger>(
            "async_logger", spdlog::sinks_init_list({console_sink, file_sink}));
        // 复制原logger的日志级别和格式
        new_logger->set_level(sSpdLogger->level());
        new_logger->set_pattern(
            "%^%H:%M:%S:%e [%P-%t] [%1!L] [%20s:%-4#] - %v%$");
        std::atomic_store(&sSpdLogger, new_logger);
        return true;
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Failed to create log file: " << ex.what() << std::endl;
        _bWriteToFile.store(false, std::memory_order_release);
        return false;
    }
}

void Logger::SetLogFilePath(std::string_view sNewFileName) {
    if (!sSpdLogger || !_bWriteToFile.load(std::memory_order_acquire)) {
        return;
    }
    {
        std::lock_guard<std::mutex> fileNameLock(_fileNameMutex);
        if (sNewFileName == _sLogFileName) {
            return;
        }
        _sLogFileName = sNewFileName;
    }
    try {
        // 获取当前的所有sink
        auto sinks = sSpdLogger->sinks();
        // 获取控制台sink
        auto console_sink = sinks[0];
        // 创建新的文件sink
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            std::string(sNewFileName));
        // 创建新的logger,保持原有的控制台sink并添加新的文件sink
        auto new_logger = std::make_shared<spdlog::logger>(
            "async_logger", spdlog::sinks_init_list({console_sink, file_sink}));
        // 复制原logger的设置
        new_logger->set_level(sSpdLogger->level());
        new_logger->set_pattern(
            "%^%H:%M:%S:%e [%P-%t] [%1!L] [%20s:%-4#] - %v%$");
        // 替换原来的logger - 使用原子操作
        std::atomic_store(&sSpdLogger, new_logger);
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Failed to set new log file: " << ex.what() << std::endl;
        // 保持使用原来的日志文件
        // std::lock_guard<std::mutex> fileNameLock(_fileNameMutex);
        // _sLogFileName = sNewFileName;
        return;
    }
}

void Logger::log(spdlog::source_loc loc, spdlog::level::level_enum lvl,
                 const spdlog::memory_buf_t* buffer) {
    auto logger = std::atomic_load(&sSpdLogger);
    if (logger) {
        logger->log(loc, lvl,
                    spdlog::string_view_t(buffer->data(), buffer->size()));
    }
}

} // namespace zws