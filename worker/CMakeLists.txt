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

# GPU whisper (Vulkan) acceleration — roadmap step 17a, default ON. When the Vulkan SDK (with
# glslc) is present, ggml-vulkan is enabled and the exe links the Vulkan loader at link time
# (imports vulkan-1.dll/libvulkan.so at runtime; the CPU fallback only runs when the loader is
# present). VW_WITH_VULKAN=OFF builds a fully portable CPU-only worker (output name -cpu) for
# loader-less/old targets. Build-memory note: compiling ggml-vulkan shaders (glslc +
# vulkan-shaders-gen ExternalProject) is RAM-hungry — on 8GB hosts keep build parallelism modest
# (ninja -j2/-j4) to avoid OOM/swap thrash (see milestone3 postmortem).
option(VW_WITH_VULKAN "worker: build whisper.cpp with ggml-vulkan GPU backend" ON)

set(GGML_VULKAN OFF CACHE BOOL "" FORCE)
if(VW_WITH_VULKAN)
    # The Windows MinGW toolchain sets FIND_ROOT_PATH_MODE_PACKAGE ONLY, so find_package(Vulkan)
    # can never see the host SDK. Allow an explicit SDK root (postmortem layout: <root>/mingw for
    # libs+headers, <root>/spv for SPIRV-Headers) to be injected into the cross find path:
    #   VW_VULKAN_SDK=/home/razvan/vw-vulkan cmake --preset windows-x64-debug
    if(DEFINED ENV{VW_VULKAN_SDK} AND NOT "$ENV{VW_VULKAN_SDK}" STREQUAL "")
        list(APPEND CMAKE_FIND_ROOT_PATH "$ENV{VW_VULKAN_SDK}/mingw" "$ENV{VW_VULKAN_SDK}/spv")
        list(APPEND CMAKE_PREFIX_PATH "$ENV{VW_VULKAN_SDK}/mingw" "$ENV{VW_VULKAN_SDK}/spv")
        set(Vulkan_INCLUDE_DIR "$ENV{VW_VULKAN_SDK}/mingw/include" CACHE PATH "" FORCE)
        message(STATUS "VW: using Vulkan SDK root '$ENV{VW_VULKAN_SDK}' for cross find path")
    endif()
    # Probe for the SDK + glslc (same COMPONENTS ggml-vulkan will require). Non-required here so a
    # missing SDK degrades to a CPU-only build with a warning instead of a hard configure failure.
    find_package(Vulkan COMPONENTS glslc)
    if(Vulkan_FOUND AND Vulkan_GLSLC_EXECUTABLE)
        message(STATUS "VW: Vulkan SDK found — enabling ggml-vulkan GPU backend")
        set(GGML_VULKAN ON CACHE BOOL "" FORCE)
    else()
        message(WARNING "VW: VW_WITH_VULKAN=ON but no Vulkan SDK/glslc found — building CPU-only worker")
    endif()
endif()

find_package(Threads REQUIRED)

# whisper.cpp/ggml build with GGML_OPENMP (see above) and link OpenMP::OpenMP_CXX transitively
# on non-Windows; find it at this scope so the worker can also manage the OpenMP runtime.
find_package(OpenMP)

add_subdirectory(third_party/whisper.cpp)

if(WIN32)
    set(VW_SOURCE_DECODER_SRCS src/vw_source_decoder_mf.c)
else()
    set(VW_SOURCE_DECODER_SRCS src/vw_source_decoder_ffmpeg.c)
    find_package(PkgConfig)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(FFMPEG IMPORTED_TARGET libavformat libavcodec libswresample libavutil)
    endif()
endif()

add_executable(vlc-whisper-worker
    src/main.c
    src/vw_worker.c
    src/vw_whisper_engine.c
    src/vw_vad.c
    src/vw_segment_builder.c
    src/vw_audio_buffer.c
    src/vw_worker_config.c
    src/vw_worker_queue.c
    ${VW_SOURCE_DECODER_SRCS}
    ../plugin/src/vw_platform_linux.c
    ../plugin/src/vw_platform_win32.c
)

if(FFMPEG_FOUND AND NOT WIN32)
    target_compile_definitions(vlc-whisper-worker PRIVATE -DVW_WITH_FFMPEG)
    target_link_libraries(vlc-whisper-worker PRIVATE PkgConfig::FFMPEG)
endif()

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

# Dist artifact naming: the GPU (default) build keeps the canonical name the plugin discovers;
# the CPU-only build gets a distinct -cpu suffix so both can ship side by side (see plan 17a).
if(GGML_VULKAN)
    set_target_properties(vlc-whisper-worker PROPERTIES OUTPUT_NAME "vlc-whisper-worker")
else()
    set_target_properties(vlc-whisper-worker PROPERTIES OUTPUT_NAME "vlc-whisper-worker-cpu")
endif()

target_link_libraries(vlc-whisper-worker PRIVATE
    vw_protocol
    whisper
    Threads::Threads
)

if(WIN32)
    # Windows Media Foundation and BCrypt libraries
    target_link_libraries(vlc-whisper-worker PRIVATE bcrypt mfplat mfreadwrite mfuuid ole32 propsys)
endif()

if(OpenMP_CXX_FOUND AND NOT WIN32)
    # On Linux ggml still builds with OpenMP; link the runtime here too. On Windows OpenMP is
    # disabled entirely (see GGML_OPENMP above) so the worker has no libgomp DLL dependency.
    target_link_libraries(vlc-whisper-worker PRIVATE OpenMP::OpenMP_CXX)
endif()
