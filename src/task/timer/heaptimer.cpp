#include "task/timer/heaptimer.h"
#include <cassert>
#include <cstddef>
#include <memory> // 为std::make_shared添加
#include <utility>

namespace zener::v0 {

Timer::Timer() {
    // std::vector::reserve 用于为 std::vector
    // 预先分配至少能容纳指定数量元素的内存空间
    _heap.reserve(64);
}

void Timer::siftUp(size_t i) {
    assert(i >= 0 && i < _heap.size()); // size_t 永远 >= 0 此处代码有点抽象
    size_t j = (i - 1) / 2;
    while (i > 0) { // 修改为检查i而不是j，防止size_t下溢
        j = (i - 1) / 2;
        if (_heap[j] < _heap[i]) {
            break;
        }
        swapNode(i, j);
        i = j;
    }
}

void Timer::swapNode(size_t i, size_t j) {
    assert(i >= 0 && i < _heap.size());
    assert(j >= 0 && j < _heap.size());
    std::swap(_heap[i], _heap[j]);
    _ref[_heap[i].id] = i;
    _ref[_heap[j].id] = j;
}

bool Timer::siftDown(size_t index, size_t n) {
    assert(index >= 0 && index < _heap.size());
    assert(n >= 0 && n <= _heap.size());
    size_t i = index;
    size_t j = i * 2 + 1;
    while (j < n) {
        if (j + 1 < n && _heap[j + 1] < _heap[j])
            j++;
        if (_heap[i] < _heap[j])
            break;
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
    } else {
        /* 已有结点：调整堆 */
        i = _ref[id];
        _heap[i].expires = Clock::now() + MS(timeout);
        _heap[i].callback = cb;
        if (!siftDown(i, _heap.size())) {
            siftUp(i);
        }
    }
}

void Timer::doWork(int id) {
    /* 删除指定id结点，并触发回调函数 */
    if (_heap.empty() || _ref.count(id) == 0) {
        return;
    }
    size_t i = _ref[id];
    TimerNode node = _heap[i];
    node.callback();
    del(i);
}

void Timer::del(size_t index) {
    /* 删除指定位置的结点 */
    assert(!_heap.empty() && index >= 0 && index < _heap.size());
    /* 将要删除的结点换到队尾，然后调整堆 */
    size_t i = index;
    size_t n = _heap.size() - 1;
    assert(i <= n);
    if (i < n) {
        swapNode(i, n);
        if (!siftDown(i, n)) {
            siftUp(i);
        }
    }
    /* 队尾元素删除 */
    _ref.erase(_heap.back().id);
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
    while (!_heap.empty()) {
        TimerNode node = _heap.front();
        if (std::chrono::duration_cast<MS>(node.expires - Clock::now())
                .count() > 0) {
            break;
        }
        node.callback();
        Pop();
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
    int id = _nextId++;

    // 保存重复次数和周期
    if (repeat != 0) {
        _repeats[id] = std::make_pair(repeat, milliseconds);
    }

    // 由于需要递归引用，我们需要使用std::function和std::shared_ptr
    auto callbackPtr = std::make_shared<std::function<void()>>();

    *callbackPtr = [this, id, cb, callbackPtr]() {
        // 执行用户回调
        cb();

        // 处理重复执行
        auto it = _repeats.find(id);
        if (it != _repeats.end()) {
            auto& [remaining, period] = it->second;
            if (remaining > 0) {
                remaining--;
                if (remaining == 0) {
                    _repeats.erase(it);
                } else {
                    // 重新注册同一个ID的计时器
                    _timer.Add(id, period, *callbackPtr);
                }
            } else if (remaining == -1) { // 无限重复
                // 重新注册同一个ID的计时器
                _timer.Add(id, period, *callbackPtr);
            }
        }
    };

    // 添加到计时器
    _timer.Add(id, milliseconds, *callbackPtr);
}

// 使用业务key的定时器调度实现
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
                        // 重新注册同一个ID的计时器
                        _timer.Add(id, period, *callbackPtr);
                    }
                } else if (remaining == -1) { // 无限重复
                    // 重新注册同一个ID的计时器
                    _timer.Add(id, period, *callbackPtr);
                }
            } else {
                // 非重复任务完成后从映射中移除
                _keyToTimerId.erase(key);
            }
        }
    };

    // 添加到计时器
    _timer.Add(id, milliseconds, *callbackPtr);
}

} // namespace zener::v0
