#ifndef ZENER_TIMER_H
#define ZENER_TIMER_H

/* 分别为红黑树定时器与小顶堆计时器
 * WebServer 里 WebServer::ExtentTime_ 方法调用 timer_->adjust(fd, timeoutMs)
 * 是通过小顶堆方便地把节点 siftdown_ ，达到重新计时的能力
 *
 *
 * TODO 实现 Netty 的时间轮计时器
 */

#ifdef __USE_MAPTIMER
#define TIMER_MANAGER_TYPE "MAP"
#include "task/timer/maptimer.h"
#else // __USE_MAPTIMER
#define TIMER_MANAGER_TYPE "HEAP"
#include "task/timer/heaptimer.h"
#endif // !__USE_MAPTIMER

namespace zener {

#ifdef __USE_MAPTIMER
using TimerManagerImpl = rbtimer::TimerManager;
#else  // !__USE_MAPTIMER
using TimerManagerImpl = v0::TimerManager;
#endif // !__USE_MAPTIMER

} // namespace zener

#endif // !ZENER_TIMER_H
