#ifndef LOG_H
#define LOG_H

#include "spdlog/common.h"
#include <atomic>
#include <mutex>

namespace zws {

class Logger {
  public:
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    static void Init();
    static bool WriteToFile(std::string_view sFileName);

    static Logger* GetLoggerInstance() { return &sLoggerInstance; }

    template <typename... Args>
    void Log(spdlog::source_loc loc, spdlog::level::level_enum lvl,
             spdlog::format_string_t<Args...> fmt, Args&&... args) {
        spdlog::memory_buf_t buf;
        fmt::vformat_to(fmt::appender(buf), fmt,
                        fmt::make_format_args(args...));
        log(loc, lvl, &buf);
    }

    [[nodiscard]] static std::string GetLogFileName() {
        if (_bWriteToFile) {
            return _sLogFileName;
        }
        return "";
    }

    [[nodiscard]] static bool IsWriteToFile() { return _bWriteToFile; }

    static void SetLogFilePath(std::string_view sNewFileName);

  private:
    Logger();

    static Logger sLoggerInstance;
    static std::atomic<bool> _bWriteToFile;
    static std::string _sLogFileName;
    static std::mutex _fileNameMutex;

    void log(spdlog::source_loc loc, spdlog::level::level_enum lvl,
             const spdlog::memory_buf_t* buffer);
};

#define ZWS_LOG_LOGGER_CALL(adlog, level, ...)                                 \
    (adlog)->Log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION},      \
                 level, __VA_ARGS__)

} // namespace zws

#endif // LOG_H
