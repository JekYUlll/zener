#ifndef LOG_H
#define LOG_H

#include "spdlog/common.h"
#include <atomic>
#include <mutex>

namespace zws {

class Logger {
  public:
    static Logger* GetLoggerInstance() { return &_instance; }

    Logger(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger& operator=(Logger&&) = delete;
    ~Logger();

    static void Init();
    static bool WriteToFile(std::string_view fileName);
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

    [[nodiscard]] static std::string GetLogFileName() {
        if (!_bWriteToFile) {
            return "";
        }
        return _logFileName;
    }

    [[nodiscard]] static bool IsWriteToFile() { return _bWriteToFile; }
    [[nodiscard]] static bool Initialized() { return _sInitialized.load(std::memory_order_acquire); }

  private:
    Logger() = default;

    static void log(const spdlog::source_loc &loc, spdlog::level::level_enum lvl,
                    const spdlog::memory_buf_t* buffer);

    static Logger _instance;
    static std::atomic<bool> _bWriteToFile;
    static std::string _logFileName;
    static std::mutex _fileNameMtx;
    static std::atomic<bool> _sInitialized;
};

#define ZENER_LOG_LOGGER_CALL(adlog, level, ...)                               \
    (adlog)->Log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION},      \
                 level, __VA_ARGS__)

} // namespace zws

#endif // LOG_H
