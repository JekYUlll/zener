#ifndef SAFEQUEUE_H
#define SAFEQUEUE_H

#include <queue>
#include <mutex>

namespace zws {

template <typename T>
class SafeQueue
{
private:
    std::queue<T> _queue;
    std::mutex _mutex;

public:
    SafeQueue();
    SafeQueue(SafeQueue &&other);
    ~SafeQueue();

    [[nodiscard]] bool empty()
    {
        std::unique_lock<std::mutex> lock(_mutex);
        return _queue.empty();
    }

    [[nodiscard]] int size()
    {
        std::unique_lock<std::mutex> lock(_mutex);

        return _queue.size();
    }

    void enqueue(T &t)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _queue.emplace(t);
    }

    bool dequeue(T &t)
    {
        std::unique_lock<std::mutex> lock(_mutex); // 队列加锁

        if (_queue.empty())
            return false;
        t = std::move(_queue.front()); // 取出队首元素，返回队首元素值，并进行右值引用

        _queue.pop(); // 弹出入队的第一个元素

        return true;
    }
};

}

#endif // !SAFEQUEUE_H