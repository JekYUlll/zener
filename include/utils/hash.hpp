#ifndef ZENER_UTILS_HASH_HPP
#define ZENER_UTILS_HASH_HPP

#include <cstddef>
#include <cstdint>

namespace zws {

// 编译时字符串哈希
constexpr uint32_t hash_str(char const* str, std::size_t n) {
    return n == 0 ? 5381 : (hash_str(str, n - 1) * 33) ^ str[n - 1];
}

constexpr uint32_t operator"" _hash(char const* str, std::size_t n) {
    return hash_str(str, n);
}

} // namespace zws

#endif // !ZENER_UTILS_HASH_HPP
