#include "utils/log/use_spd_log.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"

#include <atomic>
#include <iostream>
#include <memory>

namespace zws {

Logger Logger::_instance{};
static std::shared_ptr<spdlog::logger> sSpdLogger{};
std::atomic<bool> Logger::_sInitialized{false};
std::string Logger::_logFileName{};

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
            "%^%H:%M:%S:%e [%P-%t] [%1!L] [%20s:%-4#] - %v%$");

        spdlog::register_logger(sSpdLogger);
        spdlog::flush_every(std::chrono::seconds(3));

        _sInitialized = true;

        if (sSpdLogger) {
            sSpdLogger->info("\n=========================== New Session "
                             "Started ===========================\n");
        }
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Logger initialization failed: " << ex.what() << std::endl;
    }
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

bool Logger::WriteToFile(const std::string_view fileName) {
    if (!_sInitialized || !sSpdLogger) {
        return false;
    }

    try {
        _logFileName = fileName;

        auto console_sink =
            std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            std::string(fileName), false);

        std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
        auto new_logger = std::make_shared<spdlog::logger>(
            "main", sinks.begin(), sinks.end());

        new_logger->set_level(sSpdLogger->level());
        new_logger->set_pattern(
            "%^%H:%M:%S:%e [%P-%t] [%1!L] [%20s:%-4#] - %v%$");

        spdlog::drop("main");
        spdlog::register_logger(new_logger);

        sSpdLogger = new_logger;
        return true;
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Failed to create log file: " << ex.what() << std::endl;
        return false;
    }
}

void Logger::SetLogFilePath(const std::string_view fileName) {
    WriteToFile(fileName);
}

void Logger::log(const spdlog::source_loc& loc,
                 const spdlog::level::level_enum lvl,
                 const spdlog::memory_buf_t* buffer) {
    if (sSpdLogger) {
        sSpdLogger->log(loc, lvl,
                        spdlog::string_view_t(buffer->data(), buffer->size()));
    }
}

} // namespace zws