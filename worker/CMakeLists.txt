cmake_minimum_required(VERSION 3.25)

include(CheckIncludeFile)
include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)

set(VW_WORKER_COMMON_SRCS
    src/main.c
    src/vw_worker.c
    src/vw_whisper_engine.c
    src/vw_vad.c
    src/vw_segment_builder.c
    src/vw_hallucination_filter.c
    src/vw_audio_buffer.c
    src/vw_worker_config.c
    src/vw_worker_queue.c
    src/vw_sha256.c
    src/vw_model_download.c
    src/vw_translate.c
    src/vw_translate_async.c
    src/vw_quality_hooks.c
    ../plugin/src/vw_platform_linux.c
    ../plugin/src/vw_platform_win32.c
)

set(VW_WORKER_LIBS vw_protocol whisper)
set(VW_WORKER_DEFINITIONS VW_WORKER_BUILD=1)
set(VW_WORKER_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/protocol/include
    ${CMAKE_SOURCE_DIR}/plugin/include
)

if(VW_QUALITY_BENCHMARK_HOOKS)
    list(APPEND VW_WORKER_DEFINITIONS VW_QUALITY_BENCHMARK_HOOKS=1)
endif()

if(WIN32)
    set(VW_SOURCE_DECODER_SRCS src/vw_source_decoder_mf.c)
else()
    set(VW_SOURCE_DECODER_SRCS src/vw_source_decoder_ffmpeg.c)
    find_package(PkgConfig)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(FFMPEG IMPORTED_TARGET libavformat libavcodec libswresample libavutil)
    endif()
endif()

# Preinclude the POSIX translation pipe policy so original descriptors are CLOEXEC, curl stdin/stdout stay blocking,
# and only parent endpoints become nonblocking after spawn through the same namespaced production/test seam.
if(NOT WIN32)
    set_source_files_properties(src/vw_translate.c PROPERTIES
        COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/include/vw_translate_pipe_policy.h"
        COMPILE_DEFINITIONS "VW_TRANSLATE_PIPE_POLICY_OVERRIDE=1"
    )
endif()

add_executable(vlc-whisper-worker
    ${VW_WORKER_COMMON_SRCS}
    ${VW_SOURCE_DECODER_SRCS}
)

set_target_properties(vlc-whisper-worker PROPERTIES
    OUTPUT_NAME "vlc-whisper-worker"
)

target_include_directories(vlc-whisper-worker PRIVATE ${VW_WORKER_INCLUDES})
target_compile_definitions(vlc-whisper-worker PRIVATE ${VW_WORKER_DEFINITIONS})
target_link_libraries(vlc-whisper-worker PRIVATE ${VW_WORKER_LIBS})

if(WIN32)
    target_link_libraries(vlc-whisper-worker PRIVATE winhttp bcrypt mfplat mfreadwrite mfuuid ole32 propsys)
else()
    target_link_libraries(vlc-whisper-worker PRIVATE Threads::Threads)
    if(FFMPEG_FOUND)
        target_link_libraries(vlc-whisper-worker PRIVATE PkgConfig::FFMPEG)
        target_compile_definitions(vlc-whisper-worker PRIVATE VW_HAVE_FFMPEG=1)
    endif()
endif()

if(MSVC)
    target_compile_options(vlc-whisper-worker PRIVATE /W4 /permissive-)
else()
    target_compile_options(vlc-whisper-worker PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Build CPU and optional GPU variants on Windows release configurations.
if(WIN32)
    add_executable(vlc-whisper-worker-cpu
        ${VW_WORKER_COMMON_SRCS}
        ${VW_SOURCE_DECODER_SRCS}
    )
    set_target_properties(vlc-whisper-worker-cpu PROPERTIES OUTPUT_NAME "vlc-whisper-worker-cpu")
    target_include_directories(vlc-whisper-worker-cpu PRIVATE ${VW_WORKER_INCLUDES})
    target_compile_definitions(vlc-whisper-worker-cpu PRIVATE ${VW_WORKER_DEFINITIONS})
    target_link_libraries(vlc-whisper-worker-cpu PRIVATE ${VW_WORKER_LIBS} winhttp bcrypt mfplat mfreadwrite mfuuid ole32 propsys)
    if(MSVC)
        target_compile_options(vlc-whisper-worker-cpu PRIVATE /W4 /permissive-)
    else()
        target_compile_options(vlc-whisper-worker-cpu PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

# Linux CPU alias used by CI/quality tooling and packaging conventions.
if(NOT WIN32)
    add_executable(vlc-whisper-worker-cpu ALIAS vlc-whisper-worker)
endif()

install(TARGETS vlc-whisper-worker
    RUNTIME DESTINATION bin
)

if(WIN32)
    install(TARGETS vlc-whisper-worker-cpu RUNTIME DESTINATION bin)
endif()

# Keep the worker output predictable for packaging scripts.
set_target_properties(vlc-whisper-worker PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/worker"
)
if(WIN32)
    set_target_properties(vlc-whisper-worker-cpu PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/worker"
    )
endif()

# whisper/ggml may carry OpenMP transitively. Link it explicitly for the executable
# when available so runtime dependency discovery remains deterministic.
find_package(OpenMP QUIET)
if(OpenMP_CXX_FOUND AND NOT WIN32)
    # On Linux ggml still builds with OpenMP; link the runtime here too. On Windows OpenMP is
    # disabled entirely (see GGML_OPENMP above) so the worker has no libgomp DLL dependency.
    target_link_libraries(vlc-whisper-worker PRIVATE OpenMP::OpenMP_CXX)
endif()
