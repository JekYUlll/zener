#ifndef ZENER_TIMER_HEAPTIMER_H
#define ZENER_TIMER_HEAPTIMER_H

#include "task/timer/heaptimer.h"
#include "task/timer/maptimer.h"

namespace zws {

// TODO 实现时间轮算法
// 感觉时间轮也是一种变相的哈希

using heaptimer::HeapTimer;

using maptimer::TimerManager;

// #define Timer HeapTimer

// template <typename TimerImpl>
// class Timer {
//     TimerImpl timer;

//   public:
//     void schedule() { timer.schedule; }
// };

} // namespace zws

#endif // !ZENER_TIMER_HEAPTIMER_H