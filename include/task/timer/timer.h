#ifndef ZENER_TIMER_H
#define ZENER_TIMER_H

namespace zener {

#ifdef __USE_MAPTIMER
#define TIMER_MANAGER_TYPE "map"
#include "task/timer/maptimer.h"
using TimerManagerImpl = rbtimer::TimerManager;
#else  // __USE_MAPTIMER
#define TIMER_MANAGER_TYPE "heap"
#include "task/timer/heaptimer.h"
using TimerManagerImpl = v0::HeapTimerManager;
#endif // !__USE_MAPTIMER

} // namespace zener

#endif // !ZENER_TIMER_H
