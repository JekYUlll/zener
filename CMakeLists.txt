cmake_minimum_required(VERSION 3.22)

project(Zener VERSION 1.0 LANGUAGES CXX)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions("-O0 -g -ggdb -Wno-unused-variable")
else()
    add_definitions(-Os -DNDEBUG)
endif()

include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third_party
)

add_definitions(-D__USE_SPDLOG)

# V0 代表使用 webserver 11 原本实现
add_definitions(-D__V0)

# 设置输出目录
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

# 添加子目录
add_subdirectory(src)
add_subdirectory(tests)

# 查找 MYSQL
# 使用 pkg-config 替代 CMake 配置
find_package(PkgConfig REQUIRED)
pkg_check_modules(MYSQL REQUIRED mysqlclient)

include_directories(${MYSQL_INCLUDE_DIRS})
target_link_libraries(Zener ${MYSQL_LIBRARIES})

# # 安装规则（可选）
# install(TARGETS Zener DESTINATION bin)

# 查找 MySQL SDK
# list(APPEND CMAKE_PREFIX_PATH "/usr")
# set(MYSQLCLIENT_STATIC_LINKING true)
# set(FINDMYSQL_DEBUG true)

# find_package(MySQL REQUIRED)
# include_directories(${MYSQL_INCLUDE_DIRS})
# target_link_libraries(zener ${MYSQL_LIBRARIES})
