#ifndef ZENER_THREAD_POOL_H
#define ZENER_THREAD_POOL_H

// 编译期无法直接获取 CPU 内核数，因为 CPU 内核数是与运行环境相关的信息

// TODO 源文件拆分

#include "utils/safequeue.hpp"

#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace zws {

constexpr int THREAD_NUM = 6;

class ThreadPool {
  private:
    // 内置线程工作类
    class ThreadWorker {
      private:
        int _id;           // 工作id
        ThreadPool* _pool; // 所属线程池

      public:
        ThreadWorker(ThreadPool* pool, const int id) : _pool(pool), _id(id) {}

        void operator()() {
            std::function<void()> func;
            bool dequeued; // 是否正在取出队列中元素
            while (!_pool->_shutdown) {
                {
                    // 为线程环境加锁，互访问工作线程的休眠和唤醒
                    std::unique_lock<std::mutex> lock(_pool->_mtx);
                    // 如果任务队列为空，阻塞当前线程
                    if (_pool->_queue.empty()) {
                        _pool->_con.wait(lock); // 等待条件变量通知，开启线程
                    }
                    // 取出任务队列中的元素
                    dequeued = _pool->_queue.dequeue(func);
                }
                // 如果成功取出，执行工作函数
                if (dequeued) {
                    func();
                }
            }
        }
    };

    bool _shutdown; // 线程池是否关闭

    SafeQueue<std::function<void()>> _queue; // 执行函数安全队列，即任务队列

    std::vector<std::thread> _threads; // 工作线程队列

    std::mutex _mtx; // 线程休眠锁互斥变量

    std::condition_variable _con; // 线程环境锁，可以让线程处于休眠或者唤醒状态

  public:
    ThreadPool(const int n_threads = THREAD_NUM)
        : _threads(std::vector<std::thread>(n_threads)), _shutdown(false) {}

    ThreadPool(ThreadPool const&) = delete;

    ThreadPool(ThreadPool&&) = delete;

    ThreadPool& operator=(ThreadPool const&) = delete;

    ThreadPool& operator=(ThreadPool&&) = delete;

    // Inits thread pool
    void init() {
        for (int i = 0; i < _threads.size(); ++i) {
            _threads.at(i) = std::thread(ThreadWorker(this, i)); // 分配工作线程
        }
    }

    // Waits until threads finish their current task and shutdowns the pool
    void shutdown() {
        _shutdown = true;
        _con.notify_all(); // 通知，唤醒所有工作线程
        for (int i = 0; i < _threads.size(); ++i) {
            if (_threads.at(i).joinable()) // 判断线程是否在等待
            {
                _threads.at(i).join(); // 将线程加入到等待队列
            }
        }
    }

    // Submit a function to be executed asynchronously by the pool
    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
        // Create a function with bounded parameter ready to execute
        std::function<decltype(f(args...))()> func =
            std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        // Encapsulate it into a shared pointer in order to be able to copy
        // construct
        auto task_ptr =
            std::make_shared<std::packaged_task<decltype(f(args...))()>>(func);

        // Warp packaged task into void function
        std::function<void()> warpper_func = [task_ptr]() { (*task_ptr)(); };

        // 队列通用安全封包函数，并压入安全队列
        _queue.enqueue(warpper_func);

        // 唤醒一个等待中的线程
        _con.notify_one();

        // 返回先前注册的任务指针
        return task_ptr->get_future();
    }
};

} // namespace zws

#endif // !ZENER_THREAD_POOL_H
