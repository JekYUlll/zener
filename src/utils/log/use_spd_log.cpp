#include "utils/log/use_spd_log.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>

namespace zener {

Logger Logger::_instance{};
static std::shared_ptr<spdlog::logger> sSpdLogger{};
std::atomic<bool> Logger::_sInitialized{false};
std::string Logger::_logFileName{};
std::string Logger::_logDirectory{};
std::string Logger::_filePrefix{"zener"};

Logger::~Logger() {
    Flush();
    Shutdown();
}

void Logger::Init() {
    if (_sInitialized) {
        return;
    }
    try {
        auto console_sink =
            std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        sSpdLogger = std::make_shared<spdlog::logger>("main", console_sink);

        sSpdLogger->set_level(spdlog::level::trace);
        sSpdLogger->set_pattern(
            "%^%Y-%m-%d %H:%M:%S.%e [%P-%t] [%l] [%20s:%-4#] - %v%$");

        spdlog::register_logger(sSpdLogger);
        spdlog::flush_every(std::chrono::seconds(3));

        _sInitialized = true;

        if (sSpdLogger) {
            sSpdLogger->info("New Session Start =========================>");
        }
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Logger initialization failed: " << ex.what() << std::endl;
    }
}

// 生成包含日期的日志文件名
std::string Logger::GenerateLogFileName(const std::string& prefix) {
    auto now = std::chrono::system_clock::now();
    auto now_time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << prefix << "_";
    ss << std::put_time(std::localtime(&now_time), "%Y-%m-%d");
    ss << ".log";
    return ss.str();
}

bool Logger::WriteToFile(const std::string_view logDir) {
    return WriteToFile(logDir, _filePrefix);
}

bool Logger::WriteToFile(const std::string_view logDir, const std::string_view prefix) {
    if (!_sInitialized || !sSpdLogger) {
        return false;
    }

    try {
        // 保存日志目录
        _logDirectory = logDir;
        _filePrefix = prefix;
        
        // 创建日志目录(如果不存在)
        std::filesystem::path dirPath(_logDirectory);
        if (!std::filesystem::exists(dirPath)) {
            if (!std::filesystem::create_directories(dirPath)) {
                std::cerr << "Failed to create log directory: " << _logDirectory << std::endl;
                return false;
            }
        }
        
        // 生成基于日期的日志文件名
        std::string fileName = GenerateLogFileName(std::string(_filePrefix));
        std::filesystem::path fullPath = dirPath / fileName;
        _logFileName = fullPath.string();
        
        // 检查文件是否存在，决定是追加还是新建
        bool fileExists = std::filesystem::exists(fullPath);

        // 创建控制台和文件输出
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(_logFileName, true); // true表示追加模式

        std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
        auto new_logger = std::make_shared<spdlog::logger>(
            "main", sinks.begin(), sinks.end());

        new_logger->set_level(sSpdLogger->level());
        new_logger->set_pattern(
            "%^%Y-%m-%d %H:%M:%S.%e [%P-%t] [%l] [%20s:%-4#] - %v%$");

        spdlog::drop("main");
        spdlog::register_logger(new_logger);

        sSpdLogger = new_logger;
        
        // 如果是续写已存在的文件，添加分隔符
        if (fileExists) {
            sSpdLogger->info("====================Append to existing log====================");
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
    // 提取目录部分
    std::filesystem::path filePath(fileName);
    auto parentPath = filePath.parent_path().string();
    
    // 使用新接口，根据日期自动生成文件名
    WriteToFile(parentPath.empty() ? "." : parentPath);
}

void Logger::Flush() {
    if (sSpdLogger) {
        sSpdLogger->flush();
    }
}

void Logger::Shutdown() {
    Flush();
    spdlog::shutdown();
    sSpdLogger.reset();
    _sInitialized = false;
}

void Logger::log(const spdlog::source_loc& loc,
                 const spdlog::level::level_enum lvl,
                 const spdlog::memory_buf_t* buffer) {
    if (sSpdLogger) {
        sSpdLogger->log(loc, lvl,
                        spdlog::string_view_t(buffer->data(), buffer->size()));
    }
}

} // namespace zener