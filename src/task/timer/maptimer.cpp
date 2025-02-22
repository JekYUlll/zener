#include "task/timer/maptimer.h"

#include <chrono>
#include <cstdint>
#include <utility>

namespace zws {
namespace maptimer {

Timer::Timer() : _period(0), _repeat(-1) { _time = Now(); }

Timer::Timer(int repeat) : _period(0), _repeat(repeat) {}

Timer::~Timer() {}

int64_t Timer::Now() {
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
    return now_ms.time_since_epoch().count();
}

void Timer::OnTimer() {
    if (!_func || _repeat == 0) {
        return;
    }
    _func();
    _time += _period;
    if (_repeat > 0) {
        _repeat--;
    }
}

void TimerManager::Update() {
    if (_timers.empty()) {
        return;
    }
    int64_t now = Timer::Now();
    for (auto it = _timers.begin(); it != _timers.end();) {
        if (it->first > now) {
            return;
        }
        it->second.OnTimer();
        Timer t = it->second;
        it = _timers.erase(it);
        if (t._repeat != 0) {
            auto new_it = _timers.insert(std::make_pair(t._time, t));
            if (it == _timers.end() || new_it->first < it->first) {
                it = new_it;
            }
        }
    }
}

void TimerManager::Tick() {
    while (!_bClosed) {
        Update();
    }
}

void TimerManager::Stop() { _bClosed = true; }

} // namespace maptimer
} // namespace zws