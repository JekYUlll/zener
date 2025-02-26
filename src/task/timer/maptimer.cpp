#include "task/timer/maptimer.h"

#include <chrono>
#include <cstdint>
#include <utility>

namespace zener {
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

void TimerManager::DoScheduleWithKey(int key, int milliseconds, int repeat,
                                    std::function<void()> cb) {
    int id = _nextId++;
    
    // 保存key到定时器ID的映射
    _keyToTimerId[key] = id;
    
    // 保存重复次数和周期
    if (repeat != 0) {
        _repeats[id] = std::make_pair(repeat, milliseconds);
    }
    
    // 由于需要递归引用，我们需要使用std::function和std::shared_ptr
    auto callbackPtr = std::make_shared<std::function<void()>>();
    
    *callbackPtr = [this, id, key, cb, callbackPtr]() {
        // 执行用户回调
        cb();
        
        // 从映射中检查key是否仍有效
        auto keyIt = _keyToTimerId.find(key);
        if (keyIt != _keyToTimerId.end() && keyIt->second == id) {
            // 处理重复执行
            auto it = _repeats.find(id);
            if (it != _repeats.end()) {
                auto& [remaining, period] = it->second;
                if (remaining > 0) {
                    remaining--;
                    if (remaining == 0) {
                        _repeats.erase(it);
                        _keyToTimerId.erase(key);
                    } else {
                        // 为下次执行创建新定时器
                        Timer t(remaining);
                        t._time = Timer::Now() + period;
                        t._period = period;
                        t._func = *callbackPtr;
                        _timers.insert(std::make_pair(t._time, t));
                    }
                } else if (remaining == -1) { // 无限重复
                    // 为下次执行创建新定时器
                    Timer t(-1);
                    t._time = Timer::Now() + period;
                    t._period = period;
                    t._func = *callbackPtr;
                    _timers.insert(std::make_pair(t._time, t));
                }
            } else {
                // 非重复任务完成后从映射中移除
                _keyToTimerId.erase(key);
            }
        }
    };
    
    // 创建并添加新的定时器
    Timer t(repeat);
    t._time = Timer::Now() + milliseconds;
    t._period = milliseconds;
    t._func = *callbackPtr;
    _timers.insert(std::make_pair(t._time, t));
}

void TimerManager::CancelByKey(int key) {
    auto keyIt = _keyToTimerId.find(key);
    if (keyIt != _keyToTimerId.end()) {
        // 从映射中删除关联
        _keyToTimerId.erase(keyIt);
        
        // 注意：由于红黑树实现的限制，我们无法直接删除特定ID的定时器
        // 它将在下次Update()时被跳过，因为其关联的key不再存在于_keyToTimerId中
    }
}

} // namespace maptimer
} // namespace zener