#include "utils/safequeue.h"

namespace zws {

template <typename T>
SafeQueue<T>::SafeQueue(SafeQueue&& other) noexcept {
    std::lock_guard<std::mutex> lock(other._mutex);
    _queue = std::move(other._queue);
    // std::mutex 不可移动，新对象的 _mutex 会默认初始化
}

template <typename T>
[[nodiscard]] bool SafeQueue<T>::empty() const {
    std::unique_lock<std::mutex> lock(_mutex);
    return _queue.empty();
}

template <typename T>
[[nodiscard]] int SafeQueue<T>::size() const {
    std::unique_lock<std::mutex> lock(_mutex);
    return _queue.size();
}

template <typename T>
void SafeQueue<T>::enqueue(T& t) {
    std::unique_lock<std::mutex> lock(_mutex);
    _queue.emplace(t);
}

template <typename T>
bool SafeQueue<T>::dequeue(T& t) {
    std::unique_lock<std::mutex> lock(_mutex); // 队列加锁
    if (_queue.empty()) {
        return false;
    }
    t = std::move(
        _queue.front()); // 取出队首元素，返回队首元素值，并进行右值引用
    _queue.pop(); // 弹出入队的第一个元素
    return true;
}

} // namespace zws