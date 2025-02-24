#include "core/epoller.h"

#include <cassert>
#include <sys/epoll.h>
#include <unistd.h>

namespace zws {

/*
epoll_create
epoll_ctl
epoll_wait
*/

Epoller::Epoller(const int maxEvent, const bool isET)
    : _isET(isET), _epollFd(epoll_create(512)), _events(maxEvent) {
    assert(_epollFd >= 0 && !_events.empty());
}

Epoller::~Epoller() { close(_epollFd); }

bool Epoller::AddFd(const int fd, const uint32_t events) const {
    if (fd < 0) {
        return false;
    }
    epoll_event ev = {0};
    ev.data.fd = fd;
    ev.events = events;
    ev.events = _isET ? EPOLLIN | EPOLLET
                      : EPOLLIN; // 不知是否需要，原代码中没有设置 _isET
    return 0 == epoll_ctl(_epollFd, EPOLL_CTL_ADD, fd, &ev);
}

bool Epoller::ModFd(const int fd, const uint32_t events) const {
    if (fd < 0) {
        return false;
    }
    epoll_event ev = {0};
    ev.data.fd = fd;
    ev.events = events;
    return 0 == epoll_ctl(_epollFd, EPOLL_CTL_MOD, fd, &ev);
}

bool Epoller::DelFd(const int fd) const {
    if (fd < 0) {
        return false;
    }
    epoll_event ev = {0};
    return 0 == epoll_ctl(_epollFd, EPOLL_CTL_DEL, fd, &ev);
}

int Epoller::Wait(const int timeoutMs) {
    return epoll_wait(_epollFd, &_events[0], static_cast<int>(_events.size()),
                      timeoutMs);
}

int Epoller::GetEventFd(size_t i) const {
    assert(i < _events.size() && i >= 0); // i >= 0 永远为 true ?
    return _events[i].events;
}

uint32_t Epoller::GetEvents(const size_t i) const {
    assert(i < _events.size() && i >= 0); // i >= 0 永远为 true ?
    return _events[i].events;
}

} // namespace zws