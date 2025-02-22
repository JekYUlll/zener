#ifndef ZENER_CONFIG_COMMON_H
#define ZENER_CONFIG_COMMON_H

#include "utils/defer.h"
#include "utils/free.hpp"
#include "utils/hash.hpp"

// #define __USE_SPDLOG
// #define USE_TOMLPLUSPLUS
#define ZENER_CONFIG_FILEPATH "config/app.toml"

#ifdef __USE_STRING
namespace zws {
#define String std::string
} // namespace zws
#endif // !__USE_STRING

#if defined(_MSC_VER)
#define ZENERINLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define ZENER_INLINE inline __attribute__((always_inline))
// #define ZENER_SHORT_FUNC [[nodiscard, __gnu__::__always_inline__]]
#define ZENER_SHORT_FUNC [[nodiscard]] inline
#else
#define ZENERINLINE inline
#endif // !defined(_MSC_VER)

#endif // !ZENER_CONFIG_COMMON_H
