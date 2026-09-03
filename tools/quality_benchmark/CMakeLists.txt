find_package(Threads REQUIRED)

add_executable(vw-quality-benchmark EXCLUDE_FROM_ALL
    vw_quality_runner.c
    ../../plugin/src/vw_worker_client.c
    ../../plugin/src/vw_platform_linux.c
    ../../plugin/src/vw_platform_win32.c
)

target_include_directories(vw-quality-benchmark PRIVATE
    ${CMAKE_SOURCE_DIR}/plugin/include
    ${CMAKE_SOURCE_DIR}/protocol/include
    ${CMAKE_SOURCE_DIR}/worker/include
)

target_link_libraries(vw-quality-benchmark PRIVATE
    vw_protocol
    Threads::Threads
)

if(WIN32)
    target_link_libraries(vw-quality-benchmark PRIVATE bcrypt advapi32)
endif()

target_compile_options(vw-quality-benchmark PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
