#include "task/timer/heaptimer.h"
#include "utils/log/logger.h"
#include <cassert>
#include <cstddef>
#include <memory> // 为std::make_shared添加
#include <utility>

namespace zener::v0 {

Timer::Timer() {
    // std::vector::reserve 用于为 std::vector
    // 预先分配至少能容纳指定数量元素的内存空间
    _heap.reserve(64);
    LOG_D("堆定时器初始化，预分配容量：64");
}

void Timer::siftUp(size_t i) {
    // 防止越界访问
    if (i >= _heap.size()) {
        return;
    }

    size_t j = 0;
    while (i > 0) { // 修改为检查i而不是j，防止size_t下溢
        j = (i - 1) / 2;
        if (j >= _heap.size()) { // 额外检查父节点索引
            break;
        }
        if (_heap[j] < _heap[i]) {
            break;
        }
        swapNode(i, j);
        i = j;
    }
}

void Timer::swapNode(size_t i, size_t j) {
    // 防止越界访问
    if (i >= _heap.size() || j >= _heap.size()) {
        return;
    }

    std::swap(_heap[i], _heap[j]);
    _ref[_heap[i].id] = i;
    _ref[_heap[j].id] = j;
}

bool Timer::siftDown(size_t index, size_t n) {
    // 防止越界访问
    if (index >= _heap.size() || n > _heap.size()) {
        return false;
    }

    size_t i = index;
    size_t j = i * 2 + 1;
    while (j < n) {
        if (j + 1 < n && _heap[j + 1] < _heap[j]) {
            j++;
        }

        if (_heap[i] < _heap[j]) {
            break;
        }

        swapNode(i, j);
        i = j;
        j = i * 2 + 1;
    }
    return i > index;
}

void Timer::Add(int id, int timeout, const TimeoutCallBack& cb) {
    assert(id >= 0);
    size_t i;
    if (_ref.count(id) == 0) {
        /* 新节点：堆尾插入，调整堆 */
        i = _heap.size();
        _ref[id] = i;
        _heap.push_back({id, Clock::now() + MS(timeout), cb});
        siftUp(i);
        LOG_D("定时器：添加新节点 id={}, 超时时间={}ms", id, timeout);
    } else {
        /* 已有结点：调整堆 */
        i = _ref[id];
        if (i >= _heap.size()) {
            // 修复引用表和堆不一致的问题
            LOG_W("定时器：发现无效引用 id={}, 索引={}, 堆大小={}", id, i,
                  _heap.size());
            _ref.erase(id);
            // 重新添加
            _ref[id] = _heap.size();
            _heap.push_back({id, Clock::now() + MS(timeout), cb});
            siftUp(_heap.size() - 1);
            return;
        }

        _heap[i].expires = Clock::now() + MS(timeout);
        _heap[i].callback = cb;
        if (!siftDown(i, _heap.size())) {
            siftUp(i);
        }
        LOG_D("定时器：更新节点 id={}, 超时时间={}ms", id, timeout);
    }
}

void Timer::doWork(int id) {
    /* 删除指定id结点，并触发回调函数 */
    if (_heap.empty() || _ref.count(id) == 0) {
        return;
    }
    size_t i = _ref[id];

    // 防止索引越界
    if (i >= _heap.size()) {
        _ref.erase(id); // 清理无效引用
        return;
    }

    // 复制回调函数，避免在执行回调过程中修改节点导致问题
    auto callback = _heap[i].callback;
    del(i); // 先删除节点，避免回调中再次操作定时器造成问题

    // 执行回调函数
    if (callback) {
        try {
            callback();
        } catch (const std::exception& e) {
            // 防止回调异常导致崩溃
            // 记录错误但不中断程序
        }
    }
}

void Timer::del(size_t index) {
    /* 删除指定位置的结点 */
    // 检查堆是否为空或索引是否越界
    if (_heap.empty() || index >= _heap.size()) {
        return;
    }

    /* 将要删除的结点换到队尾，然后调整堆 */
    size_t i = index;
    size_t n = _heap.size() - 1;

    // 保存要删除的节点ID，以便稍后从map中删除
    int nodeId = _heap[i].id;

    if (i < n) {
        swapNode(i, n);
        if (!siftDown(i, n)) {
            siftUp(i);
        }
    }

    /* 队尾元素删除 */
    _ref.erase(nodeId);
    _heap.pop_back();
}

void Timer::Adjust(int id, int timeout) {
    /* 调整指定id的结点 */
    assert(!_heap.empty() && _ref.count(id) > 0);
    _heap[_ref[id]].expires = Clock::now() + MS(timeout);
    ;
    siftDown(_ref[id], _heap.size());
}

void Timer::Tick() {
    /* 清除超时结点 */
    if (_heap.empty()) {
        return;
    }
    int processedCount = 0;
    while (!_heap.empty()) {
        TimerNode node = _heap.front();
        auto timeRemaining =
            std::chrono::duration_cast<MS>(node.expires - Clock::now()).count();

        if (timeRemaining > 0) {
            break;
        }

        // 先删除节点，避免回调中再次修改定时器
        LOG_D("定时器：触发超时回调 id={}", node.id);
        int id = node.id;
        auto callback = node.callback;
        Pop(); // 移除当前节点

        // 执行回调
        if (callback) {
            try {
                callback();
            } catch (const std::exception& e) {
                LOG_E("定时器：回调执行异常 id={}, 错误={}", id, e.what());
            } catch (...) {
                LOG_E("定时器：回调执行未知异常 id={}", id);
            }
        }

        processedCount++;
        // 防止一次处理太多定时器事件
        if (processedCount >= 100) {
            LOG_W("定时器：单次处理事件过多，已处理{}个事件", processedCount);
            break;
        }
    }
}

void Timer::Pop() {
    if (_heap.empty()) {
        return; // 如果堆为空，直接返回，避免断言失败
    }
    del(0);
}

void Timer::Clear() {
    _ref.clear();
    _heap.clear();
}

int Timer::GetNextTick() {
    Tick();
    int res = -1;
    if (!_heap.empty()) {
        res =
            std::chrono::duration_cast<MS>(_heap.front().expires - Clock::now())
                .count();
        if (res < 0) {
            res = 0;
        }
    }
    return res;
}

// HeapTimerManager的DoSchedule实现
void TimerManager::DoSchedule(int milliseconds, int repeat,
                              std::function<void()> cb) {
    if (!cb) {
        LOG_W("定时器：尝试调度空回调函数");
        return;
    }

    int id = _nextId++;
    LOG_D("定时器：DoSchedule 调度定时器 id={}, 超时时间={}ms, 重复次数={}", id,
          milliseconds, repeat);

    // 保存重复次数和周期
    if (repeat != 0) {
        _repeats[id] = std::make_pair(repeat, milliseconds);
    }

    // 由于需要递归引用，我们需要使用std::function和std::shared_ptr
    auto callbackPtr = std::make_shared<std::function<void()>>();

    *callbackPtr = [this, id, cb, callbackPtr]() {
        try {
            // 执行用户回调
            cb();

            // 处理重复执行
            auto it = _repeats.find(id);
            if (it != _repeats.end()) {
                auto& [remaining, period] = it->second;
                if (remaining > 0) {
                    remaining--;
                    if (remaining == 0) {
                        LOG_D("定时器：任务完成，移除定时器 id={}", id);
                        _repeats.erase(it);
                    } else {
                        // 重新注册同一个ID的计时器
                        LOG_D("定时器：任务重新调度，剩余次数={} id={}",
                              remaining, id);
                        _timer.Add(id, period, *callbackPtr);
                    }
                } else if (remaining == -1) { // 无限重复
                    // 重新注册同一个ID的计时器
                    LOG_D("定时器：无限循环任务重新调度 id={}", id);
                    _timer.Add(id, period, *callbackPtr);
                }
            }
        } catch (const std::exception& e) {
            LOG_E("定时器：回调执行异常 id={}, 错误={}", id, e.what());
        } catch (...) {
            LOG_E("定时器：回调执行未知异常 id={}", id);
        }
    };

    // 添加到计时器
    try {
        _timer.Add(id, milliseconds, *callbackPtr);
    } catch (const std::exception& e) {
        LOG_E("定时器：添加定时器异常 id={}, 错误={}", id, e.what());
        _repeats.erase(id); // 清理已创建的资源
    }
}

// 使用业务key的定时器调度实现
void TimerManager::DoScheduleWithKey(int key, int milliseconds, int repeat,
                                     std::function<void()> cb) {
    if (!cb) {
        LOG_W("定时器：尝试使用业务key={}调度空回调函数", key);
        return;
    }

    int id = _nextId++;
    LOG_D("定时器：DoScheduleWithKey 调度定时器 key={}, id={}, 超时时间={}ms, "
          "重复次数={}",
          key, id, milliseconds, repeat);

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
                            LOG_D("定时器：任务完成，移除定时器 key={}, id={}",
                                  key, id);
                            _repeats.erase(it);
                            _keyToTimerId.erase(key);
                        } else {
                            // 重新注册同一个ID的计时器
                            LOG_D("定时器：任务重新调度，剩余次数={} key={}, "
                                  "id={}",
                                  remaining, key, id);
                            _timer.Add(id, period, *callbackPtr);
                        }
                    } else if (remaining == -1) { // 无限重复
                        // 重新注册同一个ID的计时器
                        LOG_D("定时器：无限循环任务重新调度 key={}, id={}", key,
                              id);
                        _timer.Add(id, period, *callbackPtr);
                    }
                } else {
                    // 非重复任务完成后从映射中移除
                    LOG_D("定时器：非重复任务完成，移除映射 key={}, id={}", key,
                          id);
                    _keyToTimerId.erase(key);
                }
            } else {
                LOG_W("定时器：key已失效或被替换 key={}, id={}", key, id);
            }
        } catch (const std::exception& e) {
            LOG_E("定时器：回调执行异常 key={}, id={}, 错误={}", key, id,
                  e.what());
        } catch (...) {
            LOG_E("定时器：回调执行未知异常 key={}, id={}", key, id);
        }
    };

    // 添加到计时器
    try {
        _timer.Add(id, milliseconds, *callbackPtr);
    } catch (const std::exception& e) {
        LOG_E("定时器：添加定时器异常 key={}, id={}, 错误={}", key, id,
              e.what());
        _repeats.erase(id); // 清理已创建的资源
        _keyToTimerId.erase(key);
    }
}

} // namespace zener::v0
