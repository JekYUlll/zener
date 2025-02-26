#ifndef ZENER_TIMER_H
#define ZENER_TIMER_H

#ifdef __USE_MAPTIMER
#define TIMER_MANAGER_TYPE "MAP"
#include "task/timer/maptimer.h"
#else
#define TIMER_MANAGER_TYPE "HEAP"
#include "task/timer/heaptimer.h"
#endif // !__USE_MAPTIMER

namespace zener {

#ifdef __USE_MAPTIMER
using TimerManagerImpl = rbtimer::TimerManager;
#else  // !__USE_MAPTIMER
using TimerManagerImpl = v0::HeapTimerManager;
#endif // !__USE_MAPTIMER

} // namespace zener

#endif // !ZENER_TIMER_H
