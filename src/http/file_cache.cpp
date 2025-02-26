#include "http/file_cache.h"
#include "utils/log/logger.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace zener::http {

FileCache::~FileCache() {
    // 清理所有缓存的文件映射
    std::unique_lock<std::shared_mutex> lock(_cacheMutex);
    for (auto& pair : _fileCache) {
        UnloadFile(pair.second);
    }
    _fileCache.clear();
}

CachedFile* FileCache::GetFileMapping(const std::string& filePath,
                                      const struct stat& fileStat) {
    // 先尝试以共享锁方式查找缓存
    {
        std::shared_lock<std::shared_mutex> readLock(_cacheMutex);
        auto it = _fileCache.find(filePath);
        if (it != _fileCache.end()) {
            CachedFile* cache = it->second;

            // 检查文件是否被修改
            if (cache->lastModTime == fileStat.st_mtime) {
                // 更新访问时间和引用计数
                cache->lastAccess = std::chrono::steady_clock::now();
                cache->refCount++;

                LOG_D("文件缓存命中: {}, 当前引用计数: {}", filePath,
                      cache->refCount.load());
                return cache;
            } else {
                LOG_D("文件已被修改，重新加载: {}", filePath);
                // 文件已被修改，需要重新加载，但需要升级锁
            }
        }
    }

    // 文件不在缓存中或需要重新加载，获取排他锁
    std::unique_lock<std::shared_mutex> writeLock(_cacheMutex);

    // 再次检查，避免竞争条件
    auto it = _fileCache.find(filePath);
    if (it != _fileCache.end()) {
        CachedFile* cache = it->second;

        // 再次检查文件是否被修改
        if (cache->lastModTime == fileStat.st_mtime) {
            // 更新访问时间和引用计数
            cache->lastAccess = std::chrono::steady_clock::now();
            cache->refCount++;

            LOG_D("文件缓存命中(二次检查): {}, 当前引用计数: {}", filePath,
                  cache->refCount.load());
            return cache;
        } else {
            // 删除旧缓存
            LOG_D("移除过期文件缓存: {}", filePath);
            UnloadFile(cache);
            _fileCache.erase(it);
        }
    }

    // 加载文件并创建新缓存
    CachedFile* newCache = LoadFile(filePath, fileStat);
    if (newCache) {
        _fileCache[filePath] = newCache;
        _totalMappedFiles++;
        LOG_D("新增文件缓存: {}, 当前总映射文件数: {}", filePath,
              _totalMappedFiles.load());
    }

    return newCache;
}

void FileCache::ReleaseFileMapping(const std::string& filePath) {
    // 使用排他锁以确保引用计数修改的安全性
    std::unique_lock<std::shared_mutex> writeLock(_cacheMutex);
    auto it = _fileCache.find(filePath);
    if (it != _fileCache.end()) {
        CachedFile* cache = it->second;
        // 确保引用计数不会变成负数
        int currentCount = cache->refCount.load();
        while (currentCount > 0) {
            if (cache->refCount.compare_exchange_weak(
                    currentCount, currentCount - 1, std::memory_order_release,
                    std::memory_order_relaxed)) {
                LOG_D("释放文件映射引用: {}, 当前引用计数: {}", filePath,
                      currentCount - 1);
                break;
            }
        }
    }
}

void FileCache::CleanupCache(int maxIdleTime) {
    std::unique_lock<std::shared_mutex> lock(_cacheMutex);
    auto now = std::chrono::steady_clock::now();
    int removedCount = 0;

    LOG_D("开始清理文件缓存，当前缓存数量: {}", _fileCache.size());

    for (auto it = _fileCache.begin(); it != _fileCache.end();) {
        CachedFile* cache = it->second;

        // 检查引用计数是否为0，确保文件未被使用
        if (cache->refCount <= 0) {
            auto idleTime = std::chrono::duration_cast<std::chrono::seconds>(
                                now - cache->lastAccess)
                                .count();

            if (idleTime > maxIdleTime) {
                LOG_D("清理空闲文件缓存: {}, 空闲时间: {}秒, 引用计数: {}",
                      it->first, idleTime, cache->refCount.load());
                UnloadFile(cache);
                it = _fileCache.erase(it);
                _totalMappedFiles--;
                removedCount++;
                continue;
            }
        } else {
            LOG_D("跳过仍在使用的文件: {}, 引用计数: {}", it->first,
                  cache->refCount.load());
        }
        ++it;
    }

    LOG_D("文件缓存清理完成, 清理数量: {}, 当前缓存文件数: {}", removedCount,
          _fileCache.size());
}

CachedFile* FileCache::LoadFile(const std::string& filePath,
                                const struct stat& fileStat) {
    int fd = open(filePath.c_str(), O_RDONLY);
    if (fd < 0) {
        LOG_E("打开文件失败: {}, 错误: {}", filePath, strerror(errno));
        return nullptr;
    }

    size_t fileSize = fileStat.st_size;
    void* mmapPtr = mmap(nullptr, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd); // 映射后可以关闭文件描述符

    if (mmapPtr == MAP_FAILED) {
        LOG_E("mmap文件失败: {}, 错误: {}", filePath, strerror(errno));
        return nullptr;
    }

    CachedFile* cache = new CachedFile();
    cache->data = static_cast<char*>(mmapPtr);
    cache->size = fileSize;
    cache->refCount = 1; // 初始引用计数为1
    cache->lastModTime = fileStat.st_mtime;
    cache->lastAccess = std::chrono::steady_clock::now();

    LOG_D("文件成功映射到缓存: {}, 大小: {}, 地址: {:p}", filePath, fileSize,
          (void*)cache->data);

    return cache;
}

void FileCache::UnloadFile(CachedFile* file) {
    if (file && file->data) {
        LOG_D("卸载文件映射: 地址={:p}, 大小={}", (void*)file->data,
              file->size);
        munmap(file->data, file->size);
        delete file;
    }
}

} // namespace zener::http