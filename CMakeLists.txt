cmake_minimum_required(VERSION 3.20)

project(vlc_whisper VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Option to toggle building unit/integration tests
option(BUILD_TESTING "Build unit and integration tests" ON)

if(NOT WIN32)
    find_program(VW_CURL_EXECUTABLE NAMES curl REQUIRED)
    add_compile_definitions(VW_CURL_EXECUTABLE="${VW_CURL_EXECUTABLE}")
endif()

# Option to enable code coverage instrumentation
option(ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)
if(ENABLE_COVERAGE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
endif()

# Configure static runtime linking for MinGW cross-compilation
if(WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -static")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")
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
    find_program(VALGRIND_PROGRAM valgrind)
    if(VALGRIND_PROGRAM)
        set(MEMORYCHECK_COMMAND "${VALGRIND_PROGRAM}" CACHE FILEPATH "Valgrind command" FORCE)
        set(MEMORYCHECK_COMMAND_OPTIONS "--leak-check=full --show-leak-kinds=definite --error-exitcode=1 --errors-for-leak-kinds=definite --suppressions=${CMAKE_SOURCE_DIR}/cmake/valgrind.supp" CACHE STRING "" FORCE)
        set(MEMORYCHECK_SUPPRESSIONS_FILE "${CMAKE_SOURCE_DIR}/cmake/valgrind.supp" CACHE FILEPATH "" FORCE)
    endif()
    include(CTest)
    enable_testing()
    add_subdirectory(tests)


    if(VALGRIND_PROGRAM)
        add_custom_target(vw_memcheck_gate
            COMMAND ${CMAKE_COMMAND}
                    -DCTEST_BUILD_DIR=${CMAKE_BINARY_DIR}
                    -P ${CMAKE_SOURCE_DIR}/cmake/vw_memcheck_gate.cmake
            USES_TERMINAL)
    endif()
endif()

# Separate sample snippet targets (marked EXCLUDE_FROM_ALL)
add_subdirectory(samples EXCLUDE_FROM_ALL)

# Developer-only local ASR quality benchmark target (not packaged or built by default)
add_subdirectory(tools/quality_benchmark EXCLUDE_FROM_ALL)

# Packaging and distribution (CPack & NSIS installer)
include(cmake/vw_packaging.cmake)
