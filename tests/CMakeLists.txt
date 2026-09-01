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
add_vw_test(test_protocol_util unit/test_protocol_util.c)
add_vw_test(test_queue unit/test_queue.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_queue.c)
add_vw_test(test_audio_capture unit/test_audio_capture.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_audio_capture.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_queue.c)
add_vw_test(test_segment_builder unit/test_segment_builder.c ${CMAKE_SOURCE_DIR}/worker/src/vw_segment_builder.c ${CMAKE_SOURCE_DIR}/worker/src/vw_hallucination_filter.c)
add_vw_test(test_hallucination_filter unit/test_hallucination_filter.c ${CMAKE_SOURCE_DIR}/worker/src/vw_hallucination_filter.c)
add_vw_test(test_worker_queue unit/test_worker_queue.c ${CMAKE_SOURCE_DIR}/worker/src/vw_worker_queue.c)
add_vw_test(test_caption_timing unit/test_caption_timing.c)
add_vw_test(test_benchmark unit/test_benchmark.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_benchmark.c)
add_vw_test(test_caption_presenter unit/test_caption_presenter.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_caption_presenter.c)
target_compile_definitions(test_caption_presenter PRIVATE __PLUGIN__ MODULE_STRING="vlc_whisper" _GNU_SOURCE)

if(WIN32)
    set(VW_SOURCE_DECODER_SRCS ${CMAKE_SOURCE_DIR}/worker/src/vw_source_decoder_mf.c)
else()
    set(VW_SOURCE_DECODER_SRCS ${CMAKE_SOURCE_DIR}/worker/src/vw_source_decoder_ffmpeg.c)
    find_package(PkgConfig)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(FFMPEG IMPORTED_TARGET libavformat libavcodec libswresample libavutil)
    endif()
endif()

add_vw_test(test_source_decoder unit/test_source_decoder.c ${VW_SOURCE_DECODER_SRCS})
if(FFMPEG_FOUND AND NOT WIN32)
    target_compile_definitions(test_source_decoder PRIVATE -DVW_WITH_FFMPEG)
    target_link_libraries(test_source_decoder PRIVATE PkgConfig::FFMPEG)
endif()

add_vw_test(test_worker_config unit/test_worker_config.c ${CMAKE_SOURCE_DIR}/worker/src/vw_worker_config.c)
target_link_libraries(test_worker_config PRIVATE whisper)
add_vw_test(test_audio_buffer unit/test_audio_buffer.c ${CMAKE_SOURCE_DIR}/worker/src/vw_audio_buffer.c)
add_vw_test(test_vad unit/test_vad.c ${CMAKE_SOURCE_DIR}/worker/src/vw_vad.c)
target_link_libraries(test_vad PRIVATE whisper)

add_vw_test(test_whisper_engine unit/test_whisper_engine.c ${CMAKE_SOURCE_DIR}/worker/src/vw_whisper_engine.c
            ${CMAKE_SOURCE_DIR}/plugin/src/vw_platform_linux.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_platform_win32.c)
target_link_libraries(test_whisper_engine PRIVATE whisper)
set_tests_properties(test_whisper_engine PROPERTIES SKIP_RETURN_CODE 77)

add_vw_test(test_model_download unit/test_model_download.c ${CMAKE_SOURCE_DIR}/worker/src/vw_sha256.c ${CMAKE_SOURCE_DIR}/worker/src/vw_model_download.c)
if(WIN32)
    target_link_libraries(test_model_download PRIVATE winhttp)
endif()

add_vw_test(test_translate unit/test_translate.c ${CMAKE_SOURCE_DIR}/worker/src/vw_translate.c)
target_compile_definitions(test_translate PRIVATE VW_TRANSLATE_TESTING=1)
if(WIN32)
    target_link_libraries(test_translate PRIVATE winhttp)
endif()

add_vw_test(test_translate_async unit/test_translate_async.c ${CMAKE_SOURCE_DIR}/worker/src/vw_translate.c
            ${CMAKE_SOURCE_DIR}/worker/src/vw_translate_async.c)
target_compile_definitions(test_translate_async PRIVATE VW_TRANSLATE_TESTING=1)
if(WIN32)
    target_link_libraries(test_translate_async PRIVATE winhttp)
endif()

