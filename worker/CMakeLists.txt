set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# ggml's OpenMP (default ON) links OpenMP::OpenMP_CXX, which resolves to the explicit path
# libgomp.dll.a on MinGW — an import lib that adds a libgomp-1.dll runtime dependency no -static
# flag can remove (explicit paths beat driver flags). On Windows the worker exe is
# deployed standalone (ADR-010: statically linked MinGW runtime); a missing libgomp-1.dll would
# kill the spawned worker in the loader before main. ggml computes graphs with its own
# pthread-based ggml_threadpool (n_threads still works), so OpenMP is not needed for the worker.
if(WIN32)
    set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
endif()

# GPU whisper (Vulkan) is roadmap step 17a — out of scope for the MVP worker. ggml auto-enables
# GGML_VULKAN when a Vulkan SDK is present on the build host (e.g. /home/razvan/vw-vulkan), which
# links ggml-vulkan + the SDK's libvulkan and makes the exe import vulkan-1.dll at runtime — a
# non-system dependency on bare/old Windows, and a host-dependent (non-reproducible) build. Pin it
# OFF for the worker so every host builds the same CPU-only binary.
set(GGML_VULKAN OFF CACHE BOOL "" FORCE)

find_package(Threads REQUIRED)

# whisper.cpp/ggml build with GGML_OPENMP (see above) and link OpenMP::OpenMP_CXX transitively
# on non-Windows; find it at this scope so the worker can also manage the OpenMP runtime.
find_package(OpenMP)

add_subdirectory(third_party/whisper.cpp)

add_executable(vlc-whisper-worker
    src/main.c
    src/vw_worker.c
    src/vw_whisper_engine.c
    src/vw_vad.c
    src/vw_segment_builder.c
    src/vw_audio_buffer.c
    src/vw_worker_config.c
    src/vw_worker_queue.c
    ../plugin/src/vw_platform_linux.c
    ../plugin/src/vw_platform_win32.c
)

target_include_directories(vlc-whisper-worker PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugin/include
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
    Threads::Threads
)

if(WIN32)
    # vw_platform_win32.c uses BCryptGenRandom for the auth token (same as the test targets).
    target_link_libraries(vlc-whisper-worker PRIVATE bcrypt)
endif()

if(OpenMP_CXX_FOUND AND NOT WIN32)
    # On Linux ggml still builds with OpenMP; link the runtime here too. On Windows OpenMP is
    # disabled entirely (see GGML_OPENMP above) so the worker has no libgomp DLL dependency.
    target_link_libraries(vlc-whisper-worker PRIVATE OpenMP::OpenMP_CXX)
endif()
