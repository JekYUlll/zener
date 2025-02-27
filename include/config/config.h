#ifndef ZENER_CONFIG_H
#define ZENER_CONFIG_H

// TODO:
// 添加配置的一些持久化，保持到本地（数据库读取依赖于config，似乎不能存数据库）

#include "common.h"

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>

namespace zener {

#ifdef USE_TOMLPLUSPLUS
struct Config {};
#else // !USE_TOMLPLUSPLUS

struct Config {
    std::string configPath;

    Config(const Config&) = delete;
    Config& operator=(Config const&) = delete;

    static bool Init(const std::string& configPath);

    _ZENER_SHORT_FUNC static bool Initialized() {
        return _initialized.load(std::memory_order_acquire);
    }

    _ZENER_SHORT_FUNC static Config& GetInstance() { return _instance; }

    static void Print();

    static const std::string& GetConfig(const std::string& key);
    // 加锁的版本，多线程用
    const std::string& GetConfigSafe(const std::string& key) const;

  private:
    Config() : configPath(ZENER_CONFIG_FILEPATH) {}

    static Config _instance;

    mutable std::mutex _mtx;

    static bool read(const std::string& filename);

    // 使用哈希表而不是红黑树，此处好处是查找更快，坏处是 Print
    // 的时候键不是有序的
    static std::unordered_map<std::string, std::string> _configMap;

    static std::atomic<bool> _initialized;
};

#define GET_CONFIG(key) Config::GetInstance().GetConfig(key)

// 使用 std::stoul （stoul 代表 string to unsigned long）来替代 atoi，因为
// std::stoul 可以在遇到无效输入时抛出异常

#endif // !USE_TOMLPLUSPLUS

} // namespace zener

#endif // !ZENER_CONFIG_H
