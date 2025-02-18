# 收集所有源文件
# file(GLOB_RECURSE SERVER_SOURCES 
#     "*.cpp"
#     "config/*.cpp"
#     "core/*.cpp"
#     "database/*.cpp"
#     "error/*.cpp"
#     "http/*.cpp"
#     "log/*.cpp"
#     "net/*.cpp"
#     "pool/*.cpp"
#     "timer/*.cpp"
# )

# 添加可执行文件
# add_executable(zener_server ${SERVER_SOURCES})

# 如果需要链接其他库
# target_link_libraries(zener_server pthread)

add_subdirectory(cmd)