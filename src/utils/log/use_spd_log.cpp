#include "utils/log/use_spd_log.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>

namespace zener {

Logger Logger::_instance{};
static std::shared_ptr<spdlog::logger> sSpdLogger{};
std::atomic<bool> Logger::_sInitialized{false};
std::string Logger::_logFileName{};
std::string Logger::_logDirectory{};
std::string Logger::_filePrefix{"zener"};
bool Logger::_usingRotation{false}; // 初始化轮转标志
// 添加互斥锁保护日志操作
static std::mutex sLoggerMutex;

Logger::~Logger() {
    Flush();
    Shutdown();
}

void Logger::Init() {
    // 使用互斥锁保护初始化过程
    std::lock_guard<std::mutex> lock(sLoggerMutex);

    if (_sInitialized.load(std::memory_order_acquire)) {
        return;
    }
    try {
        auto console_sink =
            std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        sSpdLogger = std::make_shared<spdlog::logger>("main", console_sink);

        sSpdLogger->set_level(spdlog::level::trace);
        sSpdLogger->set_pattern(LOG_PATTERN);

        spdlog::register_logger(sSpdLogger);
        spdlog::flush_every(std::chrono::seconds(3));

        _sInitialized.store(true, std::memory_order_release);

        if (sSpdLogger) {
            sSpdLogger->info("New Session Start =========================>");
        }
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Logger initialization failed: " << ex.what() << std::endl;
    } catch (const std::exception& ex) {
        std::cerr << "Logger initialization failed with exception: "
                  << ex.what() << std::endl;
    }
}

// 生成包含日期的日志文件名
std::string Logger::GenerateLogFileName(const std::string& prefix) {
    try {
        const auto now = std::chrono::system_clock::now();
        const auto now_time = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << prefix << "_";
        ss << std::put_time(std::localtime(&now_time), "%Y-%m-%d");
        ss << ".log";
        return ss.str();
    } catch (const std::exception& ex) {
        std::cerr << "Error generating log filename: " << ex.what()
                  << std::endl;
        return prefix + "_error.log"; // 提供一个备用文件名
    }
}

bool Logger::WriteToFile(const std::string_view logDir) {
    return WriteToFile(logDir, _filePrefix);
}

bool Logger::WriteToFile(const std::string_view logDir,
                         const std::string_view prefix) {
    // 使用互斥锁保护文件操作
    std::lock_guard<std::mutex> lock(sLoggerMutex);

    if (!_sInitialized.load(std::memory_order_acquire) || !sSpdLogger) {
        return false;
    }
    try {
        _logDirectory = logDir;
        _filePrefix = prefix;

        const std::filesystem::path dirPath(_logDirectory);
        if (!exists(dirPath)) { // 创建日志目录(如果不存在)
            if (!create_directories(dirPath)) {
                std::cerr << "Failed to create log directory: " << _logDirectory
                          << std::endl;
                return false;
            }
        }
        const std::string fileName =
            GenerateLogFileName(std::string(_filePrefix));
        const std::filesystem::path fullPath = dirPath / fileName;
        _logFileName = fullPath.string();

        const bool fileExists = exists(fullPath);

        // 创建新的日志器前先缓存当前日志级别
        const auto currentLevel = sSpdLogger->level();

        // 创建新的sink
        const auto console_sink =
            std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        const auto file_sink =
            std::make_shared<spdlog::sinks::basic_file_sink_mt>(
                _logFileName, false); // false表示追加模式，true表示截断模式

        // 设置文件sink的缓冲区刷新策略，减少I/O操作
        file_sink->set_pattern(LOG_PATTERN);

        std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
        const auto new_logger = std::make_shared<spdlog::logger>(
            "main", sinks.begin(), sinks.end());

        new_logger->set_level(currentLevel);
        new_logger->set_pattern(LOG_PATTERN);

        // 安全地替换日志器
        try {
            if (spdlog::get("main")) {
                spdlog::drop("main");
            }
            spdlog::register_logger(new_logger);
            sSpdLogger = new_logger;
        } catch (const std::exception& ex) {
            std::cerr << "Error replacing logger: " << ex.what() << std::endl;
            // 如果替换失败，继续使用旧的日志器
            return false;
        }

        if (fileExists) {
            sSpdLogger->info("====================Append to existing "
                             "log====================");
        }

        sSpdLogger->info("Log file created/opened: {}", _logFileName);
        return true;
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Failed to create log file: " << ex.what() << std::endl;
        return false;
    } catch (const std::exception& ex) {
        std::cerr << "Failed with exception: " << ex.what() << std::endl;
        return false;
    }
}

void Logger::SetLogFilePath(const std::string_view fileName) {
    try {
        // 提取目录部分
        const std::filesystem::path filePath(fileName);
        const auto parentPath = filePath.parent_path().string();
        WriteToFile(parentPath.empty()
                        ? "."
                        : parentPath); // 使用新接口，根据日期自动生成文件名
    } catch (const std::exception& ex) {
        std::cerr << "Error setting log file path: " << ex.what() << std::endl;
    }
}

void Logger::Flush() {
    std::lock_guard<std::mutex> lock(sLoggerMutex);
    if (sSpdLogger) {
        try {
            sSpdLogger->flush();
        } catch (const std::exception& ex) {
            std::cerr << "Error flushing logger: " << ex.what() << std::endl;
        }
    }
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock(sLoggerMutex);
    try {
        Flush();
        spdlog::shutdown();
        sSpdLogger.reset();
        _sInitialized.store(false, std::memory_order_release);
    } catch (const std::exception& ex) {
        std::cerr << "Error during logger shutdown: " << ex.what() << std::endl;
    }
}

void Logger::log(const spdlog::source_loc& loc,
                 const spdlog::level::level_enum lvl,
                 const spdlog::memory_buf_t* buffer) {
    // 快速检查避免获取锁
    if (!_sInitialized.load(std::memory_order_acquire) || !sSpdLogger) {
        return;
    }

    // 使用共享锁进行日志记录，允许多个线程同时读取但保护写入操作
    std::lock_guard<std::mutex> lock(sLoggerMutex);

    if (sSpdLogger) {
        try {
            sSpdLogger->log(
                loc, lvl,
                spdlog::string_view_t(buffer->data(), buffer->size()));
        } catch (const std::exception& ex) {
            // 避免在日志记录中抛出异常导致程序崩溃
            std::cerr << "Error logging message: " << ex.what() << std::endl;
        }
    }
}

bool Logger::WriteToFileWithRotation(const std::string_view logDir,
                                     const std::string_view prefix,
                                     size_t max_size, size_t max_files) {
    // 使用互斥锁保护文件操作
    std::lock_guard<std::mutex> lock(sLoggerMutex);

    if (!_sInitialized.load(std::memory_order_acquire) || !sSpdLogger) {
        return false;
    }

    try {
        _logDirectory = logDir;
        _filePrefix = prefix;
        _usingRotation = true;

        const std::filesystem::path dirPath(_logDirectory);
        if (!exists(dirPath)) { // 创建日志目录(如果不存在)
            if (!create_directories(dirPath)) {
                std::cerr << "Failed to create log directory: " << _logDirectory
                          << std::endl;
                return false;
            }
        }

        // 构建日志文件基础名称（不包含 .log 后缀，rotating sink 会自动添加）
        std::string fileName = std::string(_filePrefix);
        const std::filesystem::path fullPath = dirPath / (fileName + ".log");
        _logFileName = fullPath.string();

        // 缓存当前日志级别
        const auto currentLevel = sSpdLogger->level();

        // 创建新的sink
        const auto console_sink =
            std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        const auto rotating_file_sink =
            std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                _logFileName, max_size, max_files);

        // 设置文件sink的格式
        rotating_file_sink->set_pattern(LOG_PATTERN);
        console_sink->set_pattern(LOG_PATTERN);

        std::vector<spdlog::sink_ptr> sinks{console_sink, rotating_file_sink};
        const auto new_logger = std::make_shared<spdlog::logger>(
            "main", sinks.begin(), sinks.end());

        new_logger->set_level(currentLevel);
        new_logger->set_pattern(LOG_PATTERN);

        // 安全地替换日志器
        try {
            if (spdlog::get("main")) {
                spdlog::drop("main");
            }
            spdlog::register_logger(new_logger);
            sSpdLogger = new_logger;

            // 配置定期刷新
            spdlog::flush_every(std::chrono::seconds(3));

        } catch (const std::exception& ex) {
            std::cerr << "Error replacing logger: " << ex.what() << std::endl;
            // 如果替换失败，继续使用旧的日志器
            return false;
        }

        sSpdLogger->info("Log rotation enabled: max_size={}MB, max_files={}",
                         max_size / (1024 * 1024), max_files);
        sSpdLogger->info("Log file created with rotation: {}", _logFileName);
        return true;
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Failed to create rotating log file: " << ex.what()
                  << std::endl;
        return false;
    } catch (const std::exception& ex) {
        std::cerr << "Failed with exception: " << ex.what() << std::endl;
        return false;
    }
}

} // namespace zener