#ifndef ZENER_SPD_LOGGER_H
#define ZENER_SPD_LOGGER_H

#include "spdlog/common.h"
#include <atomic>
#include <string>

namespace zener {

class Logger {
  public:
    static Logger* GetLoggerInstance() { return &_instance; }

    Logger(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger& operator=(Logger&&) = delete;
    ~Logger();

    static void Init();
    
    // 使用日期自动生成日志文件名，并创建日志文件
    static bool WriteToFile(std::string_view logDir);
    
    // 兼容旧接口，但现在会自动根据日期生成文件名
    static bool WriteToFile(std::string_view logDir, std::string_view prefix);
    
    // 旧接口保留，但内部会自动处理文件名
    static void SetLogFilePath(std::string_view fileName);
    
    static void Flush();
    static void Shutdown();

    template <typename... Args>
    void Log(const spdlog::source_loc loc, const spdlog::level::level_enum lvl,
             spdlog::format_string_t<Args...> fmt, Args&&... args) {
        spdlog::memory_buf_t buf;
        fmt::vformat_to(fmt::appender(buf), fmt,
                        fmt::make_format_args(args...));
        log(loc, lvl, &buf);
    }

    [[nodiscard]] static std::string GetLogFileName() { return _logFileName; }
    [[nodiscard]] static std::string GetLogDirectory() { return _logDirectory; }
    [[nodiscard]] static bool Initialized() { return _sInitialized; }

  private:
    Logger() = default;

    static void log(const spdlog::source_loc& loc,
                    spdlog::level::level_enum lvl,
                    const spdlog::memory_buf_t* buffer);
                    
    // 根据当前日期生成日志文件名
    static std::string GenerateLogFileName(const std::string& prefix);

    static Logger _instance;
    static std::string _logFileName;  // 当前日志文件的完整路径
    static std::string _logDirectory; // 日志文件存储目录
    static std::string _filePrefix;   // 日志文件名前缀，默认为"zener"
    static std::atomic<bool> _sInitialized;
};

#define ZENER_LOG_LOGGER_CALL(adlog, level, ...)                               \
    (adlog)->Log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION},      \
                 level, __VA_ARGS__)

} // namespace zener

#endif // !ZENER_SPD_LOGGER_H
