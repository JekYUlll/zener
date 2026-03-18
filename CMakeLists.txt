cmake_minimum_required(VERSION 3.21)

project(Zener
    VERSION 1.0
    DESCRIPTION "High-performance Web Server"
    LANGUAGES CXX
)

cmake_policy(SET CMP0079 NEW)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)

# clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_definitions(-D__USE_SPDLOG)
# add_definitions(-DNO_LOG) # 关闭日志
add_definitions(-D__V0) # __V0

include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third_party
)

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

add_compile_definitions(FMT_DEPRECATED_ENUM_FORMATTING)
add_compile_definitions(SPDLOG_USE_STD_FORMAT)

# spdlog
add_library(spdlog INTERFACE)
add_definitions(-DFMT_HEADER_ONLY)
target_compile_definitions(spdlog INTERFACE
    FMT_DEPRECATED_ENUM_FORMATTING
    SPDLOG_USE_STD_FORMAT
    SPDLOG_HEADER_ONLY
    SPDLOG_USE_STD_FORMAT=0
    SPDLOG_FMT_EXTERNAL=0
)
target_include_directories(spdlog INTERFACE
    ${PROJECT_SOURCE_DIR}/third_party/spdlog/include
)

# MySQL
list(APPEND CMAKE_PREFIX_PATH "/usr")
set(MYSQLCLIENT_STATIC_LINKING true)
set(FINDMYSQL_DEBUG true)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MYSQL REQUIRED IMPORTED_TARGET mysqlclient)
include_directories(${MYSQL_INCLUDE_DIRS})

# CORE_SOURCES
add_subdirectory(src)

add_executable(Zener cmd/server/main.cpp)
# add_executable(Zener${TIMER_SUFFIX} cmd/server/main.cpp)

find_package(Boost REQUIRED COMPONENTS regex)

target_link_libraries(Zener PRIVATE
    zener_core
    spdlog
    PkgConfig::MYSQL
    Boost::regex
)
