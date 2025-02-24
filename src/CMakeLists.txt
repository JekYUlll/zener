cmake_minimum_required(VERSION 3.22)

file(GLOB_RECURSE SERVER_SOURCES
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/buffer/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/config/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/database/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/file/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/http/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/task/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/task/timer/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/utils/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/utils/log/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/utils/error/*.cpp"
)

# 显式传递变量到父作用域
set(SERVER_SOURCES ${SERVER_SOURCES} PARENT_SCOPE)