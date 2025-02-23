#ifndef SAFEQUEUE_H
#define SAFEQUEUE_H

#include <mutex>
#include <queue>

namespace zws {

template <typename T>
class SafeQueue {

  public:
    SafeQueue() = default;
    SafeQueue(SafeQueue&& other) noexcept;
    ~SafeQueue() = default;

    [[nodiscard]] bool empty() const;

    [[nodiscard]] int size() const;

    void enqueue(T& t);

    bool dequeue(T& t);

  private:
    std::queue<T> _que;
    mutable std::mutex _mtx;
};

template <typename T>
inline SafeQueue<T>::SafeQueue(SafeQueue&& other) noexcept {
    std::lock_guard<std::mutex> lock(other._mtx);
    _que = std::move(other._que);
    // std::mutex 不可移动，新对象的 _mutex 会默认初始化
}

template <typename T>
inline bool SafeQueue<T>::empty() const {
    std::unique_lock<std::mutex> lock(_mtx);
    return _que.empty();
}

template <typename T>
inline int SafeQueue<T>::size() const {
    std::unique_lock<std::mutex> lock(_mtx);
    return _que.size();
}

template <typename T>
inline void SafeQueue<T>::enqueue(T& t) {
    std::unique_lock<std::mutex> lock(_mtx);
    _que.emplace(t);
}

template <typename T>
inline bool SafeQueue<T>::dequeue(T& t) {
    std::unique_lock<std::mutex> lock(_mtx); // 队列加锁
    if (_que.empty()) {
        return false;
    }
    t = std::move(_que.front()); // 取出队首元素，返回队首元素值，并进行右值引用
    _que.pop(); // 弹出入队的第一个元素
    return true;
}

} // namespace zws

#endif // !SAFEQUEUE_H