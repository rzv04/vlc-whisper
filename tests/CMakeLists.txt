find_package(Threads REQUIRED)

macro(add_vw_test test_name)
    add_executable(${test_name} ${ARGN})
    target_include_directories(${test_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/protocol/include
        ${CMAKE_SOURCE_DIR}/worker/include
        ${CMAKE_SOURCE_DIR}/plugin/include
        ${CMAKE_SOURCE_DIR}/worker/third_party/vlc-3.0.23/include
        ${CMAKE_SOURCE_DIR}/worker/third_party/whisper.cpp/include
        ${CMAKE_SOURCE_DIR}/worker/third_party/whisper.cpp/ggml/include
        ${CMAKE_SOURCE_DIR}/tests/include
    )
    target_link_libraries(${test_name} PRIVATE vw_protocol Threads::Threads)
    target_compile_options(${test_name} PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ${test_name} COMMAND ${test_name})
endmacro()

add_vw_test(test_protocol_codec unit/test_protocol_codec.c)
add_vw_test(test_protocol_validate unit/test_protocol_validate.c)
add_vw_test(test_queue unit/test_queue.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_queue.c)
add_vw_test(test_audio_capture unit/test_audio_capture.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_audio_capture.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_queue.c)
add_vw_test(test_segment_builder unit/test_segment_builder.c ${CMAKE_SOURCE_DIR}/worker/src/vw_segment_builder.c)
add_vw_test(test_caption_timing unit/test_caption_timing.c)
add_vw_test(test_caption_presenter unit/test_caption_presenter.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_caption_presenter.c)
target_compile_definitions(test_caption_presenter PRIVATE __PLUGIN__ MODULE_STRING="vlc_whisper" _GNU_SOURCE)

add_vw_test(test_worker_config unit/test_worker_config.c ${CMAKE_SOURCE_DIR}/worker/src/vw_worker_config.c)

add_vw_test(test_worker_ipc integration/test_worker_ipc.c ${CMAKE_SOURCE_DIR}/worker/src/vw_worker.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_worker_client.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_platform_linux.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_platform_win32.c)
add_vw_test(test_worker_lifecycle integration/test_worker_lifecycle.c ${CMAKE_SOURCE_DIR}/worker/src/vw_worker.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_worker_client.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_platform_linux.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_platform_win32.c)

if(WIN32)
    add_vw_test(test_platform unit/test_platform.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_platform_win32.c)
else()
    add_vw_test(test_platform unit/test_platform.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_platform_linux.c)
endif()

if(WIN32)
    # vw_platform_win32.c uses BCryptGenRandom for the auth token
    target_link_libraries(test_worker_ipc PRIVATE bcrypt)
    target_link_libraries(test_worker_lifecycle PRIVATE bcrypt)
    target_link_libraries(test_platform PRIVATE bcrypt)
endif()

add_executable(test_plugin_load integration/test_plugin_load.c)
target_compile_options(test_plugin_load PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(test_plugin_load PRIVATE ${CMAKE_DL_LIBS})
add_test(NAME test_plugin_load COMMAND test_plugin_load $<TARGET_FILE:vlc_whisper_plugin>)