add_vw_test(test_worker_ipc integration/test_worker_ipc.c ${CMAKE_SOURCE_DIR}/worker/src/vw_worker.c ${CMAKE_SOURCE_DIR}/worker/src/vw_worker_config.c ${CMAKE_SOURCE_DIR}/worker/src/vw_worker_queue.c ${CMAKE_SOURCE_DIR}/worker/src/vw_audio_buffer.c ${CMAKE_SOURCE_DIR}/worker/src/vw_segment_builder.c ${CMAKE_SOURCE_DIR}/worker/src/vw_hallucination_filter.c ${CMAKE_SOURCE_DIR}/worker/src/vw_whisper_engine.c ${CMAKE_SOURCE_DIR}/worker/src/vw_vad.c ${CMAKE_SOURCE_DIR}/worker/src/vw_sha256.c ${CMAKE_SOURCE_DIR}/worker/src/vw_model_download.c ${CMAKE_SOURCE_DIR}/worker/src/vw_translate.c ${CMAKE_SOURCE_DIR}/worker/src/vw_translate_async.c ${VW_SOURCE_DECODER_SRCS} ${CMAKE_SOURCE_DIR}/plugin/src/vw_worker_client.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_platform_linux.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_platform_win32.c)
target_link_libraries(test_worker_ipc PRIVATE whisper)
if(FFMPEG_FOUND AND NOT WIN32)
    target_compile_definitions(test_worker_ipc PRIVATE -DVW_WITH_FFMPEG)
    target_link_libraries(test_worker_ipc PRIVATE PkgConfig::FFMPEG)
endif()

if(WIN32)
    target_link_libraries(test_worker_ipc PRIVATE winhttp)
endif()
add_vw_test(test_worker_lifecycle integration/test_worker_lifecycle.c ${CMAKE_SOURCE_DIR}/worker/src/vw_worker.c ${CMAKE_SOURCE_DIR}/worker/src/vw_worker_config.c ${CMAKE_SOURCE_DIR}/worker/src/vw_worker_queue.c ${CMAKE_SOURCE_DIR}/worker/src/vw_audio_buffer.c ${CMAKE_SOURCE_DIR}/worker/src/vw_segment_builder.c ${CMAKE_SOURCE_DIR}/worker/src/vw_hallucination_filter.c ${CMAKE_SOURCE_DIR}/worker/src/vw_whisper_engine.c ${CMAKE_SOURCE_DIR}/worker/src/vw_vad.c ${CMAKE_SOURCE_DIR}/worker/src/vw_sha256.c ${CMAKE_SOURCE_DIR}/worker/src/vw_model_download.c ${CMAKE_SOURCE_DIR}/worker/src/vw_translate.c ${CMAKE_SOURCE_DIR}/worker/src/vw_translate_async.c ${VW_SOURCE_DECODER_SRCS} ${CMAKE_SOURCE_DIR}/plugin/src/vw_worker_client.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_platform_linux.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_platform_win32.c)
target_link_libraries(test_worker_lifecycle PRIVATE whisper)
if(FFMPEG_FOUND AND NOT WIN32)
    target_compile_definitions(test_worker_lifecycle PRIVATE -DVW_WITH_FFMPEG)
    target_link_libraries(test_worker_lifecycle PRIVATE PkgConfig::FFMPEG)
endif()

if(WIN32)
    target_link_libraries(test_worker_lifecycle PRIVATE winhttp)
endif()
add_vw_test(test_worker_client unit/vw_test_worker_client.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_worker_client.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_platform_linux.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_platform_win32.c)
add_vw_test(test_worker_client_seek_epoch unit/test_worker_client_seek_epoch.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_worker_client.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_platform_linux.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_platform_win32.c)
add_vw_test(test_worker_client_compat unit/test_worker_client_compat.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_worker_client.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_platform_linux.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_platform_win32.c)

if(WIN32)
    add_vw_test(test_platform unit/test_platform.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_platform_win32.c)
else()
    add_vw_test(test_platform unit/test_platform.c ${CMAKE_SOURCE_DIR}/plugin/src/vw_platform_linux.c)
endif()

if(WIN32)
    # Windows Media Foundation and BCrypt libraries
    target_link_libraries(test_whisper_engine PRIVATE bcrypt)
    target_link_libraries(test_source_decoder PRIVATE bcrypt mfplat mfreadwrite mfuuid ole32 propsys)
    target_link_libraries(test_worker_ipc PRIVATE bcrypt mfplat mfreadwrite mfuuid ole32 propsys)
    target_link_libraries(test_worker_lifecycle PRIVATE bcrypt mfplat mfreadwrite mfuuid ole32 propsys)
    target_link_libraries(test_worker_client PRIVATE bcrypt)
    target_link_libraries(test_worker_client_seek_epoch PRIVATE bcrypt)
    target_link_libraries(test_worker_client_compat PRIVATE bcrypt)
    target_link_libraries(test_platform PRIVATE bcrypt)
endif()

add_executable(test_plugin_load integration/test_plugin_load.c)
target_compile_options(test_plugin_load PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(test_plugin_load PRIVATE ${CMAKE_DL_LIBS})
add_test(NAME test_plugin_load COMMAND test_plugin_load $<TARGET_FILE:vlc_whisper_plugin>)

if(VALGRIND_PROGRAM)
    get_property(_vw_tests DIRECTORY PROPERTY TESTS)
    if(_vw_tests)
        set_tests_properties(${_vw_tests} PROPERTIES ENVIRONMENT "VW_FORCE_CPU=1")
    endif()
endif()