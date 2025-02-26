#include "config/config.h"
#include "utils/log/logger.h"
// #include "utils/hash.hpp"

#include <atomic>
#include <cassert>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>

namespace zener {

Config Config::_instance;

std::unordered_map<std::string, std::string> Config::_configMap;

std::atomic<bool> Config::_initialized = false;

bool Config::read(std::string const& filename) {
    namespace fs = std::filesystem;
    // 检查文件是否存在
    if (!fs::exists(filename)) {
        LOG_E("Config file does not exist: {}!", filename);
        return false;
    }
    // 莫名其妙 config.toml 没权限了导致失败，增加权限的判断和修改 --2025/02/24
    if (auto perms = fs::status(filename).permissions();
        (perms & fs::perms::owner_read) == fs::perms::none) {
        LOG_W("Config file {} lacks read permission, attempting to add...",
              filename);
        try {
            permissions(filename, fs::perms::owner_read, fs::perm_options::add);
        } catch (const fs::filesystem_error& e) {
            LOG_E("Failed to add read permission: {}!", e.what());
            return false;
        }
    }
    std::ifstream file(filename);
    if (!file.is_open()) {
        LOG_E("Failed to open config file: {}!", filename);
        return false;
    }
    std::string line;
    std::string currentSection;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') { // 跳过空行和注释
            continue;
        }
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);
        if (line[0] == '[' && line[line.length() - 1] == ']') { // 处理TOML节
            currentSection = line.substr(1, line.length() - 2);
            continue;
        }
        std::istringstream iss(line);
        if (std::string key, value;
            std::getline(iss, key, '=') && std::getline(iss, value)) {
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            if (!currentSection.empty()) { // 如果在某个节中，添加节名作为前缀
                /* 等价于：
                    key = currentSection + "." + key; //
                   一点蚊子腿性能优化，减少临时对象
                */
                std::string originalKey(std::move(key));
                key.reserve(currentSection.size() + 1 + originalKey.size());
                key.append(currentSection);
                key.append(".");
                key.append(originalKey);
            }
            if (size_t commentPos = value.find('#');
                commentPos != std::string::npos) { // 移除注释
                value = value.substr(0, commentPos);
                value.erase(value.find_last_not_of(" \t") + 1);
            }
            if (value.length() >= 2 && value.front() == '"' &&
                value.back() == '"') { // 移除引号（如果存在）
                value = value.substr(1, value.length() - 2);
            }
            _configMap[key] = value;
        }
    }
    return true;
}

bool Config::Init(const std::string& configPath) {
    if (_initialized.load(std::memory_order_acquire)) {
        return true;
    }
    if (!read(configPath)) {
        LOG_E("Failed to read config file: {}!", configPath);
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
    _initialized.store(true, std::memory_order_release);
    return true;
}

void Config::Print() {
    if (!Initialized()) {
        LOG_W("Should init config before {}!", __FUNCTION__);
        return;
    }
    LOG_I("===================== Config Loaded =====================");
    for (auto [key, val] : _configMap) {
        LOG_I("{0} : {1}", key, val);
    }
    LOG_I("=========================================================");
}

const std::string& Config::GetConfig(const std::string& key) {
    static const std::string empty;
    if (!Initialized()) {
        LOG_W("Should init config before {}!", __FUNCTION__);
        return empty;
    }
    if (const auto it = _configMap.find(key); it != _configMap.end()) {
        return it->second;
    }
    LOG_W("Config '{}' not found!", key);
    return empty;
}

const std::string& Config::GetConfigSafe(const std::string& key) const {
    static const std::string empty;
    if (!Initialized()) {
        LOG_W("Should init before {}.", __FUNCTION__);
        return empty;
    }
    // TODO 增加一个超时取消
    {
        std::lock_guard locker(_mtx);
        if (const auto it = _configMap.find(key); it != _configMap.end()) {
            return it->second;
        }
        LOG_W("Config key '{}' not found.", key);
    }
    return empty;
}

} // namespace zener
