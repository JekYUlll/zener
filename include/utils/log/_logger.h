#ifndef ZENER_M_LOGGER_H
#define ZENER_M_LOGGER_H

/// @JekYUlll
/// TODO
/// 自己实现 logger
/// 不依赖 spdlog 的版本
/// created 2025/02/18

#include <mutex>

namespace zws {

class MLogger {
  public:
    ~MLogger() = default;

    static void Init();

    inline static MLogger& GetInstance() { return instance; }

  private:
    MLogger() = default;
    MLogger(const MLogger&) = delete;
    MLogger(MLogger&&) = delete;
    MLogger& operator=(const MLogger&) = delete;

    static MLogger instance;

    const char* _path;
    std::mutex _mtx;
};

} // namespace zws

#endif // !ZENER_M_LOGGER_H
