#ifndef SAFEQUEUE_H
#define SAFEQUEUE_H

#include <mutex>
#include <queue>

namespace zws {

template <typename T>
class SafeQueue {
  private:
    std::queue<T> _queue;
    mutable std::mutex _mutex;

  public:
    SafeQueue() = default;
    SafeQueue(SafeQueue&& other) noexcept;
    ~SafeQueue() = default;

    [[nodiscard]] bool empty() const;

    [[nodiscard]] int size() const;

    void enqueue(T& t);

    bool dequeue(T& t);
};

} // namespace zws

#endif // !SAFEQUEUE_H