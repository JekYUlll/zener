#ifndef ZENER_THREAD_POOL_H
#define ZENER_THREAD_POOL_H

// TODO 源文件拆分
/*
1. 动态线程调整
    根据队列负载动态增减线程，避免资源浪费（如 C++17 的 std::jthread）。

2. ​无锁队列或任务窃取
    替换 std::queue 为无锁结构（如 moodycamel::ConcurrentQueue）或实现工作窃取。

3. ​支持优先级任务
    使用优先队列，按任务优先级调度。

4. ​超时机制
    允许任务设置超时时间，避免长时间阻塞。

5. ​异常安全
    捕获任务异常并传递到主线程，避免线程崩溃。
*/
#include "utils/safequeue.hpp"

#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace zener {

constexpr int THREAD_NUM = 6; // 线程池并行的线程数量

class ThreadPool {
  private:
    // 内置线程工作类
    class ThreadWorker {
      private:
        int _id;           // 工作id
        ThreadPool* _pool; // 所属线程池

      public:
        ThreadWorker(ThreadPool* pool, const int id) : _id(id), _pool(pool) {}

        void operator()() const {
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
    explicit ThreadPool(const int n_threads = THREAD_NUM)
        : _shutdown(false), _threads(std::vector<std::thread>(n_threads)) {}

    ThreadPool(ThreadPool const&) = delete;

    ThreadPool(ThreadPool&&) = delete;

    ThreadPool& operator=(ThreadPool const&) = delete;

    ThreadPool& operator=(ThreadPool&&) = delete;

    // Inits thread pool
    void init() {
        for (size_t i = 0; i < _threads.size(); ++i) {
            _threads.at(i) = std::thread(ThreadWorker(this, i)); // 分配工作线程
        }
    }

    // Waits until threads finish their current task and shutdowns the pool
    void shutdown() {
        _shutdown = true;
        _con.notify_all(); // 唤醒所有工作线程
        for (auto& t : _threads) {
            if (t.joinable()) { // 判断线程是否在等待
                t.join();       // 将线程加入到等待队列
            }
        }
    }

    // 把参数列表的函数 function 包装成 packaged_task, 变成智能指针，再塞进一个
    // function，然后塞进队列
    // Submit a function to be executed asynchronously by the pool
    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
        // Create a function with bounded parameter ready to execute
        std::function<decltype(f(args...))()> func = std::bind(
            std::forward<F>(f),
            std::forward<Args>(args)...); // 此处的万能引用似乎是无效的
        // Encapsulate it into a shared pointer in order to be able to copy
        // construct
        auto task_ptr =
            std::make_shared<std::packaged_task<decltype(f(args...))()>>(
                func); // std::packaged_task ​不可复制，只能移动

        // Warp packaged task into void function
        std::function<void()> warpper_func = [task_ptr]() { (*task_ptr)(); };
        // 队列通用安全封包函数，并压入安全队列
        _queue.enqueue(warpper_func);

        // _queue.enqueue([task_ptr]() { (*task_ptr)(); }); // 简化为一步

        _con.notify_one(); // 唤醒一个等待中的线程

        return task_ptr->get_future(); // 返回先前注册的任务指针
    }
};

} // namespace zener

#endif // !ZENER_THREAD_POOL_H
