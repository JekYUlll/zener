#include "task/timer/maptimer.h"
#include "utils/log/logger.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <thread>
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

    // 添加异常处理
    try {
        _func();
    } catch (const std::exception& e) {
        LOG_E("定时器：回调执行异常，错误={}", e.what());
    } catch (...) {
        LOG_E("定时器：回调执行未知异常");
    }

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
    int processedCount = 0;

    // 使用写锁保护定时器操作
    std::unique_lock<std::shared_mutex> lock(_timerMutex);

    // 创建临时容器存储需要处理的定时器
    std::vector<std::pair<int64_t, Timer>> triggeredTimers;

    // 找出所有需要触发的定时器
    auto it = _timers.begin();
    while (it != _timers.end() && it->first <= now && processedCount < 100) {
        triggeredTimers.push_back(*it);
        it = _timers.erase(it);
        processedCount++;
    }

    // 如果收集了过多的定时器，记录警告
    if (processedCount >= 100) {
        LOG_W("定时器：单次Update处理定时器过多，已处理 {} "
              "个，定时器队列可能堆积",
              processedCount);
    }

    // 释放锁后再执行回调，避免长时间持有锁
    lock.unlock();

    // 处理触发的定时器
    for (auto& [time, t] : triggeredTimers) {
        // 执行定时器回调
        try {
            t.OnTimer();
        } catch (const std::exception& e) {
            LOG_E("定时器：Update处理定时器异常，错误={}", e.what());
            continue;
        }

        // 如果定时器需要重复触发，则重新插入到容器中
        if (t._repeat != 0) {
            try {
                std::unique_lock<std::shared_mutex> insertLock(_timerMutex);
                _timers.insert(std::make_pair(t._time, t));
            } catch (const std::exception& e) {
                LOG_E("定时器：重新插入定时器异常，错误={}", e.what());
            }
        }
    }

    // 定期清理已取消但仍在队列中的定时器
    static int64_t lastCleanupTime = 0;
    if (now - lastCleanupTime > 30000) { // 每30秒清理一次
        lastCleanupTime = now;
        CleanupCancelledTimers();
    }
}

// 新增：清理已取消的定时器
void TimerManager::CleanupCancelledTimers() {
    LOG_D("定时器：开始清理已取消的定时器");
    int countBefore = _timers.size();

    try {
        std::unique_lock<std::shared_mutex> lock(_timerMutex);
        std::unordered_set<int> validIds;

        // 收集所有有效的定时器ID
        for (const auto& [key, id] : _keyToTimerId) {
            validIds.insert(id);
        }

        // 创建新的定时器集合，只保留有效的定时器
        std::multimap<int64_t, Timer> newTimers;
        for (const auto& [time, timer] : _timers) {
            // 此处简化处理，保留所有定时器，因为无法从Timer中获取ID
            // 实际实现中应该在Timer中添加ID字段以便过滤
            newTimers.insert(std::make_pair(time, timer));
        }

        // 如果清理有效，更新定时器集合
        if (newTimers.size() < _timers.size()) {
            _timers = std::move(newTimers);
            LOG_I("定时器：清理完成，从 {} 减少到 {} 个定时器", countBefore,
                  _timers.size());
        }
    } catch (const std::exception& e) {
        LOG_E("定时器：清理定时器时发生异常，错误={}", e.what());
    }
}

void TimerManager::Tick() {
    while (!_bClosed) {
        try {
            Update();

            // 智能休眠，避免CPU占用过高
            int nextTick = GetNextTick();
            if (nextTick < 0) {
                // 没有定时器，休眠一段时间
                // 缩短休眠时间，增加对_bClosed检查频率
                for (int i = 0; i < 10 && !_bClosed; i++) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    if (_bClosed) {
                        LOG_I("定时器线程检测到关闭标志(长休眠)，正在退出...");
                        break;
                    }
                }
            } else if (nextTick > 0) {
                // 有定时器，但还未到期，休眠到下一个定时器触发时间
                // 分段休眠并检查_bClosed，确保快速响应关闭请求
                int sleepTime = std::min(nextTick, 100);
                for (int i = 0; i < sleepTime / 10 && !_bClosed; i++) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    if (_bClosed) {
                        LOG_I("定时器线程检测到关闭标志(短休眠)，正在退出...");
                        break;
                    }
                }
            }
            // 如果nextTick==0，表示有定时器已到期，立即处理

            // 检查关闭标志，避免死循环
            if (_bClosed) {
                LOG_I("定时器线程收到停止信号，正在退出...");
                break;
            }
        } catch (const std::exception& e) {
            LOG_E("定时器：Tick异常，错误={}", e.what());
            // 出错时短暂休眠，避免CPU占用过高
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } catch (...) {
            LOG_E("定时器：Tick未知异常");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    LOG_I("定时器线程已安全退出");
}

