#ifndef ZENER_TIMER_H
#define ZENER_TIMER_H

/// webserver 默认实现
/// 小根堆定时器

#include <chrono>
#include <functional>
#include <unordered_map>
#include <vector>

namespace zws {
namespace heaptimer {

typedef std::function<void()> TimeoutCallBack;
typedef std::chrono::high_resolution_clock Clock;
typedef std::chrono::milliseconds MS;
typedef Clock::time_point TimeStamp;

struct TimerNode {
    int id;
    TimeStamp expires;
    TimeoutCallBack callback;
    bool operator<(const TimerNode& other) { return expires < other.expires; }
};

class HeapTimer {
  public:
    HeapTimer();

    ~HeapTimer() { Clear(); }

    void Adjust(int id, int newExpires);

    void Add(int id, int timeOut, const TimeoutCallBack& cb);

    void doWork(int id);

    void Clear();

    void Tick();

    void Pop();

    int GetNextTick();

  private:
    void del(size_t i);

    void siftUp(size_t i);

    bool siftDown(size_t index, size_t n);

    void swapNode(size_t i, size_t j);

    std::vector<TimerNode> _heap;

    std::unordered_map<int, size_t> _ref;
};

} // namespace heaptimer
} // namespace zws

#endif // !ZENER_TIMER_H