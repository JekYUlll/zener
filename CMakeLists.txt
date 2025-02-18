cmake_minimum_required(VERSION 3.22)
project(zener_web_server)

set(CMAKE_VERBOSE_MAKEFILE ON)
add_definitions("-O0 -g -ggdb -Wno-unused-variable")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third_party
)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

add_subdirectory(src)
add_subdirectory(tests)

# 收集src目录下所有源文件
aux_source_directory(src ALL_SOURCES)
add_executable(MyExecutable
    ${ALL_SOURCES}
    src/log/log.cpp
)