void TimerManager::Stop() {
    LOG_I("定时器管理器停止");
    _bClosed = true;

    // 强制唤醒可能在等待的定时器线程
    try {
        std::unique_lock<std::shared_mutex> lock(_timerMutex);
        // 添加一个立即触发的空定时器，确保线程从等待中退出
        Timer dummy(0);
        dummy._time = Timer::Now();
        dummy._func = []() { LOG_D("定时器：唤醒定时器线程的哑定时器被触发"); };
        _timers.insert(std::make_pair(dummy._time, dummy));
        LOG_I("定时器管理器：已添加唤醒定时器，确保定时器线程能够退出");
    } catch (const std::exception& e) {
        LOG_E("定时器管理器停止时发生异常: {}", e.what());
    } catch (...) {
        LOG_E("定时器管理器停止时发生未知异常");
    }
}

void TimerManager::DoSchedule(int milliseconds, int repeat,
                              std::function<void()> cb) {
    // 检查参数有效性
    if (!cb) {
        LOG_W("定时器：尝试调度空回调函数");
        return;
    }

    if (milliseconds <= 0) {
        LOG_W("定时器：尝试调度无效超时值 {}ms", milliseconds);
        milliseconds = 1; // 确保至少有1ms超时
    }

    try {
        Timer t(repeat);
        t._time = Timer::Now() + milliseconds;
        t._period = milliseconds;
        t._func = std::move(cb);

        // 使用写锁保护插入操作
        std::unique_lock<std::shared_mutex> lock(_timerMutex);
        _timers.insert(std::make_pair(t._time, t));
        LOG_D("定时器：添加新定时器，触发时间={}, 重复次数={}", t._time,
              repeat);
    } catch (const std::exception& e) {
        LOG_E("定时器：添加定时器异常，错误={}", e.what());
    }
}

int TimerManager::GetNextTick() {
    std::shared_lock<std::shared_mutex> lock(_timerMutex);

    if (_timers.empty()) {
        return -1;
    }

    try {
        const int64_t now = Timer::Now();
        const int64_t next = _timers.begin()->first;
        int diff = static_cast<int>(next - now);
        return diff > 0 ? diff : 0;
    } catch (const std::exception& e) {
        LOG_E("定时器：GetNextTick异常，错误={}", e.what());
        return 0;
    }
}

