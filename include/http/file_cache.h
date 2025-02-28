/**
 * @file file_cache.h
 * @brief 文件缓存系统，避免重复映射相同文件
 *
 * 在高并发场景下，大量连接可能会请求相同的静态文件(如index.html)，
 * 这会导致重复的mmap系统调用和内存资源消耗。
 * 此组件实现一个简单的文件映射缓存，使相同路径的文件只映射一次。
 */

#ifndef ZENER_HTTP_FILE_CACHE_H
#define ZENER_HTTP_FILE_CACHE_H

#include <atomic>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <sys/stat.h>
#include <unordered_map>

namespace zener::http {

struct CachedFile {
    char* data;                                       // 文件映射指针
    size_t size;                                      // 文件大小
    std::atomic<int> refCount;                        // 引用计数
    time_t lastModTime;                               // 文件最后修改时间
    std::chrono::steady_clock::time_point lastAccess; // 最后访问时间
};

class FileCache {
  public:
    FileCache(const FileCache&) = delete;
    FileCache& operator=(const FileCache&) = delete;

    static FileCache& GetInstance() {
        static FileCache instance;
        return instance;
    }

    /**
     * @brief 获取文件映射
     * @param filePath 文件完整路径
     * @param fileStat 文件状态结构
     * @return 文件映射信息，若失败返回nullptr
     */
    CachedFile* GetFileMapping(const std::string& filePath,
                               const struct stat& fileStat);

    /**
     * @brief 释放文件映射引用
     * @param filePath 文件路径
     */
    void ReleaseFileMapping(const std::string& filePath);

    /**
     * @brief 清理过期缓存
     * @param maxIdleTime 最大空闲时间(秒)
     */
    void CleanupCache(int maxIdleTime = 60);

  private:
    FileCache() = default;
    ~FileCache();

    // 加载文件并创建映射
    static CachedFile* LoadFile(const std::string& filePath,
                                const struct stat& fileStat);

    // 卸载文件映射
    static void UnloadFile(const CachedFile* file);

  private:
    std::unordered_map<std::string, CachedFile*> _fileCache{};
    std::shared_mutex _cacheMutex{}; // 读写锁，允许并发读取
    std::atomic<int> _totalMappedFiles{0};
};

} // namespace zener::http

#endif // ZENER_HTTP_FILE_CACHE_H