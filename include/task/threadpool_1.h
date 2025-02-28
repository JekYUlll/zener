#ifndef ZENER_THREADPOOL1_H
#define ZENER_THREADPOOL1_H

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <utils/log/logger.h>

namespace zener::v0 {

class ThreadPool {
public:
    explicit ThreadPool(const size_t threadCount = std::thread::hardware_concurrency() - 2)
        : _pool(std::make_shared<Pool>()) {
        assert(threadCount > 0);
        _pool->threads.reserve(threadCount);
        for (size_t i = 0; i < threadCount; ++i) {
            _pool->threads.emplace_back([pool = _pool] {
                std::unique_lock<std::mutex> lock(pool->mtx);
                while (true) {
                    if (pool->isClosed) break;
                    if (!pool->tasks.empty()) {
                        auto task = std::move(pool->tasks.front());
                        pool->tasks.pop();
                        ++pool->activeThreads;
                        lock.unlock();
                        task();
                        lock.lock();
                        --pool->activeThreads;
                    } else {
                        pool->cond.wait(lock);
                    }
                }
            });
        }
    }

    ~ThreadPool() {
        Shutdown(0); // 强制关闭
    }

    template <typename F>
    void AddTask(F&& task) { {
            std::packaged_task<void()> packagedTask(std::forward<F>(task));
            std::lock_guard<std::mutex> lock(_pool->mtx);
            _pool->tasks.emplace(std::move(packagedTask));
        }
        _pool->cond.notify_one();
    }

    void Shutdown(int timeoutMS = 1000) const {
        if (!_pool) return;
        LOG_I("ThreadPool: Initiating shutdown (timeout={}ms)...", timeoutMS);

        // -------------------- Phase 1: 优雅关闭 --------------------
        {
            std::lock_guard<std::mutex> lock(_pool->mtx);
            _pool->isClosed = true;
            _pool->cond.notify_all();
        }
        // 等待任务完成或超时
        auto completionFuture = std::async(std::launch::async, [this, timeoutMS] {
            std::unique_lock<std::mutex> lock(_pool->mtx);
            return _pool->cond.wait_for(lock, std::chrono::milliseconds(timeoutMS), [this] {
                return _pool->tasks.empty() && _pool->activeThreads == 0;
            });
        });

        if (completionFuture.get()) {
            LOG_I("ThreadPool: All tasks completed gracefully");
        } else {
            LOG_W("ThreadPool: Graceful shutdown timed out");
        }

        // -------------------- Phase 2: 强制关闭 --------------------
        {
            std::lock_guard<std::mutex> lock(_pool->mtx);
            while (!_pool->tasks.empty()) {
                _pool->tasks.pop();
            }
            // 分离线程
            for (auto& thread : _pool->threads) {
                if (thread.joinable()) {
                    thread.detach();
                    LOG_D("ThreadPool: Detached thread {}", thread.native_handle());
                }
            }
            _pool->threads.clear();
        }
        LOG_I("ThreadPool: Shutdown complete.");
    }

private:
    struct Pool {
        std::vector<std::thread> threads;
        std::mutex mtx;
        std::condition_variable cond;
        bool isClosed = false;
        std::queue<std::packaged_task<void()>> tasks;
        std::atomic<int> activeThreads{0};
    };
    std::shared_ptr<Pool> _pool;
};

} // namespace zener::v0

#endif // !ZENER_THREADPOOL1_H