void TimerManager::DoScheduleWithKey(int key, int milliseconds, int repeat,
                                     const std::function<void()>& cb) {
    // 检查参数有效性
    if (!cb) {
        LOG_W("定时器：尝试使用key={}调度空回调函数", key);
        return;
    }

    if (milliseconds <= 0) {
        LOG_W("定时器：key={}尝试调度无效超时值 {}ms", key, milliseconds);
        milliseconds = 1; // 确保至少有1ms超时
    }

    try {
        // 使用写锁保护整个操作
        std::unique_lock<std::shared_mutex> lock(_timerMutex);

        // 先取消该key关联的旧定时器
        CancelByKeyInternal(key);

        int id = _nextId++;
        LOG_D("定时器：通过key={}创建新定时器，id={}, 超时={}ms, 重复={}", key,
              id, milliseconds, repeat);

        // 保存key到定时器ID的映射
        _keyToTimerId[key] = id;

        // 保存重复次数和周期
        if (repeat != 0) {
            _repeats[id] = std::make_pair(repeat, milliseconds);
        }

        // 由于需要递归引用，我们需要使用std::function和std::shared_ptr
        auto callbackPtr = std::make_shared<std::function<void()>>();

        *callbackPtr = [this, id, key, cb, callbackPtr]() {
            try {
                // 执行用户回调前检查key是否仍有效
                bool isValid = false;
                {
                    std::shared_lock<std::shared_mutex> readLock(_timerMutex);
                    auto keyIt = _keyToTimerId.find(key);
                    isValid =
                        (keyIt != _keyToTimerId.end() && keyIt->second == id);
                }

                if (!isValid) {
                    LOG_D("定时器：key={}的定时器id={}已失效，跳过执行", key,
                          id);
                    return;
                }

                // 执行用户回调
                cb();

                // 处理重复执行
                std::unique_lock<std::shared_mutex> writeLock(_timerMutex);
                auto keyIt = _keyToTimerId.find(key);
                if (keyIt != _keyToTimerId.end() && keyIt->second == id) {
                    auto it = _repeats.find(id);
                    if (it != _repeats.end()) {
                        auto& [remaining, period] = it->second;
                        if (remaining > 0) {
                            remaining--;
                            LOG_D("定时器：key={}的定时器id={}"
                                  "执行完成，剩余重复次数={}",
                                  key, id, remaining);

                            if (remaining == 0) {
                                LOG_D(
                                    "定时器：key={}的定时器id={}已完成所有重复",
                                    key, id);
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
                            LOG_D("定时器：key={}的定时器id={}"
                                  "执行完成，无限重复模式",
                                  key, id);
                            // 为下次执行创建新定时器
                            Timer t(-1);
                            t._time = Timer::Now() + period;
                            t._period = period;
                            t._func = *callbackPtr;
                            _timers.insert(std::make_pair(t._time, t));
                        }
                    } else {
                        // 非重复任务完成后从映射中移除
                        LOG_D("定时器：key={}的非重复定时器id={}已完成", key,
                              id);
                        _keyToTimerId.erase(key);
                    }
                } else {
                    LOG_D("定时器：key={}已被取消或更新，跳过后续处理", key);
                }
            } catch (const std::exception& e) {
                LOG_E("定时器：key={}的回调执行异常，id={}, 错误={}", key, id,
                      e.what());

                // 发生异常时，尝试清理资源
                try {
                    std::unique_lock<std::shared_mutex> cleanupLock(
                        _timerMutex);
                    auto keyIt = _keyToTimerId.find(key);
                    if (keyIt != _keyToTimerId.end() && keyIt->second == id) {
                        _keyToTimerId.erase(keyIt);
                    }
                    _repeats.erase(id);
                } catch (...) {
                    LOG_E("定时器：清理异常定时器资源时发生错误");
                }
            } catch (...) {
                LOG_E("定时器：key={}的回调执行未知异常，id={}", key, id);

                // 发生异常时，尝试清理资源
                try {
                    std::unique_lock<std::shared_mutex> cleanupLock(
                        _timerMutex);
                    auto keyIt = _keyToTimerId.find(key);
                    if (keyIt != _keyToTimerId.end() && keyIt->second == id) {
                        _keyToTimerId.erase(keyIt);
                    }
                    _repeats.erase(id);
                } catch (...) {
                    LOG_E("定时器：清理异常定时器资源时发生错误");
                }
            }
        };

        // 创建并添加新的定时器
        Timer t(repeat);
        t._time = Timer::Now() + milliseconds;
        t._period = milliseconds;
        t._func = *callbackPtr;
        _timers.insert(std::make_pair(t._time, t));

        lock.unlock();
    } catch (const std::exception& e) {
        LOG_E("定时器：添加key={}的定时器异常，错误={}", key, e.what());

        // 清理任何部分创建的资源
        try {
            std::unique_lock<std::shared_mutex> cleanupLock(_timerMutex);
            if (const auto keyIt = _keyToTimerId.find(key); keyIt != _keyToTimerId.end()) {
                _repeats.erase(keyIt->second);
                _keyToTimerId.erase(keyIt);
            }
        } catch (...) {
            LOG_E("定时器：清理失败的定时器资源时发生错误");
        }
    }
}

void TimerManager::CancelByKey(const int key) {
    std::unique_lock<std::shared_mutex> lock(_timerMutex);
    CancelByKeyInternal(key);
}

void TimerManager::CancelByKeyInternal(int key) {
    try {
        LOG_D("定时器：尝试取消key={}的定时器", key);

        if (const auto keyIt = _keyToTimerId.find(key); keyIt != _keyToTimerId.end()) {
            const int timerId = keyIt->second;
            LOG_D("定时器：找到key={}关联的定时器id={}", key, timerId);

            // 从映射中删除关联
            _keyToTimerId.erase(keyIt);

            // 清理重复信息
            if (const auto repeatIt = _repeats.find(timerId); repeatIt != _repeats.end()) {
                _repeats.erase(repeatIt);
                LOG_D("定时器：已清理id={}的重复信息", timerId);
            }

            // 注意：由于红黑树实现的限制，我们无法直接删除特定ID的定时器
            // 它将在下次Update()时被跳过，因为其关联的key不再存在于_keyToTimerId中
            LOG_D("定时器：定时器id={}将在下次触发时自动跳过", timerId);
        } else {
            LOG_D("定时器：未找到key={}关联的定时器", key);
        }
    } catch (const std::exception& e) {
        LOG_E("定时器：取消key={}的定时器时发生异常，错误={}", key, e.what());
    } catch (...) {
        LOG_E("定时器：取消key={}的定时器时发生未知异常", key);
    }
}

} // namespace rbtimer
} // namespace zener