#ifndef ZENER_MULTIMAP_TIMER_H
#define ZENER_MULTIMAP_TIMER_H

// https://www.bilibili.com/video/BV1dP411r7Lf?spm_id_from=333.788.videopod.episodes&vd_source=9b0b9cbfd8c349b95b4776bd10953f3a&p=3
// 基于 std::multimap 红黑树的定时器
// 并且为单例模式

#include <cstdint>
#include <functional>
#include <map>
#include <utility>

namespace zws {
namespace maptimer {

class Timer {
    friend class TimerManager;

  public:
    Timer();
    Timer(int repeat);
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
void Timer::Callback(int milliseconds, F&& f, Args&&... args) {
    _period = milliseconds;
    _func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
}

class TimerManager {
  public:
    TimerManager& GetInstance() {
        static TimerManager instance;
        return instance;
    }

    ~TimerManager() = default;

    // 注册 无限重复版本
    template <typename F, typename... Args>
    void Schedule(int milliseconds, F&& f, Args&&... args);

    // 注册
    template <typename F, typename... Args>
    void Schedule(int milliseconds, int repeat, F&& f, Args&&... args);

    void Update();

    void Tick(); //*

    void Stop(); //*

  private:
    TimerManager() = default;
    TimerManager(const TimerManager&) = delete;
    TimerManager& operator=(TimerManager&) = delete;

    std::multimap<int64_t, Timer> _timers;

    bool _bClosed; //*
};

template <typename F, typename... Args>
void TimerManager::Schedule(int milliseconds, F&& f, Args&&... args) {
    Schedule(milliseconds, -1, std::forward<F>(f), std::forward<Args>(args)...);
}

template <typename F, typename... Args>
void TimerManager::Schedule(int milliseconds, int repeat, F&& f,
                            Args&&... args) {
    Timer t(repeat);
    t.Callback(milliseconds, std::forward<F>(f), std::forward<Args>(args)...);
    _timers.insert(std::make_pair(t._time, t));
}

} // namespace maptimer
} // namespace zws

#endif // !ZENER_MULTIMAP_TIMER_H