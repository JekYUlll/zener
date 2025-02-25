#include "utils/defer.h"

namespace zener {

Defer::Defer(std::function<void()> func)
    : _func(std::move(func)), _active(true) {}

Defer::Defer(Defer&& other) noexcept
    : _func(std::move(other._func)), _active(other._active) {
    other._active = false;
}

Defer::~Defer() {
    if (_active && _func) {
        _func();
    }
}

} // namespace zener