#ifndef ZENER_THREADPOOL1_H
#define ZENER_THREADPOOL1_H

/**
 * 半同步/半反应堆线程池:内包含一个工作队列，主线程（线程池的创建者）往工作队列中插入任务，工作线程通过竞争来取得任务并执行它。
 */

#include <cassert>
#include <condition_variable>
#include <functional>
// #include <chrono>
// #include <future>
#include <mutex>
#include <queue>
#include <thread>

namespace zener {
namespace v0 {

class ThreadPool {
  public:
    explicit ThreadPool(const size_t threadCount = 6)
        : _pool(std::make_shared<Pool>()) {
        assert(threadCount > 0);
        for (size_t i = 0; i < threadCount; i++) {
            std::thread([pool = _pool] {
                std::unique_lock<std::mutex> locker(pool->mtx);
                while (true) {
                    // 假如此时ThreadPool已经被销毁，pool_是shared_ptr所以暂未销毁，仍然可以执行剩下的任务
                    if (!pool->tasks.empty()) {
                        auto task = std::move(pool->tasks.front());
                        pool->tasks.pop();
                        locker.unlock();
                        task();
                        locker.lock();
                    } else if (pool->isClosed)
                        break;
                    else
                        pool->cond.wait(locker);
                }
            }).detach();
        }
    }

    ThreadPool() = default;

    ThreadPool(ThreadPool&&) = default;

    ~ThreadPool() {
        if (static_cast<bool>(_pool)) {
            {
                std::lock_guard<std::mutex> locker(_pool->mtx);
                _pool->isClosed = true;
            }
            _pool->cond.notify_all();
        }
    }

    template <class F>
    void AddTask(F&& task) {
        {
            std::lock_guard<std::mutex> locker(_pool->mtx);
            _pool->tasks.emplace(std::forward<F>(task));
        }
        _pool->cond.notify_one();
    }

    // 改进：支持有返回值的任务
    // template <class F>
    // auto
    // AddTask(F&& task) -> std::future<typename std::invoke_result<F>::type> {
    //     using ReturnType = typename std::invoke_result<F>::type;
    //     auto taskPtr = std::make_shared<std::packaged_task<ReturnType()>>(
    //         std::forward<F>(task));
    //     std::future<ReturnType> future = taskPtr->get_future();
    //     {
    //         std::lock_guard<std::mutex> locker(pool_->mtx);
    //         pool_->tasks.emplace([taskPtr]() { (*taskPtr)(); });
    //     }
    //     pool_->cond.notify_one();
    //     return future;
    // }

    // 改成链式调用
    // template <class F> ThreadPool& AddTask(F &&task) {
    //     {
    //         std::lock_guard<std::mutex> locker(pool_->mtx);
    //         pool_->tasks.emplace(std::forward<F>(task));
    //     }
    //     pool_->cond.notify_one();
    //     return *this;
    // }

  private:
    struct Pool {
        std::mutex mtx;
        std::condition_variable cond;
        bool isClosed{};
        std::queue<std::function<void()>> tasks;
    };
    std::shared_ptr<Pool> _pool;

    // 建议改进：使用优先队列或无锁队列
    // struct Task {
    //     int priority;
    //     std::function<void()> func;
    //     // 支持任务优先级
    // };
    // std::priority_queue<Task> tasks;

    // size_t min_threads;
    // size_t max_threads;
    // size_t idle_threads;

    // void adjustThreadCount() {
    //     // 动态调整线程数量
    // }

    // bool waitForTasks(std::chrono::milliseconds timeout) {
    //     // 支持超时等待
    //     return true;
    // }
};

} // namespace v0
} // namespace zener

#endif // !ZENER_THREADPOOL1_H