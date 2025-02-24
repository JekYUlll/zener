#ifndef ZENER_CONFIG_COMMON_H
#define ZENER_CONFIG_COMMON_H

#include "utils/defer.h"
#include "utils/free.hpp"
#include "utils/hash.hpp"
#include "utils/log/logger.h"

// 实际上没有用到
#define ZENER_CONFIG_FILEPATH "config.toml"

#ifdef __USE_STRING
namespace zws {
#define String std::string
} // namespace zws
#endif // !__USE_STRING

#if defined(_MSC_VER)
#define _ZENER_INLINE __forceinline
#define _ZENER_SHORT_FUNC [[nodiscard]] inline
#define _ZENER_SHORT_INLINE_FUNC [[nodiscard]] __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define _ZENER_INLINE inline __attribute__((always_inline))
#define _ZENER_SHORT_FUNC [[nodiscard]] inline
#define _ZENER_SHORT_INLINE_FUNC [[nodiscard, __gnu__::__always_inline__]]
#else
#define _ZENER_INLINE inline
#define _ZENER_SHORT_FUNC [[nodiscard]] inline
#define _ZENER_SHORT_INLINE_FUNC [[nodiscard]] inline
#endif // !defined(_MSC_VER)

#endif // !ZENER_CONFIG_COMMON_H
