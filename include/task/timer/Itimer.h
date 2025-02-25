#ifndef ZENER_TIMER_INTERFACE_H
#define ZENER_TIMER_INTERFACE_H

#include <functional>
#include <tuple>

namespace zener {

// 通用计时器接口，基于MapTimer风格设计
class ITimerManager {
  public:
    virtual ~ITimerManager() = default;

    // 调度一个任务，无限重复执行
    template <typename F, typename... Args>
    void Schedule(int milliseconds, F&& f, Args&&... args) {
        DoScheduleImpl(milliseconds, -1, std::forward<F>(f),
                       std::forward<Args>(args)...);
    }

    // 调度一个任务，指定重复次数
    template <typename F, typename... Args>
    void Schedule(int milliseconds, int repeat, F&& f, Args&&... args) {
        DoScheduleImpl(milliseconds, repeat, std::forward<F>(f),
                       std::forward<Args>(args)...);
    }

    virtual void Update() = 0; // 更新定时器，处理到期任务

    virtual void Tick() = 0; // 在单独线程中循环处理定时器

    virtual void Stop() = 0;  // 停止定时器

    virtual int GetNextTick() = 0; // 获取下一个定时事件的超时时间（毫秒）

  protected:
    // 模板方法的实现，由子类提供
    template <typename F, typename... Args>
    void DoScheduleImpl(int milliseconds, int repeat, F&& f, Args&&... args) {
        // 使用auto直接推导lambda类型，避免std::function的开销
        auto callback = [func = std::forward<F>(f),
                         tup = std::make_tuple(std::forward<Args>(args)...)]() {
            std::apply(func, tup);
        };

        DoSchedule(milliseconds, repeat, callback);
    }

    // 实际调度实现，由子类提供
    virtual void DoSchedule(int milliseconds, int repeat,
                            std::function<void()> cb) = 0;
};

} // namespace zener

#endif // !ZENER_TIMER_INTERFACE_H