#ifndef ZENER_TIMER_H
#define ZENER_TIMER_H

#include <chrono>
#include <functional>
#include <unordered_map>
#include <vector>

namespace zws {

typedef std::function<void()> TimeoutCallBack;
typedef std::chrono::high_resolution_clock Clock;
typedef std::chrono::milliseconds MS;
typedef Clock::time_point TimeStamp;

struct TimerNode {
    int id;
    TimeStamp expires;
    TimeoutCallBack cb;
    bool operator<(const TimerNode& other) { return expires < other.expires; }
};

class HeapTimer {
  public:
    HeapTimer();

  private:
    void del(size_t i);

    void siftUp(size_t i);

    void siftDown(size_t index, size_t n);

    void swapNode(size_t i, size_t j);

    std::vector<TimerNode> _heap;

    std::unordered_map<int, size_t> _ref;
};

} // namespace zws

#endif // !ZENER_TIMER_H