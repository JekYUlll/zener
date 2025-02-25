#ifndef ZENER_TIMER_H
#define ZENER_TIMER_H

#include "task/timer/Itimer.h"
#include "task/timer/heaptimer.h"
#include "task/timer/maptimer.h"

namespace zener {

#ifdef __USE_MAPTIMER
    using TimerManagerImpl = rbtimer::TimerManager;
#else // __USE_MAPTIMER
    using TimerManagerImpl = v0::HeapTimerManager;
#endif // !__USE_MAPTIMER

} // !zener

#endif // !ZENER_TIMER_H
