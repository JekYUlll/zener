#ifndef ZENER_EPOLLER_H
#define ZENER_EPOLLER_H

#include <cstdint>
#include <sys/epoll.h>
#include <vector>

namespace zws {

constexpr int N_MAX_EVENT = 1024;

class Epoller {
  public:
    explicit Epoller(int maxEvent = N_MAX_EVENT);

    ~Epoller();

    bool AddFd(int fd, uint32_t events);

    bool ModFd(int fd, uint32_t events);

    bool DelFd(int fd);

    int Wait(int timeoutMs = -1);

    int GetEventFd(size_t i) const;

    uint32_t GetEvents(size_t i) const;

  private:
    int _epollFd;
    std::vector<struct epoll_event> _events;
};

} // namespace zws

#endif // !ZENER_EPOLLER_H