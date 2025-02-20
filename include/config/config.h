#ifndef ZENER_CONFIG_H
#define ZENER_CONFIG_H

#include "common.h"

#include <map>
#include <string>

namespace zws {

#ifdef USE_TOMLPLUSPLUS
struct Config {};
#else

struct Config {
    std::string configPath;

  public:
    static bool Init(std::string const& configPath);

    static inline Config& getInstance() {
        static Config instance;
        return instance;
    }

  private:
    static bool read(std::string const& filename,
                     std::map<std::string, std::string>& config);

    Config() : configPath(ZENER_CONFIG_FILEPATH) {}

    Config(Config const&) = delete;
    Config& operator=(Config const&) = delete;
};

#endif // !USE_TOMLPLUSPLUS

} // namespace zws

#endif // !ZENER_CONFIG_H
