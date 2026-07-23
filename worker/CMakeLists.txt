cmake_minimum_required(VERSION 3.20)

# C is for the main codebase; CXX is enabled only to compile whisper.cpp itself.
project(vlc_whisper_worker C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Do not build whisper.cpp command-line programs, tests, server, or demos.
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)



# Keep MinGW output self-contained where practical.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/whisper.cpp)

add_executable(vlc-whisper-worker
    src/main.c
)

target_compile_options(vlc-whisper-worker PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

target_link_libraries(vlc-whisper-worker PRIVATE
    whisper
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(vlc-whisper-worker PRIVATE OpenMP::OpenMP_CXX)
endif()