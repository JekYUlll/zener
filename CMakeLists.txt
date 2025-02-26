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

# 构建类型优化
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# 链接时优化(LTO)选项 - 可显著提高Release版本性能
option(ENABLE_LTO "Enable Link Time Optimization for Release builds" OFF)

if(ENABLE_LTO AND(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo"))
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_output)

    if(ipo_supported)
        message(STATUS "IPO/LTO enabled - 将大幅提升性能")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(STATUS "IPO/LTO not supported: ${ipo_output}")
    endif()
endif()

add_definitions(-D__USE_SPDLOG) # __USE_SPDLOG 使用 spdlog 而不是自己的 log 实现
add_definitions(-D__V0) # __V0 代表启用 webserver 11 原本实现

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

# 主服务器可执行文件 - 修改为使用后缀
add_executable(Zener${TIMER_SUFFIX} cmd/server/main.cpp)
target_link_libraries(Zener${TIMER_SUFFIX} PRIVATE
    zener_core
    PkgConfig::MYSQL
)

# 测试框架集成
enable_testing()

function(add_zener_test name)
    add_executable(${name} tests/${name}/main.cpp)
    target_link_libraries(${name} PRIVATE zener_core)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_zener_test(log_test)
add_zener_test(hardware_test)

# 完善安装规则 - 也需要修改
install(TARGETS Zener${TIMER_SUFFIX}
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION bin
)

# 安装配置文件
install(FILES ${PROJECT_SOURCE_DIR}/bin/config.toml
    DESTINATION etc/zener
    COMPONENT config)