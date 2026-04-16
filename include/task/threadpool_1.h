#ifndef ZENER_THREADPOOL1_H
#define ZENER_THREADPOOL1_H
/*
    使用packaged_task，支持返回值任务
    支持工作窃取（work stealing）：每个线程有独立的本地双端队列，
    空闲时随机从其他线程的队列尾部窃取任务，减少全局锁竞争。
*/
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <queue>
#include <random>
#include <thread>
#include <utils/log/logger.h>

namespace zener::v0 {

class ThreadPool {
  public:
    explicit ThreadPool(
        const size_t threadCount = std::thread::hardware_concurrency() - 2)
        : _pool(std::make_shared<Pool>(threadCount)) {
        assert(threadCount > 0);
        _pool->threads.reserve(threadCount);
        for (size_t i = 0; i < threadCount; ++i) {
            _pool->threads.emplace_back([pool = _pool, i] {
                std::mt19937 rng(std::random_device{}());
                const size_t n = pool->localQueues.size();

                while (true) {
                    std::packaged_task<void()> task;

                    // 1. 先尝试从本地队列头部取任务
                    {
                        std::lock_guard<std::mutex> lk(
                            pool->localQueues[i].mtx);
                        if (!pool->localQueues[i].deque.empty()) {
                            task =
                                std::move(pool->localQueues[i].deque.front());
                            pool->localQueues[i].deque.pop_front();
                        }
                    }

                    // 2. 本地为空，尝试从全局队列取
                    if (!task.valid()) {
                        std::unique_lock<std::mutex> lock(pool->globalMtx);
                        if (!pool->globalTasks.empty()) {
                            task = std::move(pool->globalTasks.front());
                            pool->globalTasks.pop();
                        }
                    }

                    // 3. 全局也为空，随机窃取其他线程本地队列尾部
                    if (!task.valid()) {
                        std::uniform_int_distribution<size_t> dist(0, n - 1);
                        size_t victim = dist(rng);
                        if (victim != i) {
                            std::lock_guard<std::mutex> lk(
                                pool->localQueues[victim].mtx);
                            if (!pool->localQueues[victim].deque.empty()) {
                                task = std::move(
                                    pool->localQueues[victim].deque.back());
                                pool->localQueues[victim].deque.pop_back();
                            }
                        }
                    }

                    if (task.valid()) {
                        ++pool->activeThreads;
                        task();
                        --pool->activeThreads;
                    } else {
                        // 无任务可取，等待通知
                        std::unique_lock<std::mutex> lock(pool->globalMtx);
                        if (pool->isClosed)
                            break;
                        pool->cond.wait_for(lock,
                                            std::chrono::microseconds(200));
                        if (pool->isClosed && pool->globalTasks.empty())
                            break;
                    }
                }
            });
        }
    }

    ~ThreadPool() {
        Shutdown(0); // 强制关闭
    }

    // 提交任务：优先投入当前线程对应的本地队列，否则投入全局队列
    template <typename F>
    void AddTask(F &&task) {
        std::packaged_task<void()> packagedTask(std::forward<F>(task));

        // 尝试找到当前线程的本地队列
        const auto tid = std::this_thread::get_id();
        auto &queues = _pool->localQueues;
        for (size_t i = 0; i < _pool->threads.size(); ++i) {
            if (_pool->threads[i].get_id() == tid) {
                std::lock_guard<std::mutex> lk(queues[i].mtx);
                queues[i].deque.push_back(std::move(packagedTask));
                _pool->cond.notify_one();
                return;
            }
        }

        // 外部线程提交，走全局队列
        {
            std::lock_guard<std::mutex> lock(_pool->globalMtx);
            _pool->globalTasks.emplace(std::move(packagedTask));
        }
        _pool->cond.notify_one();
    }

    void Shutdown(int timeoutMS = 1000) const {
        if (!_pool)
            return;
        LOG_I("ThreadPool: Initiating shutdown (timeout={}ms)...", timeoutMS);

        // -------------------- Phase 1: 优雅关闭 --------------------
        {
            std::lock_guard<std::mutex> lock(_pool->globalMtx);
            _pool->isClosed = true;
            _pool->cond.notify_all();
        }
        // 等待任务完成或超时
        auto completionFuture =
            std::async(std::launch::async, [this, timeoutMS] {
                std::unique_lock<std::mutex> lock(_pool->globalMtx);
                return _pool->cond.wait_for(
                    lock, std::chrono::milliseconds(timeoutMS), [this] {
                        return _pool->globalTasks.empty() &&
                               _pool->activeThreads == 0;
                    });
            });

        if (completionFuture.get()) {
            LOG_I("ThreadPool: All tasks completed gracefully");
        } else {
            LOG_W("ThreadPool: Graceful shutdown timed out");
        }

        // -------------------- Phase 2: 强制关闭 --------------------
        {
            std::lock_guard<std::mutex> lock(_pool->globalMtx);
            while (!_pool->globalTasks.empty()) {
                _pool->globalTasks.pop();
            }
            for (auto &lq : _pool->localQueues) {
                std::lock_guard<std::mutex> lk(lq.mtx);
                lq.deque.clear();
            }
            for (auto &thread : _pool->threads) {
                if (thread.joinable()) {
                    thread.detach();
                    LOG_D("ThreadPool: Detached thread {}",
                          thread.native_handle());
                }
            }
            _pool->threads.clear();
        }
        LOG_I("ThreadPool: Shutdown complete.");
    }

  private:
    struct LocalQueue {
        std::mutex mtx;
        std::deque<std::packaged_task<void()>> deque;
    };

    struct Pool {
        explicit Pool(size_t n) : localQueues(n) {}
        std::vector<std::thread> threads;
        std::vector<LocalQueue> localQueues; // 每线程本地双端队列
        std::mutex globalMtx;
        std::condition_variable cond;
        bool isClosed = false;
        std::queue<std::packaged_task<void()>>
            globalTasks; // 全局队列（外部提交）
        std::atomic<int> activeThreads{0};
    };
    std::shared_ptr<Pool> _pool;
};

} // namespace zener::v0

#endif // !ZENER_THREADPOOL1_H
