#ifndef ZENER_DEFER_H
#define ZENER_DEFER_H

#include <functional>

namespace zws {

class Defer {
  public:
    explicit Defer(std::function<void()> func);
    Defer(const Defer&) = delete;
    Defer& operator=(const Defer&) = delete;
    Defer(Defer&& other) noexcept;
    ~Defer();

    void cancel() { _active = false; }

  private:
    std::function<void()> _func;
    bool _active;
};

#define CONCAT_IMPL(x, y) x##y
#define CONCAT(x, y) CONCAT_IMPL(x, y)
#define defer(func) Defer CONCAT(_defer_, __LINE__)(func)

} // namespace zws

#endif // !ZENER_DEFER_H