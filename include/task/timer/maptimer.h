#ifndef ZENER_MULTIMAP_TIMER_H
#define ZENER_MULTIMAP_TIMER_H
// https://www.bilibili.com/video/BV1dP411r7Lf?spm_id_from=333.788.videopod.episodes&vd_source=9b0b9cbfd8c349b95b4776bd10953f3a&p=3
// 基于 std::multimap 红黑树的定时器
#include "common.h"
#include "task/timer/Itimer.h"

#include <cstdint>
#include <functional>
#include <map>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace zener::rbtimer {

    /// 即 TimerNode 节点
class Timer {
    friend class TimerManager;

  public:
    Timer();
    explicit Timer(int repeat);
    ~Timer();

    template <typename F, typename... Args>
    void Callback(int milliseconds, F&& f, Args&&... args);

    void OnTimer();

    static int64_t Now();

  private:
    int64_t _time; // 定时器触发的时间点 毫秒为单位
    std::function<void()> _func;
    int _period; // 触发的周期 毫秒为单位
    int _repeat; // 触发的次数，-1表示无限触发
};

template <typename F, typename... Args>
void Timer::Callback(const int milliseconds, F&& f, Args&&... args) {
    _period = milliseconds;
    _func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
}

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
    void Update() override;

    // 在单独线程中循环处理定时器
    void Tick() override;

    // 停止定时器
    void Stop() override;

    // 获取下一个定时事件的超时时间
    int GetNextTick() override;

    // 基于业务ID取消定时器
    void CancelByKey(uint64_t key);

    // 使用业务ID调度定时器 TODO key 改为 uint64_t
    template <typename F, typename... Args>
    void ScheduleWithKey(uint64_t key, int milliseconds, int repeat, F&& f,
                         Args&&... args) {
        CancelByKey(key); // 先取消该key关联的旧定时器 TODO 有必要吗？
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
                           const std::function<void()>& cb);

    // 内部方法：在持有锁的情况下取消定时器
    void CancelByKeyInternal(uint64_t key);

    // 清理已取消但仍在队列中的定时器
    void CleanupCancelledTimers();

  private:
    TimerManager() : _bClosed(false), _nextId(0) {}

    std::multimap<int64_t, Timer> _timers{};
    bool _bClosed{false};
    int _nextId{0}; // 为每个计时器生成唯一ID
    std::unordered_map<int, std::pair<int, int>>
        _repeats{}; // ID -> (repeat count, period)
    // TODO 有必要吗？ 我直接传 connId 当 key. 后面查看这个map作用
    std::unordered_map<uint64_t, int> _keyToTimerId{}; // 业务ID -> 定时器ID的映射

    // 线程安全相关
    mutable std::shared_mutex _timerMutex; // 读写锁保护定时器操作
};

} // namespace zener::rbtimer


#endif // !ZENER_MULTIMAP_TIMER_H