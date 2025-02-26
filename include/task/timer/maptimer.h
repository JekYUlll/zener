#ifndef ZENER_MULTIMAP_TIMER_H
#define ZENER_MULTIMAP_TIMER_H
// https://www.bilibili.com/video/BV1dP411r7Lf?spm_id_from=333.788.videopod.episodes&vd_source=9b0b9cbfd8c349b95b4776bd10953f3a&p=3
// 基于 std::multimap 红黑树的定时器
#include "common.h"
#include "task/timer/Itimer.h"

#include <cstdint>
#include <functional>
#include <map>
#include <utility>

namespace zener {
namespace rbtimer {

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

  protected:
    // 实际的调度实现
    void DoSchedule(int milliseconds, int repeat,
                    std::function<void()> cb) override;

  private:
    TimerManager() = default;

    std::multimap<int64_t, Timer> _timers{};

    bool _bClosed{false};
};

} // namespace rbtimer
} // namespace zener

#endif // !ZENER_MULTIMAP_TIMER_H