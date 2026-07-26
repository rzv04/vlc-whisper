cmake_minimum_required(VERSION 3.20)

project(vlc_whisper C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Option to toggle building unit/integration tests
option(BUILD_TESTING "Build unit and integration tests" ON)

# Configure static runtime linking for MinGW cross-compilation
if(WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -static")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++ -Wl,-Bstatic")
    add_link_options(-static -static-libgcc -static-libstdc++)
endif()

# Core protocol library
add_subdirectory(protocol)

# Isolated transcription worker executable
add_subdirectory(worker)

# Native VLC plugin module
add_subdirectory(plugin)

# Automated unit & integration tests
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# Separate sample snippet targets (marked EXCLUDE_FROM_ALL)
add_subdirectory(samples EXCLUDE_FROM_ALL)
