#ifndef ZENER_TIMER_H
#define ZENER_TIMER_H

namespace zener {

#ifdef __USE_MAPTIMER
namespace rbtimer {
class TimerManager; // 使用前向声明，隔离依赖（虽说这个案例里也没隔离啥）
}
using TimerManagerImpl = rbtimer::TimerManager;
#else  // __USE_MAPTIMER
namespace vo {
class TimerManager;
}
using TimerManagerImpl = v0::HeapTimerManager;
#endif // !__USE_MAPTIMER

} // namespace zener

#endif // !ZENER_TIMER_H
