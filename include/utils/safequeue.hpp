#ifndef SAFEQUEUE_H
#define SAFEQUEUE_H

/*
    通用线程安全队列 用于 ThreadPoll

    为什么 SafeQueue 选择普通加锁队列而非阻塞队列？

    **阻塞逻辑外置**：线程池通过条件变量​
   (std::condition_variable)
   实现阻塞等待，将队列的同步机制（是否阻塞）与数据存储解耦。

*/

#include <mutex>
#include <queue>

namespace zener {

template <typename T>
class SafeQueue {

  public:
    SafeQueue() = default;
    SafeQueue(SafeQueue&& other) noexcept;
    ~SafeQueue() = default;

    [[nodiscard]] bool empty() const;

    [[nodiscard]] int size() const;

    void enqueue(T& t);

    [[nodiscard]] bool dequeue(T& t);

  private:
    std::queue<T> _que;
    mutable std::mutex _mtx;
};

template <typename T>
inline SafeQueue<T>::SafeQueue(SafeQueue&& other) noexcept {
    std::lock_guard<std::mutex> locker(other._mtx);
    _que = std::move(other._que);
}

template <typename T>
inline bool SafeQueue<T>::empty() const {
    std::unique_lock<std::mutex> locker(_mtx);
    return _que.empty();
}

template <typename T>
inline int SafeQueue<T>::size() const {
    std::unique_lock<std::mutex> locker(_mtx);
    return _que.size();
}

template <typename T>
inline void SafeQueue<T>::enqueue(T& t) {
    std::unique_lock<std::mutex> locker(_mtx);
    _que.emplace(t);
}

template <typename T>
inline bool SafeQueue<T>::dequeue(T& t) {
    std::unique_lock<std::mutex> locker(_mtx); // 队列加锁
    if (_que.empty()) {
        return false;
    }
    t = std::move(_que.front()); // 取出队首元素，返回队首元素值，并进行右值引用
    _que.pop();                  // 弹出入队的第一个元素
    return true;
}

} // namespace zener

#endif // !SAFEQUEUE_H