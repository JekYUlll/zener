#include "task/timer/heaptimer.h"
#include <cassert>
#include <cstddef>
#include <utility>

namespace zws {
namespace heaptimer {

HeapTimer::HeapTimer() {
    // std::vector::reserve 用于为 std::vector
    // 预先分配至少能容纳指定数量元素的内存空间
    _heap.reserve(64);
}

void HeapTimer::siftUp(size_t i) {
    assert(i >= 0 && i < _heap.size());
    size_t j = (i - 1) / 2;
    while (j >= 0) {
        if (_heap[j] < _heap[i]) {
            break;
        }
        swapNode(i, j);
        i = j;
        j = (i - 1) / 2;
    }
}

void HeapTimer::swapNode(size_t i, size_t j) {
    assert(i >= 0 && i < _heap.size());
    assert(j >= 0 && j < _heap.size());
    std::swap(_heap[i], _heap[j]);
    _ref[_heap[i].id] = i;
    _ref[_heap[j].id] = j;
}

bool HeapTimer::siftDown(size_t index, size_t n) {
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

void HeapTimer::Add(int id, int timeout, const TimeoutCallBack& cb) {
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

void HeapTimer::doWork(int id) {
    /* 删除指定id结点，并触发回调函数 */
    if (_heap.empty() || _ref.count(id) == 0) {
        return;
    }
    size_t i = _ref[id];
    TimerNode node = _heap[i];
    node.callback();
    del(i);
}

void HeapTimer::del(size_t index) {
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

void HeapTimer::Adjust(int id, int timeout) {
    /* 调整指定id的结点 */
    assert(!_heap.empty() && _ref.count(id) > 0);
    _heap[_ref[id]].expires = Clock::now() + MS(timeout);
    ;
    siftDown(_ref[id], _heap.size());
}

void HeapTimer::Tick() {
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

void HeapTimer::Pop() {
    assert(!_heap.empty());
    del(0);
}

void HeapTimer::Clear() {
    _ref.clear();
    _heap.clear();
}

int HeapTimer::GetNextTick() {
    Tick();
    size_t res = -1;
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

} // namespace heaptimer
} // namespace zws