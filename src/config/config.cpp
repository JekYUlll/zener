#include "config/config.h"
#include "utils/hash.hpp"
#include "utils/log/logger.h"
#include <cassert>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace zws {

Config Config::_instance;

std::unordered_map<std::string, std::string> Config::_configMap;

bool Config::read(std::string const& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        LOG_E("Failed to open config file: {}", filename);
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

bool Config::Init(std::string const& configPath) {
    auto& config = getInstance();
    // std::map<std::string, std::string> configMap;
    if (!read(configPath)) {
        LOG_E("Failed to read config file");
        return false;
    }
    for (auto const& [key, value] : _configMap) {
        try {
            switch (hash_str(key.c_str(), key.length())) {
            // case "window.width"_hash:
            //     config.windowWidth = std::stoi(value);
            //     break;
            // case "window.height"_hash:
            //     config.windowHeight = std::stoi(value);
            //     break;
            // case "fps.display"_hash:
            //     config.displayFPS = std::stoi(value);
            //     break;
            // case "fps.record"_hash: config.recordFPS = std::stoi(value);
            // break; case "fps.compare"_hash:
            //     config.compareFPS = std::stoi(value);
            //     break;
            // case "action.standardPath"_hash: config.standardPath = value;
            // break; case "action.bufferSize"_hash:
            //     config.actionBufferSize = std::stoi(value);
            //     break;
            // case "similarity.speedWeight"_hash:
            //     config.speedWeight = std::stof(value);
            //     break;
            // case "similarity.minSpeedRatio"_hash:
            //     config.minSpeedRatio = std::stof(value);
            //     break;
            // case "similarity.maxSpeedRatio"_hash:
            //     config.maxSpeedRatio = std::stof(value);
            //     break;
            // case "similarity.minSpeedPenalty"_hash:
            //     config.minSpeedPenalty = std::stof(value);
            //     break;
            // case "similarity.dtwBandwidthRatio"_hash:
            //     config.dtwBandwidthRatio = std::stof(value);
            //     break;
            // case "similarity.threshold"_hash:
            //     config.similarityThreshold = std::stof(value);
            //     break;
            // case "similarity.difficulty"_hash:
            //     config.difficulty = std::stoi(value);
            //     break;
            // case "similarity.similarityHistorySize"_hash:
            //     config.similarityHistorySize = std::stoi(value);
            //     break;
            default:
                LOG_W("Unknown config key: {}", key);
                break;
            }
        } catch (std::exception const& e) {
            LOG_E("Error parsing config value for {}: {}", key, e.what());
        }
    }

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

    return true;
}

const char* Config::GetConfig(const std::string& key) {
    static std::string cache;
    auto it = _configMap.find(key);
    if (it != _configMap.end()) {
        cache = it->second;
        return cache.c_str();
    }
    LOG_W("Config '{}' not found", key);
    return nullptr;
}

} // namespace zws
