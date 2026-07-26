set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/whisper.cpp)

add_executable(vlc-whisper-worker
    src/main.c
    src/vw_worker.c
    src/vw_whisper_engine.c
    src/vw_vad.c
    src/vw_segment_builder.c
    src/vw_audio_buffer.c
    src/vw_worker_config.c
)

target_include_directories(vlc-whisper-worker PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocol/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/whisper.cpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vlc-3.0.23/include
)

target_compile_options(vlc-whisper-worker PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

target_link_libraries(vlc-whisper-worker PRIVATE
    vw_protocol
    whisper
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(vlc-whisper-worker PRIVATE OpenMP::OpenMP_CXX)
endif()
