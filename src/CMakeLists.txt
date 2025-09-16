# 显式声明源文件（避免GLOB）
set(CORE_SOURCES
    buffer/buffer.cpp
    config/config.cpp
    core/epoller.cpp
    core/server.cpp
    database/sql_connector.cpp
    http/conn.cpp
    http/file_cache.cpp
    http/request.cpp
    http/response.cpp
    task/threadpool.cpp
    task/threadpool_1.cpp
    task/timer/heaptimer.cpp
    task/timer/maptimer.cpp
    utils/defer.cpp
    utils/error/error.cpp
    utils/log/_logger.cpp
    utils/log/use_spd_log.cpp
)

# 转换为绝对路径
list(TRANSFORM CORE_SOURCES PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/)

# 核心库构建
add_library(zener_core STATIC ${CORE_SOURCES})

# 头文件包含
target_include_directories(zener_core PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# 依赖配置
target_link_libraries(zener_core PRIVATE
    spdlog
)

# 编译选项配置
target_compile_options(zener_core PRIVATE
    $<$<CONFIG:Release>:-O3 -march=native -flto>
    $<$<CONFIG:Debug>:-O0 -g3>
)

# 链接选项
target_link_options(zener_core INTERFACE
    $<$<CONFIG:Release>:-Wl,--gc-sections>
)
