#include "task/timer/timer.h"

namespace zws {

HeapTimer::HeapTimer() {
    // std::vector::reserve 用于为 std::vector
    // 预先分配至少能容纳指定数量元素的内存空间
    _heap.reserve(64);
}

} // namespace zws