cmake_minimum_required(VERSION 3.22)
project(zener)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions("-O0 -g -ggdb -Wno-unused-variable")

include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third_party
)

add_definitions(-D__USE_SPDLOG)

# 设置输出目录
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

# 添加子目录
add_subdirectory(src)
add_subdirectory(tests)

# find_package(MYSQL REQUIRED)

# 查找 MySQL SDK
#set(MYSQLCLIENT_STATIC_LINKING true)
#set(FINDMYSQL_DEBUG true)
find_package(MySQL REQUIRED)
include_directories(${MYSQL_INCLUDE_DIRS})
# target_link_libraries(zener PRIVATE MYSQL::mysqlclient)
target_link_libraries(zener ${MYSQL_LIBRARIES})

