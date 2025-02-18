#ifndef ZWS_CONFIG_COMMON_H
#define ZWS_CONFIG_COMMON_H

#define USE_SPDLOG
#define ZWS_CONFIG_FILEPATH "config/app.toml"

#ifndef __USE__STRING
namespace zws {
#define String std::string
} // namespace zws
#endif // !__USE__STRING

#if defined(_MSC_VER)
#define ZWSINLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define ZWSINLINE inline __attribute__((always_inline))
#else // 默认处理
#define ZWSINLINE inline
#endif

#endif // !ZWS_CONFIG_COMMON_H