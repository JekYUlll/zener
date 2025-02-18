#include "config/config.h"
#include "log/logger.h"
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace zws {

bool Config::read(const std::string& filename,
                  std::map<std::string, std::string>& config) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        LOG_E("failed to open config file: {}", filename);
        return false;
    }
    std::string line;
    std::string currentSection;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        // 去除首尾空格
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(0, line.find_last_not_of(" \t") + 1);
        // 处理 TOML 节
        if (line[0] == '[' && line[line.length() - 1] == ']') {
            currentSection = line.substr(1, line.length() - 2);
            continue;
        }
        std::istringstream iss(line);
        std::string key, value;

        if (std::getline(iss, key, '=') && std::getline(iss, value)) {
            key.erase(0, line.find_first_not_of(" \t"));
            key.erase(0, line.find_last_not_of(" \t") + 1);
            value.erase(0, line.find_first_not_of(" \t"));
            value.erase(0, line.find_last_not_of(" \t") + 1);
        }
    }
}

bool Config::Init(const std::string& configPath) {
    auto& config = getInstance();
    std::map<std::string, std::string> configMap;

    if (!read(configPath, configMap)) {
        LOG_E("failed to read config file.");
        return false;
    }

    return false;
}

} // namespace zws