#ifndef ZENER_CONFIG_H
#define ZENER_CONFIG_H

#include "common.h"

#include <string>
#include <unordered_map>

namespace zws {

#ifdef USE_TOMLPLUSPLUS
struct Config {};
#else // !USE_TOMLPLUSPLUS

struct Config {
    std::string configPath;

  public:
    static bool Init(std::string const& configPath);

    _ZENER_SHORT_FUNC static Config& getInstance() { return _instance; }

    const char* GetConfig(const std::string& key);

  private:
    static Config _instance;

    static bool read(std::string const& filename);

    Config() : configPath(ZENER_CONFIG_FILEPATH) {}

    Config(Config const&) = delete;
    Config& operator=(Config const&) = delete;

    static std::unordered_map<std::string, std::string> _configMap;
};

#define GET_CONFIG(key) Config::getInstance().GetConfig(key)

#endif // !USE_TOMLPLUSPLUS

} // namespace zws

#endif // !ZENER_CONFIG_H
