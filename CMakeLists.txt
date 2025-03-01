cmake_minimum_required(VERSION 3.21)
project(Zener
    VERSION 1.0
    DESCRIPTION "High-performance Web Server"
    LANGUAGES CXX
)

# 启用现代目标属性传播
cmake_policy(SET CMP0079 NEW)

# 全局配置
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 输出目录配置
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)

# 禁用高级指令集优化 | 临时设置，为了兼容 valgrind
set(CMAKE_CXX_FLAGS "-march=x86-64 -mtune=generic -O2")

# 禁用所有 SIMD 优化
set(CMAKE_CXX_FLAGS "-mno-avx -mno-avx2 -mno-sse4.1 -mno-sse4.2 -march=x86-64 -mtune=generic")

# 构建类型优化 - 强制设置为Release，除非明确指定其他类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    message(STATUS "设置构建类型为Release (默认值)")
endif()

message(STATUS "当前构建类型: ${CMAKE_BUILD_TYPE}")

# 链接时优化(LTO)选项 - 可显著提高Release版本性能
option(ENABLE_LTO "Enable Link Time Optimization for Release builds" ON)

if(ENABLE_LTO AND(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo"))
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_output)

    if(ipo_supported)
        message(STATUS "IPO/LTO enabled - 提升性能")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(STATUS "IPO/LTO not supported: ${ipo_output}")
    endif()
endif()

add_definitions(-D__USE_SPDLOG) # 使用 spdlog
add_definitions(-D__V0) # __V0

include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third_party
)

# 改进编译器选项设置
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_definitions("-O0 -g -ggdb -Wall -Wextra -Wno-unused-variable")
        add_definitions(-DDEBUG)
    elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_definitions("-Os -Wall -Wextra")
        add_definitions(-DRELEASE)
    endif()
elseif(MSVC)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_definitions("/W4 /DEBUG")
        add_definitions(-DDEBUG)
    elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_definitions("/W4 /O2")
        add_definitions(-DRELEASE)
    endif()
endif()

# 定时器实现选择 - 使用单一选项更便于管理
set(TIMER_IMPLEMENTATION "MAP" CACHE STRING "Timer implementation to use (MAP or HEAP)")
set_property(CACHE TIMER_IMPLEMENTATION PROPERTY STRINGS "MAP" "HEAP")

# 根据定时器实现设置可执行文件名称后缀
if(TIMER_IMPLEMENTATION STREQUAL "MAP")
    add_definitions(-D__USE_MAPTIMER)
    set(TIMER_SUFFIX "-map")
    message(STATUS "Using map-based timer implementation (红黑树定时器)")
else()
    set(TIMER_SUFFIX "-heap")
    message(STATUS "Using heap-based timer implementation (堆定时器)")
endif()

# SPDlog源码集成（仅头文件）
add_library(spdlog INTERFACE)
target_include_directories(spdlog INTERFACE
    ${PROJECT_SOURCE_DIR}/third_party/spdlog/include
)

# MySQL配置优化
list(APPEND CMAKE_PREFIX_PATH "/usr")
set(MYSQLCLIENT_STATIC_LINKING true)
set(FINDMYSQL_DEBUG true)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MYSQL REQUIRED IMPORTED_TARGET mysqlclient)
include_directories(${MYSQL_INCLUDE_DIRS})

# 添加子目录 收集了 CORE_SOURCES
add_subdirectory(src)

# 主服务器可执行文件 - 同时创建基本版本和特定定时器版本
add_executable(Zener cmd/server/main.cpp)
add_executable(Zener${TIMER_SUFFIX} cmd/server/main.cpp)

# 为基本版本链接库
target_link_libraries(Zener PRIVATE
    zener_core
    PkgConfig::MYSQL
)

# 为特定定时器版本链接库
target_link_libraries(Zener${TIMER_SUFFIX} PRIVATE
    zener_core
    PkgConfig::MYSQL
)

# 添加自定义命令，在Release模式下自动strip二进制文件
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_custom_command(TARGET Zener POST_BUILD
        COMMAND ${CMAKE_STRIP} $<TARGET_FILE:Zener>
        COMMENT "Stripping debug symbols from ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Zener"
    )
    add_custom_command(TARGET Zener${TIMER_SUFFIX} POST_BUILD
        COMMAND ${CMAKE_STRIP} $<TARGET_FILE:Zener${TIMER_SUFFIX}>
        COMMENT "Stripping debug symbols from ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Zener${TIMER_SUFFIX}"
    )
endif()

# 测试框架集成
enable_testing()

function(add_zener_test name)
    add_executable(${name} tests/${name}/main.cpp)
    target_link_libraries(${name} PRIVATE zener_core)
    add_test(NAME ${name} COMMAND ${name})

    # 在Release模式下strip测试二进制文件
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND ${CMAKE_STRIP} $<TARGET_FILE:${name}>
            COMMENT "Stripping debug symbols from ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${name}"
        )
    endif()
endfunction()

add_zener_test(log_test)
add_zener_test(hardware_test)

# 完善安装规则 - 安装基本版本和特定定时器版本
install(TARGETS Zener Zener${TIMER_SUFFIX}
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION bin
)

# 安装配置文件
install(FILES ${PROJECT_SOURCE_DIR}/bin/config.toml
    DESTINATION etc/zener
    COMPONENT config)