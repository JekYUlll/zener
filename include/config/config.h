#ifndef ZENER_CONFIG_H
#define ZENER_CONFIG_H

#include "common.h"

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>

namespace zws {

#ifdef USE_TOMLPLUSPLUS
struct Config {};
#else // !USE_TOMLPLUSPLUS

struct Config {
    std::string configPath;

  public:
    static bool Init(const std::string& configPath);

    _ZENER_SHORT_FUNC static bool Initialized() {
        return _initialized.load(std::memory_order_acquire);
    }

    _ZENER_SHORT_FUNC static Config& GetInstance() { return _instance; }

    static void Print();

    const std::string& GetConfig(const std::string& key) const;
    // 加锁的版本，多线程用
    const std::string& GetConfigSafe(const std::string& key) const;

  private:
    Config() : configPath(ZENER_CONFIG_FILEPATH) {}
    Config(const Config&) = delete;
    Config& operator=(Config const&) = delete;
    static Config _instance;

    mutable std::mutex _mtx;

    static bool read(const std::string& filename);

    static std::unordered_map<std::string, std::string> _configMap;

    static std::atomic<bool> _initialized;
};

#define GET_CONFIG(key) Config::GetInstance().GetConfig(key)

// 使用 std::stoul （stoul 代表 string to unsigned long）来替代 atoi，因为
// std::stoul 可以在遇到无效输入时抛出异常

#endif // !USE_TOMLPLUSPLUS

} // namespace zws

#endif // !ZENER_CONFIG_H
