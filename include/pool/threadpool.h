#ifndef THREADPOOL_H
#define THREADPOOL_H

// 在简单的实现中，std::mutex 锁住整个队列，导致每次访问队列时都需要锁定整个队列。
// 手写队列，可以选择更精细的锁粒度，例如 分段锁 或 读写锁，这样多个线程在读取队列时可以并发，减少不必要的锁竞争。

namespace zws {

    class ThreadPool
    {

    };

}

#endif // !THREADPOOL_H