#ifndef ZENER_TIMER_H
#define ZENER_TIMER_H
/// webserver 默认实现
/// 小根堆定时器
#include "task/timer/timer.h"

#include <chrono>
#include <functional>
#include <unordered_map>
#include <vector>

namespace zws {
namespace v0 {

typedef std::function<void()> TimeoutCallBack;
typedef std::chrono::high_resolution_clock Clock;
typedef std::chrono::milliseconds MS;
typedef Clock::time_point TimeStamp;

struct TimerNode {
    int id;
    TimeStamp expires;
    TimeoutCallBack callback;
    bool operator<(const TimerNode& other) const { return expires < other.expires; }
};

class HeapTimerManager; // 前置声明

class HeapTimer {
  public:
    friend class HeapTimerManager; // 声明HeapTimerManager为友元类

    HeapTimer();

    ~HeapTimer() { Clear(); }

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

class HeapTimerManager final : public ITimerManager {
  public:
    static HeapTimerManager& GetInstance() {
        static HeapTimerManager instance;
        return instance;
    }

    HeapTimerManager(const HeapTimerManager&) = delete;
    HeapTimerManager& operator=(HeapTimerManager&) = delete;
    ~HeapTimerManager() override = default;

    // 更新定时器，处理到期任务
    void Update() override { _timer.Tick(); }

    // 在单独线程中循环处理定时器
    void Tick() override {
        while (!_bClosed) {
            Update();
        }
    }

    // 停止定时器
    void Stop() override { _bClosed = true; }

    // 获取下一个定时事件的超时时间
    int GetNextTick() override { return _timer.GetNextTick(); }

    // 基于业务ID取消定时器
    void CancelByKey(int key) {
        if (const auto it = _keyToTimerId.find(key); it != _keyToTimerId.end()) {
            const int timerId = it->second;
            // 直接访问HeapTimer的私有成员
            if (_timer._ref.count(timerId) > 0) {
                if (const size_t index = _timer._ref[timerId]; index < _timer._heap.size()) {
                    _timer.del(index);
                }
            }
            // 从重复记录中删除
            _repeats.erase(timerId);
            // 从映射中删除
            _keyToTimerId.erase(it);
        }
    }

    // 使用业务ID调度定时器，如客户端fd
    template <typename F, typename... Args>
    void ScheduleWithKey(int key, int milliseconds, int repeat, F&& f,
                         Args&&... args) {
        // 先取消该key关联的旧定时器
        CancelByKey(key);

        auto callback = [this, key, func = std::forward<F>(f),
                         tup = std::make_tuple(std::forward<Args>(args)...)]() {
            // 执行回调前先检查key是否仍然有效
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
    HeapTimerManager() : _bClosed(false), _nextId(0) {}

    HeapTimer _timer;
    bool _bClosed;
    int _nextId; // 为每个计时器生成唯一ID
    std::unordered_map<int, std::pair<int, int>>
        _repeats;                               // ID -> (repeat count, period)
    std::unordered_map<int, int> _keyToTimerId; // 业务ID -> 定时器ID的映射
};

} // namespace v0

// 提供一个全局的TimerManager别名，方便在不同的实现间切换
#ifndef __USE_MAPTIMER
using TimerManagerImpl = v0::HeapTimerManager;
#endif

} // namespace zws

#endif // !ZENER_TIMER_H