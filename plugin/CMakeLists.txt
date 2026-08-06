add_library(vlc_whisper_plugin SHARED
    src/vlc_whisper_module.c
    src/vw_session.c
    src/vw_audio_capture.c
    src/vw_caption_presenter.c
    src/vw_worker_client.c
    src/vw_queue.c
    src/vw_platform_win32.c
    src/vw_platform_linux.c
)

target_compile_definitions(vlc_whisper_plugin PRIVATE
    __PLUGIN__
    MODULE_STRING="vlc_whisper"
    _GNU_SOURCE
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
if(UNIX AND NOT APPLE)
  target_link_libraries(vlc_whisper_plugin PRIVATE dl)
endif()

if(WIN32)
    # Windows DLLs require an import library to resolve external symbols.
    # We generate one from a .def file containing the VLC API symbols we use.
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/libvlccore.dll.a
        COMMAND ${CMAKE_DLLTOOL} -d ${CMAKE_CURRENT_SOURCE_DIR}/libvlccore.def -l ${CMAKE_CURRENT_BINARY_DIR}/libvlccore.dll.a
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/libvlccore.def
    )
    add_custom_target(vlc_core_import DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/libvlccore.dll.a)
    add_dependencies(vlc_whisper_plugin vlc_core_import)
    target_link_libraries(vlc_whisper_plugin PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/libvlccore.dll.a bcrypt)
endif()

target_compile_options(vlc_whisper_plugin PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

set_target_properties(vlc_whisper_plugin PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "vlc_whisper_plugin"
)
