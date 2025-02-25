#include "task/timer/maptimer.h"

#include <chrono>
#include <cstdint>
#include <utility>

namespace zws {
namespace rbtimer {

Timer::Timer() : _period(0), _repeat(-1) { _time = Now(); }

Timer::Timer(const int repeat) : _time(0), _period(0), _repeat(repeat) {}

Timer::~Timer() = default;

int64_t Timer::Now() {
    const auto now = std::chrono::system_clock::now();
    const auto now_ms =
        std::chrono::time_point_cast<std::chrono::milliseconds>(now);
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
    const int64_t now = Timer::Now();
    for (auto it = _timers.begin(); it != _timers.end();) {
        if (it->first > now) {
            return;
        }
        it->second.OnTimer();
        Timer t = it->second;
        it = _timers.erase(it);
        if (t._repeat != 0) {
            if (const auto new_it = _timers.insert(std::make_pair(t._time, t));
                it == _timers.end() || new_it->first < it->first) {
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

void TimerManager::DoSchedule(int milliseconds, int repeat,
                              std::function<void()> cb) {
    Timer t(repeat);
    t._time = Timer::Now() + milliseconds;
    t._period = milliseconds;
    t._func = std::move(cb);
    _timers.insert(std::make_pair(t._time, t));
}

int TimerManager::GetNextTick() {
    if (_timers.empty()) {
        return -1;
    }
    const int64_t now = Timer::Now();
    const int64_t next = _timers.begin()->first;
    int diff = static_cast<int>(next - now);
    return diff > 0 ? diff : 0;
}

} // namespace maptimer
} // namespace zws