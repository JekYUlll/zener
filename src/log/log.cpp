#include "log/log.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/async.h"
#include <memory>

namespace zws {

    Logger Logger::sLoggerInstance{};
    static std::shared_ptr<spdlog::logger> sSpdLogger{};
    
    bool Logger::_bWriteToFile = false;
    std::string Logger::_sLogFileName{};

    Logger::Logger() {
        // 构造函数为空,初始化在Init()中完成
    }

    void Logger::Init() {
        sSpdLogger = spdlog::stdout_color_mt<spdlog::async_factory>("async_logger");
        sSpdLogger->set_level(spdlog::level::trace);
        sSpdLogger->set_pattern("%^%H:%M:%S:%e [%P-%t] [%1!L] [%20s:%-4#] - %v%$");
    }

    void Logger::WriteToFile(std::string_view sFileName) {
        _bWriteToFile = true;
        if (!sSpdLogger) {
            return;
        }
        _sLogFileName = sFileName;
        // 创建文件sink - 用于写入文件
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(std::string(sFileName));
        // 获取当前的控制台sink - 用于输出到终端
        auto console_sink = sSpdLogger->sinks().front();
        // 创建新的logger，同时使用两个sink，这样日志会同时输出到控制台和文件
        auto new_logger = std::make_shared<spdlog::logger>("async_logger", 
            spdlog::sinks_init_list({console_sink, file_sink}));
        // 复制原logger的设置
        new_logger->set_level(sSpdLogger->level());
        new_logger->set_pattern("%^%H:%M:%S:%e [%P-%t] [%1!L] [%20s:%-4#] - %v%$");
        // 替换原来的logger
        sSpdLogger = new_logger;
    }

    void Logger::SetLogFilePath(std::string_view sNewFileName) {
        if (!sSpdLogger || !_bWriteToFile) {  // 检查logger存在且已启用文件输出
            return;
        }
        if (sNewFileName == _sLogFileName) {  // 如果文件名没变，直接返回
            return;
        }
        try {
            _sLogFileName = sNewFileName;
            // 获取当前的所有sink
            auto sinks = sSpdLogger->sinks();
            // 获取控制台sink
            auto console_sink = sinks[0];
            // 创建新的文件sink
            auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(std::string(sNewFileName));
            // 创建新的logger,保持原有的控制台sink并添加新的文件sink
            auto new_logger = std::make_shared<spdlog::logger>("async_logger",
                spdlog::sinks_init_list({console_sink, file_sink}));
            // 复制原logger的设置
            new_logger->set_level(sSpdLogger->level());
            new_logger->set_pattern("%^%H:%M:%S:%e [%P-%t] [%1!L] [%20s:%-4#] - %v%$");
            // 替换原来的logger
            sSpdLogger = new_logger;
        } catch (const spdlog::spdlog_ex& ex) {
            // 如果创建文件失败，恢复原来的文件名
            _sLogFileName = sNewFileName;
            return;
        }
    }

    void Logger::log(spdlog::source_loc loc, spdlog::level::level_enum lvl, const spdlog::memory_buf_t *buffer) {
        sSpdLogger->log(loc, lvl, spdlog::string_view_t(buffer->data(), buffer->size()));
    }


}