#include "utils/log/use_spd_log.h"
#include "spdlog/spdlog.h" // 不得不再次引入。文件变大了 从几百 KB 变成 12 MB, 心态爆炸 TODO 单拿出来
#include "spdlog/async.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"

#include <atomic>
#include <iostream>
#include <memory>
#include <mutex>
#include <utils/log/logger.h>

namespace zws {

Logger Logger::_instance{};
static std::shared_ptr<spdlog::logger> sSpdLogger{}; // 初始化生成一个空指针

std::string Logger::_logFileName{};

std::atomic<bool> Logger::_bWriteToFile{false};
std::atomic<bool> Logger::_sInitialized{false};
std::mutex Logger::_fileNameMtx;

static std::mutex sLoggerMtx;

Logger::~Logger() {
    Flush();
    Shutdown();
}

void Logger::Init() {
    if (_sInitialized.load(std::memory_order_relaxed)) {
        return;
    }
    std::lock_guard lock(sLoggerMtx);
    /*
     * 异步模式需要显式初始化线程池
     * 必须在创建异步 logger 前调用
     */
    spdlog::init_thread_pool(32768, 2); // 32 kb, 2 线程
    const auto logger =
        spdlog::stdout_color_mt<spdlog::async_factory>("async_logger");
    logger->set_level(spdlog::level::trace);
    logger->set_pattern("%^%H:%M:%S:%e [%P-%t] [%1!L] [%20s:%-4#] - %v%$");
    std::atomic_store(&sSpdLogger, logger);
    // 添加 flush 规则
    spdlog::flush_every(std::chrono::seconds(3)); // 定时自动刷新
    if(logger) {
        sSpdLogger->flush_on(spdlog::level::err); // 错误级别立即刷新
    }
    _sInitialized.store(true, std::memory_order_release);
}

void Logger::Flush() {
    std::lock_guard lock(sLoggerMtx);
    if (const auto logger = std::atomic_load(&sSpdLogger)) {
        logger->flush(); // 触发所有sink的flush操作
        spdlog::details::registry::instance().flush_all(); // 强制全局刷新
    }
}

void Logger::Shutdown() {
    std::lock_guard locker(sLoggerMtx);
    spdlog::shutdown(); // 停止所有后台线程并刷新[4,7](@ref)
    sSpdLogger.reset(); // 显式释放资源
    _sInitialized.store(false);
}

// 将日志写入文件
// @param sFileName: 日志文件名
// @return: 是否成功写入文件
bool Logger::WriteToFile(const std::string_view fileName) {
    LOG_D("Writing log to {}.", fileName);
    std::lock_guard lock(sLoggerMtx);
    if (!sSpdLogger) {
        std::cerr << "Logger not initialized! " << __FUNCTION__ << std::endl;
        return false;
    }
    try {
        _bWriteToFile.store(true, std::memory_order_release);
        {
            std::lock_guard locker(_fileNameMtx);
            _logFileName = fileName;
        }
        const auto logger = std::atomic_load(&sSpdLogger);
        // 获取已有的控制台sink
        auto console_sink = logger->sinks().front();
        // 创建文件sink
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            std::string(fileName));
        // 创建新的logger,同时输出到控制台和文件
        const auto new_logger = std::make_shared<spdlog::logger>(
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

void Logger::SetLogFilePath(const std::string_view fileName) {
    if (!sSpdLogger || !_bWriteToFile.load(std::memory_order_acquire)) {
        return;
    }
    LOG_D("Set log file path to {}.", fileName);
    {
        std::lock_guard locker(_fileNameMtx);
        if (fileName == _logFileName) {
            return;
        }
        _logFileName = fileName;
    }
    try {
        const auto sinks = sSpdLogger->sinks(); // 获取当前的所有sink
        auto console_sink = sinks[0]; // 获取控制台sink
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            std::string(fileName)); // 创建新的文件sink
        const auto new_logger = std::make_shared<spdlog::logger>(
            "async_logger", spdlog::sinks_init_list({console_sink, file_sink})); // 创建新的logger,保持原有的控制台sink并添加新的文件sink
        // 复制原logger的设置
        new_logger->set_level(sSpdLogger->level());
        new_logger->set_pattern(
            "%^%H:%M:%S:%e [%P-%t] [%1!L] [%20s:%-4#] - %v%$");
        std::atomic_store(&sSpdLogger, new_logger); // 替换原来的logger - 使用原子操作
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Failed to set new log file: " << ex.what() << std::endl;
        // 保持使用原来的日志文件
        // std::lock_guard fileNameLock(_fileNameMutex);
        // _sLogFileName = sNewFileName;
    }
}

void Logger::log(const spdlog::source_loc &loc, const spdlog::level::level_enum lvl,
                 const spdlog::memory_buf_t* buffer) {
    try {
        if (const auto logger = std::atomic_load(&sSpdLogger)) {
            logger->log(loc, lvl,
                        spdlog::string_view_t(buffer->data(), buffer->size()));
        }
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Failed to log: " << ex.what() << std::endl;
    }
}

} // namespace zws