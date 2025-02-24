#include "config/config.h"
#include "utils/log/logger.h"
// #include "utils/hash.hpp"

#include <atomic>
#include <cassert>
#include <cstddef>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>

namespace zws {

Config Config::_instance;

std::unordered_map<std::string, std::string> Config::_configMap;

std::atomic<bool> Config::_initialized = false;

bool Config::read(std::string const& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        LOG_E("failed to open config file: {}", filename);
        return false;
    }
    std::string line;
    std::string currentSection;
    while (std::getline(file, line)) {
        // 跳过空行和注释
        if (line.empty() || line[0] == '#') {
            continue;
        }
        // 去除首尾空格
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);
        // 处理TOML节
        if (line[0] == '[' && line[line.length() - 1] == ']') {
            currentSection = line.substr(1, line.length() - 2);
            continue;
        }
        std::istringstream iss(line);
        std::string key, value;
        if (std::getline(iss, key, '=') && std::getline(iss, value)) {
            // 去除首尾空格
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);

            // 如果在某个节中，添加节名作为前缀
            if (!currentSection.empty()) {
                key = currentSection + "." + key;
            }
            // 移除注释
            size_t commentPos = value.find('#');
            if (commentPos != std::string::npos) {
                value = value.substr(0, commentPos);
                value.erase(value.find_last_not_of(" \t") + 1);
            }
            // 移除引号（如果存在）
            if (value.length() >= 2 && value.front() == '"' &&
                value.back() == '"') {
                value = value.substr(1, value.length() - 2);
            }
            _configMap[key] = value;
        }
    }
    return true;
}

bool Config::Init(const std::string& configPath) {
    if (Initialized()) {
        LOG_D("already initilized before");
        return true;
    }
    auto& config = GetInstance();
    if (!read(configPath)) {
        LOG_E("failed to read config file: {}", configPath);
        return false;
    }
    // 如果需要在初始化的时候直接进行判断和赋值。暂时不需要
    // for (auto const& [key, value] : _configMap) {
    //     try {
    //         switch (hash_str(key.c_str(), key.length())) {
    //             // case "window.width"_hash:
    //             //     config.windowWidth = std::stoi(value);
    //             //     break;
    //         default:
    //             LOG_W("Unknown config key: {}", key);
    //             break;
    //         }
    //     } catch (std::exception const& e) {
    //         LOG_E("Error parsing config value for {}: {}", key, e.what());
    //     }
    // }
    // LOG_I("Configuration loaded:\n"
    //       "  Window: {}x{}\n"
    //       "  FPS: display={}, record={}, compare={}\n"
    //       "  Standard action: {}\n"
    //       "  Similarity: weight={:.2f}, speedRatio={:.2f}-{:.2f}, "
    //       "penalty={:.2f}, "
    //       "bandWidth={:.2f}, threshold={:.2f}",
    //       config.windowWidth, config.windowHeight, config.displayFPS,
    //       config.recordFPS, config.compareFPS, config.standardPath,
    //       config.speedWeight, config.minSpeedRatio, config.maxSpeedRatio,
    //       config.minSpeedPenalty, config.dtwBandwidthRatio,
    //       config.similarityThreshold);
    _initialized.store(true, std::memory_order_release);
    return true;
}

void Config::Print() {
    if (!Initialized()) {
        LOG_W("init before {}", __FUNCTION__);
        return;
    }
    LOG_I("===================== config loaded =====================");
    for (auto [key, val] : _configMap) {
        LOG_I("{0} : {1}", key, val);
    }
    LOG_I("=========================================================");
}

const std::string& Config::GetConfig(const std::string& key) const {
    static const std::string empty;
    if (!Initialized()) {
        LOG_W("init before {}", __FUNCTION__);
        return empty;
    }
    auto it = _configMap.find(key);
    if (it != _configMap.end()) {
        return it->second;
    }
    LOG_W("config '{}' not found", key);
    return empty;
}

const std::string& Config::GetConfigSafe(const std::string& key) const {
    static const std::string empty;
    if (!Initialized()) {
        LOG_W("init before {}", __FUNCTION__);
        return empty;
    }
    // TODO 增加一个超时取消
    {
        std::lock_guard<std::mutex> locker(_mtx);
        auto it = _configMap.find(key);
        if (it != _configMap.end()) {
            return it->second;
        }
        LOG_W("config '{}' not found", key);
    }
    return empty;
}

} // namespace zws
