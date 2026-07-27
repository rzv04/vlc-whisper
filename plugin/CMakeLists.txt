add_library(vlc_whisper_plugin SHARED
    src/vlc_whisper_module.c
    src/vw_session.c
    src/vw_audio_capture.c
    src/vw_caption_presenter.c
    src/vw_worker_client_win32.c
    src/vw_queue.c
    src/vw_platform_win32.c
)

target_include_directories(vlc_whisper_plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../worker/third_party/vlc-3.0.23/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocol/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../worker/include
)

target_link_libraries(vlc_whisper_plugin PRIVATE
    vw_protocol
)

target_compile_options(vlc_whisper_plugin PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

set_target_properties(vlc_whisper_plugin PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "vlc_whisper_plugin"
)
