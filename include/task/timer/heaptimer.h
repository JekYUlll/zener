#ifndef ZENER_HEAP_TIMER_H
#define ZENER_HEAP_TIMER_H
/// webserver 默认实现
/// 小根堆定时器 用于超时控制
#include "common.h"
#include "task/timer/Itimer.h"

#include <chrono>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace zener {
namespace v0 {

typedef std::function<void()> TimeoutCallBack;
typedef std::chrono::high_resolution_clock Clock;
typedef std::chrono::milliseconds MS;
typedef Clock::time_point TimeStamp;

struct TimerNode {
    int id;
    TimeStamp expires;        // 超时时间
    TimeoutCallBack callback; // 超时回调
    bool operator<(const TimerNode& other) const {
        return expires < other.expires;
    }
};

class TimerManager;

class Timer {
  public:
    friend class TimerManager;

    Timer();

    ~Timer() { Clear(); }

    void Adjust(int id, int newExpires);
    void Add(int id, int timeOut, const TimeoutCallBack& cb);
    void Clear();
    void Pop();

    void doWork(int id);

    void Tick();
    int GetNextTick();

  private:
    void del(size_t i);
    void siftUp(size_t i);
    bool siftDown(size_t index, size_t n);
    void swapNode(size_t i, size_t j);

    std::vector<TimerNode> _heap;
    std::unordered_map<int, size_t> _ref;
};

class TimerManager final : public ITimerManager {
  public:
    _ZENER_SHORT_FUNC static TimerManager& GetInstance() {
        static TimerManager instance;
        return instance;
    }

    TimerManager(const TimerManager&) = delete;
    TimerManager& operator=(TimerManager&) = delete;
    ~TimerManager() override = default;

    // 更新定时器，处理到期任务
    void Update() override {
        std::lock_guard<std::mutex> lk(_mtx);
        _timer.Tick();
    }

    // 在单独线程中循环处理定时器
    void Tick() override {
        while (!_bClosed) {
            Update();
        }
    }

    // 停止定时器
    void Stop() override { _bClosed = true; }

    // 获取下一个定时事件的超时时间
    int GetNextTick() override {
        std::lock_guard<std::mutex> lk(_mtx);
        return _timer.GetNextTick();
    }

    // 基于业务ID取消定时器
    void CancelByKey(const int key) {
        std::lock_guard<std::mutex> lk(_mtx);
        CancelByKeyLocked(key);
    }

    // 使用业务ID调度定时器，如客户端fd
    template <typename F, typename... Args>
    void ScheduleWithKey(int key, int milliseconds, int repeat, F&& f,
                         Args&&... args) {
        std::lock_guard<std::mutex> lk(_mtx);
        CancelByKeyLocked(key);
        auto callback = [this, key, func = std::forward<F>(f),
                         tup = std::make_tuple(std::forward<Args>(args)...)]() {
            // Called from within Update() which already holds _mtx
            if (_keyToTimerId.find(key) != _keyToTimerId.end()) {
                std::apply(func, tup);
            }
        };
        DoScheduleWithKey(key, milliseconds, repeat, callback);
    }

  protected:
    // 实际的调度实现
    void DoSchedule(int milliseconds, int repeat,
                    std::function<void()> cb) override;

    // 使用业务ID的调度实现
    void DoScheduleWithKey(int key, int milliseconds, int repeat,
                           std::function<void()> cb);

  private:
    TimerManager() : _bClosed(false), _nextId(0) {}

    // Must be called with _mtx held
    void CancelByKeyLocked(const int key) {
        const auto it = _keyToTimerId.find(key);
        if (it == _keyToTimerId.end()) return;
        const int timerId = it->second;
        try {
            if (_timer._ref.count(timerId) > 0) {
                const size_t index = _timer._ref[timerId];
                if (index < _timer._heap.size()) {
                    _timer.del(index);
                }
            }
            _repeats.erase(timerId);
            _keyToTimerId.erase(it);
        } catch (...) {
            _repeats.erase(timerId);
            _keyToTimerId.erase(key);
        }
    }

    mutable std::mutex _mtx;
    Timer _timer;
    bool _bClosed;
    int _nextId;
    std::unordered_map<int, std::pair<int, int>> _repeats;
    std::unordered_map<int, int> _keyToTimerId;
};

} // namespace v0

} // namespace zener

#endif // !ZENER_HEAP_TIMER_H