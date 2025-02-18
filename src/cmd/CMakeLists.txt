# 收集源文件
file(GLOB_RECURSE SERVER_SOURCES 
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/config/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/database/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/error/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/http/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/log/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/net/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/pool/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/timer/*.cpp"
)

# 添加服务器可执行文件
add_executable(zener_server 
    server/main.cpp
    ${SERVER_SOURCES}
)

# 如果需要链接其他库
# target_link_libraries(zener_server pthread)
