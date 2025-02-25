#ifndef ZENER_BLOCKDEQUE_H
#define ZENER_BLOCKDEQUE_H

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <fcntl.h>
#include <mutex>

/// 阻塞双端队列
/// 2025/02/23
/// webserver 中用于实现日志

// 构造函数和析构函数直接在类内定义时不会导致重复定义
// 构造函数/析构函数的定义直接写在类声明内（未显式声明 inline 或
// non-inline），则它们是隐式内联函数

// 对于模板类：
// 类内定义的成员函数（包括构造函数/析构函数）
// 必须是隐式内联的，否则无法通过编译

namespace zener {

template <typename T>
class BlockDeque {
  public:
    explicit BlockDeque(size_t maxCapacity = 1000);

    ~BlockDeque();

    void clear();

    void Close();

    [[nodiscard]] bool empty() const;

    [[nodiscard]] bool full() const;

    [[nodiscard]] size_t size() const;

    [[nodiscard]] size_t capacity() const;

    T front();
    const T front() const;

    T back();
    const T back() const;

    void push_back(const T& item);

    void push_front(const T& item);

    bool pop(T& item);

    bool pop(T& item, int timeout);

    void flush();

  private:
    std::deque<T> _deq;

    size_t _capacity;

    bool _isClose;

    mutable std::mutex _mtx;

    std::condition_variable _condConsumer;

    std::condition_variable _condProducer;
};

template <typename T>
inline BlockDeque<T>::BlockDeque(size_t maxCapacity) : _capacity(maxCapacity) {
    assert(maxCapacity > 0);
    _isClose = false;
}

template <typename T>
inline BlockDeque<T>::~BlockDeque() {
    Close();
}

template <typename T>
inline void BlockDeque<T>::Close() {
    {
        std::lock_guard<std::mutex> locker(_mtx);
        _deq.clear();
        _isClose = true;
    }
    _condProducer.notify_all();
    _condConsumer.notify_all();
}

template <typename T>
inline void BlockDeque<T>::flush() {
    _condConsumer.notify_one();
}

template <typename T>
inline void BlockDeque<T>::clear() {
    std::lock_guard<std::mutex> locker(_mtx);
    _deq.clear();
}

template <typename T>
inline T BlockDeque<T>::front() {
    std::lock_guard<std::mutex> locker(_mtx);
    return _deq.front();
}

template <typename T>
inline const T BlockDeque<T>::front() const {
    std::lock_guard<std::mutex> locker(_mtx);
    return _deq.front();
}

template <typename T>
inline T BlockDeque<T>::back() {
    std::lock_guard<std::mutex> locker(_mtx);
    return _deq.back();
}

template <typename T>
inline const T BlockDeque<T>::back() const {
    std::lock_guard<std::mutex> locker(_mtx);
    return _deq.back();
}

template <typename T>
inline size_t BlockDeque<T>::size() const {
    std::lock_guard<std::mutex> locker(_mtx);
    return _deq.size();
}

template <typename T>
inline size_t BlockDeque<T>::capacity() const {
    std::lock_guard<std::mutex> locker(_mtx);
    return _capacity;
}

template <typename T>
inline void BlockDeque<T>::push_back(const T& item) {
    std::unique_lock<std::mutex> locker(_mtx);
    while (_deq.size() >= _capacity) {
        _condProducer.wait(locker);
    }
    _deq.push_back(item);
    _condConsumer.notify_one();
}

template <typename T>
inline void BlockDeque<T>::push_front(const T& item) {
    std::unique_lock<std::mutex> locker(_mtx);
    while (_deq.size() >= _capacity) {
        _condProducer.wait(locker);
    }
    _deq.push_front(item);
    _condConsumer.notify_one();
}

template <typename T>
inline bool BlockDeque<T>::empty() const {
    std::lock_guard<std::mutex> locker(_mtx);
    return _deq.empty();
}

template <typename T>
inline bool BlockDeque<T>::full() const {
    std::lock_guard<std::mutex> locker(_mtx);
    return _deq.size() >= capacity();
}

// 通过参数传递弹出的元素（而非直接返回元素）
// 如果 pop
// 直接返回元素，当队列为空或关闭时，无法通过返回值区分“失败”和“合法元素”
// 替代方案：
// 1. std::optional<T>（C++17 起）
// 2. 抛出异常
template <typename T>
inline bool BlockDeque<T>::pop(T& item) {
    std::unique_lock<std::mutex> locker(_mtx);
    while (_deq.empty()) {
        // 释放锁并阻塞当前线程，等待生产者通过push操作唤醒
        _condConsumer.wait(locker);
        if (_isClose) {
            return false;
        }
    }
    item = _deq.front();
    _deq.pop_front();
    _condProducer.notify_one();
    return true;
}

template <typename T>
inline bool BlockDeque<T>::pop(T& item, int timeout) {
    std::unique_lock<std::mutex> locker(_mtx);
    while (_deq.empty()) {
        if (_condConsumer.wait_for(locker, std::chrono::seconds(timeout)) ==
            std::cv_status::timeout) {
            return false;
        }
        if (_isClose) {
            return false;
        }
    }
    item = _deq.front();
    _deq.pop_front();
    _condProducer.notify_one();
    return true;
}

} // namespace zener

#endif // !ZENER_BLOCKDEQUE_H