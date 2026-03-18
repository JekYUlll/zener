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
    utils/error/error.cpp
    utils/log/_logger.cpp
    utils/log/use_spd_log.cpp
)

# 转换为绝对路径
list(TRANSFORM CORE_SOURCES PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/)

add_library(zener_core STATIC ${CORE_SOURCES})

target_include_directories(zener_core PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(zener_core PRIVATE
    spdlog
)

target_compile_options(zener_core PRIVATE
    $<$<CONFIG:Release>:-O3 -march=native -flto>
    $<$<CONFIG:Debug>:-O0 -g3>
)

target_link_options(zener_core INTERFACE
    $<$<CONFIG:Release>:-Wl,--gc-sections>
)
