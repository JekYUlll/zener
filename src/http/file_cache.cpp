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
    for (auto&[fst, snd] : _fileCache) {
        UnloadFile(snd);
    }
    _fileCache.clear();
}

CachedFile* FileCache::GetFileMapping(const std::string& filePath,
                                      const struct stat& fileStat) {
    // 先尝试以共享锁方式查找缓存
    {
        std::shared_lock<std::shared_mutex> readLock(_cacheMutex);
        if (const auto it = _fileCache.find(filePath); it != _fileCache.end()) {
            // 检查文件是否被修改
            if (CachedFile* cache = it->second; cache->lastModTime == fileStat.st_mtime) {
                // 更新访问时间和引用计数
                cache->lastAccess = std::chrono::steady_clock::now();
                ++cache->refCount;

                LOG_D("File cache hit: {}, current reference count: {}",
                      filePath, cache->refCount.load());
                return cache;
            }
            LOG_D("File has been modified, reloading: {}", filePath);
            // 文件已被修改，需要重新加载，但需要升级锁
        }
    }

    // 文件不在缓存中或需要重新加载，获取排他锁
    std::unique_lock<std::shared_mutex> writeLock(_cacheMutex);

    // 再次检查，避免竞争条件
    if (const auto it = _fileCache.find(filePath); it != _fileCache.end()) {
        // 再次检查文件是否被修改
        if (CachedFile* cache = it->second; cache->lastModTime == fileStat.st_mtime) {
            // 更新访问时间和引用计数
            cache->lastAccess = std::chrono::steady_clock::now();
            ++cache->refCount;

            LOG_D("File cache hit (second check): {}, current reference count: "
                  "{}",
                  filePath, cache->refCount.load());
            return cache;
        } else {
            // 删除旧缓存
            LOG_D("Removing expired file cache: {}", filePath);
            UnloadFile(cache);
            _fileCache.erase(it);
        }
    }

    // 加载文件并创建新缓存
    CachedFile* newCache = LoadFile(filePath, fileStat);
    if (newCache) {
        _fileCache[filePath] = newCache;
        ++_totalMappedFiles;
        LOG_D("New file cache added: {}, current total mapped files: {}",
              filePath, _totalMappedFiles.load());
    }

    return newCache;
}

void FileCache::ReleaseFileMapping(const std::string& filePath) {
    // 使用排他锁以确保引用计数修改的安全性
    std::unique_lock writeLock(_cacheMutex);
    if (const auto it = _fileCache.find(filePath); it != _fileCache.end()) {
        CachedFile* cache = it->second;
        // 确保引用计数不会变成负数
        int currentCount = cache->refCount.load();
        while (currentCount > 0) {
            if (cache->refCount.compare_exchange_weak(
                    currentCount, currentCount - 1, std::memory_order_release,
                    std::memory_order_relaxed)) {
                LOG_D("Releasing file mapping reference: {}, current reference "
                      "count: {}",
                      filePath, currentCount - 1);
                break;
            }
        }
    }
}

void FileCache::CleanupCache(const int maxIdleTime) {
    std::unique_lock<std::shared_mutex> lock(_cacheMutex);
    const auto now = std::chrono::steady_clock::now();
    int removedCount = 0;

    LOG_D("Starting to clean file cache, current cache size: {}",
          _fileCache.size());

    for (auto it = _fileCache.begin(); it != _fileCache.end();) {
        // 检查引用计数是否为0，确保文件未被使用
        if (const CachedFile* cache = it->second; cache->refCount <= 0) {
            const auto idleTime = std::chrono::duration_cast<std::chrono::seconds>(
                                now - cache->lastAccess)
                                .count();

            if (idleTime > maxIdleTime) {
                LOG_D("Cleaning idle file cache: {}, idle time: {} seconds, "
                      "reference count: {}",
                      it->first, idleTime, cache->refCount.load());
                UnloadFile(cache);
                it = _fileCache.erase(it);
                --_totalMappedFiles;
                removedCount++;
                continue;
            }
        } else {
            LOG_D("Skipping file still in use: {}, reference count: {}",
                  it->first, cache->refCount.load());
        }
        ++it;
    }

    LOG_D("File cache cleaning completed, cleaned count: {}, current cache "
          "file count: {}",
          removedCount, _fileCache.size());
}

CachedFile* FileCache::LoadFile(const std::string& filePath,
                                const struct stat& fileStat) {
    const int fd = open(filePath.data(), O_RDONLY);
    if (fd < 0) {
        LOG_E("Failed to open file: {}, error: {}", filePath, strerror(errno));
        return nullptr;
    }
    const size_t fileSize = fileStat.st_size;
    /*
     *将文件映射到内存提高文件的访问速度
     *MAP_PRIVATE 建立一个写入时拷贝的私有映射
     */
    void* mmapPtr = mmap(nullptr, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mmapPtr == MAP_FAILED) {
        LOG_E("mmap file failed: {}, error: {}", filePath, strerror(errno));
        return nullptr;
    }
    close(fd); // 映射后可以关闭文件描述符 // 在判断前还是判断好=后？
    auto* cache = new CachedFile(); // TODO 是否内存泄漏？
    cache->data = static_cast<char*>(mmapPtr);
    cache->size = fileSize;
    cache->refCount.store(1); // 初始引用计数为1
    cache->lastModTime = fileStat.st_mtime;
    cache->lastAccess = std::chrono::steady_clock::now();

    LOG_D("File successfully mapped to cache: {}, size: {}, address: {:p}",
          filePath, fileSize, (void*)cache->data);

    return cache;
}

void FileCache::UnloadFile(const CachedFile* file) {
    if (file && file->data) {
        LOG_D("Unloading file mapping: address={:p}, size={}",
              (void*)file->data, file->size);
        munmap(file->data, file->size);
        delete file;
    }
}

} // namespace zener::http