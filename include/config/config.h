#ifndef ZWS_CONFIG_H
#define ZWS_CONFIG_H

#include "common.h"
#include <map>
#include <string>

namespace zws {

struct Config {
    std::string configPath;

  public:
    static bool Init(const std::string& configPath);
    static inline Config& getInstance() {
        static Config instance;
        return instance;
    }

  private:
    static bool read(const std::string& filename,
                     std::map<std::string, std::string>& config);

    Config() : configPath(ZWS_CONFIG_FILEPATH) {}
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
};
} // namespace zws

#endif // !ZWS_CONFIG_H
