#ifndef ZENER_EPOLLER_H
#define ZENER_EPOLLER_H

#include <cstdint>
#include <sys/epoll.h>
#include <vector>

namespace zws {

constexpr int N_MAX_EVENT = 1024;

class Epoller {
  public:
    explicit Epoller(int maxEvent = N_MAX_EVENT,
                     bool isET = true); // 比起原方案，添加一个 isET

    ~Epoller();

    // 向 epoll 事件表注册事件
    bool AddFd(int fd, uint32_t events);
    // 修改已经注册的fd的监听事件
    bool ModFd(int fd, uint32_t events);
    // 从epoll事件表中删除一个fd
    bool DelFd(int fd);
    // 等待epoll上监听的fd产生事件，超时时间timeout，产生的事件需要使用GetEvents获得
    int Wait(int timeoutMs = -1);
    // 获取产生的事件的来源fd（应在wait之后调用）
    int GetEventFd(size_t i) const;
    // 获取产生的事件（应在wait之后调用）
    uint32_t GetEvents(size_t i) const;

  private:
    bool _isET;                              // 是否开启ET模式
    int _epollFd;                            // epoll事件表
    std::vector<struct epoll_event> _events; // 存储epoll上监听的fd产生的事件
};

} // namespace zws

#endif // !ZENER_EPOLLER_H