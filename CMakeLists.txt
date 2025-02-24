cmake_minimum_required(VERSION 3.22)

project(Zener VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions("-O0 -g -ggdb -Wno-unused-variable")
    add_definitions(-DDEBUG)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_definitions(-Os -DRELEASE)
endif()

include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third_party
)

#使用 spdlog 而不是自己的 log 实现
add_definitions(-D__USE_SPDLOG)
# V0 代表使用 webserver 11 原本实现
add_definitions(-D__V0)

# 设置输出目录
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

# 添加子目录
add_subdirectory(src) # 在src中收集了SERVER_SOURCES
add_subdirectory(cmd)
add_subdirectory(tests)

# 主 server
add_executable(Zener 
    ${CMAKE_SOURCE_DIR}/cmd/server/main.cpp
    ${SERVER_SOURCES}
)

add_executable(log_test 
    ${CMAKE_SOURCE_DIR}/tests/log_test/main.cpp
    ${SERVER_SOURCES}
)

add_executable(hardware_test
        ${CMAKE_SOURCE_DIR}/tests/hardware_test/main.cpp
        ${SERVER_SOURCES}
)

# 查找 MySQL SDK
list(APPEND CMAKE_PREFIX_PATH "/usr")
set(MYSQLCLIENT_STATIC_LINKING true)
set(FINDMYSQL_DEBUG true)

# 查找 MYSQL
# 使用 pkg-config 替代 CMake 配置
find_package(PkgConfig REQUIRED)
# find_package(MySQL REQUIRED) # 直接找 MYSQL 找不到
pkg_check_modules(MYSQL REQUIRED IMPORTED_TARGET
        mysqlclient
)

include_directories(${MYSQL_INCLUDE_DIRS})

target_link_libraries(Zener ${MYSQL_LIBRARIES})
target_link_libraries(log_test ${MYSQL_LIBRARIES})
target_link_libraries(hardware_test ${MYSQL_LIBRARIES})

# # 安装规则（可选）
# install(TARGETS Zener DESTINATION bin)


