cmake_minimum_required(VERSION 3.20)

# C is for the main codebase; CXX is enabled only to compile whisper.cpp itself.
project(vlc_whisper_worker C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Do not build whisper.cpp command-line programs, tests, server, or demos.
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)

# Keep MinGW output self-contained where practical.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

if(WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -static")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++ -Wl,-Bstatic")
    add_link_options(-static -static-libgcc -static-libstdc++)

    execute_process(
        COMMAND ${CMAKE_C_COMPILER} -print-file-name=libgomp.a
        OUTPUT_VARIABLE MINGW_LIBGOMP_STATIC
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(EXISTS "${MINGW_LIBGOMP_STATIC}")
        set(OpenMP_gomp_LIBRARY "${MINGW_LIBGOMP_STATIC}" CACHE FILEPATH "Path to static libgomp" FORCE)
        set(OpenMP_libgomp_LIBRARY "${MINGW_LIBGOMP_STATIC}" CACHE FILEPATH "Path to static libgomp" FORCE)
    endif()

    execute_process(
        COMMAND ${CMAKE_C_COMPILER} -print-file-name=libwinpthread.a
        OUTPUT_VARIABLE MINGW_LIBWINPTHREAD_STATIC
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(EXISTS "${MINGW_LIBWINPTHREAD_STATIC}")
        set(OpenMP_pthread_LIBRARY "${MINGW_LIBWINPTHREAD_STATIC}" CACHE FILEPATH "Path to static winpthread" FORCE)
        set(OpenMP_libwinpthread_LIBRARY "${MINGW_LIBWINPTHREAD_STATIC}" CACHE FILEPATH "Path to static winpthread" FORCE)
        set(OpenMP_libpthread_LIBRARY "${MINGW_LIBWINPTHREAD_STATIC}" CACHE FILEPATH "Path to static winpthread" FORCE)
        set(THREADS_PTHREADS_WIN32_LIBRARY "${MINGW_LIBWINPTHREAD_STATIC}" CACHE FILEPATH "Path to static winpthread" FORCE)
        set(CMAKE_THREAD_LIBS_INIT "${MINGW_LIBWINPTHREAD_STATIC}" CACHE STRING "Path to static winpthread" FORCE)
        set(CMAKE_HAVE_THREADS_LIBRARY 1 CACHE BOOL "" FORCE)
        set(Threads_FOUND TRUE CACHE BOOL "" FORCE)
    endif()
endif()

add_subdirectory(third_party/whisper.cpp)

if(WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
    execute_process(
        COMMAND ${CMAKE_C_COMPILER} -print-file-name=libwinpthread.a
        OUTPUT_VARIABLE MINGW_LIBWINPTHREAD_STATIC
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    execute_process(
        COMMAND ${CMAKE_C_COMPILER} -print-file-name=libgomp.a
        OUTPUT_VARIABLE MINGW_LIBGOMP_STATIC
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(EXISTS "${MINGW_LIBWINPTHREAD_STATIC}")
        if(TARGET Threads::Threads)
            set_target_properties(Threads::Threads PROPERTIES INTERFACE_LINK_LIBRARIES "${MINGW_LIBWINPTHREAD_STATIC}")
        endif()
    endif()
    if(EXISTS "${MINGW_LIBGOMP_STATIC}" AND EXISTS "${MINGW_LIBWINPTHREAD_STATIC}")
        if(TARGET OpenMP::OpenMP_CXX)
            set_target_properties(OpenMP::OpenMP_CXX PROPERTIES INTERFACE_LINK_LIBRARIES "${MINGW_LIBGOMP_STATIC};${MINGW_LIBWINPTHREAD_STATIC}")
        endif()
        if(TARGET OpenMP::OpenMP_C)
            set_target_properties(OpenMP::OpenMP_C PROPERTIES INTERFACE_LINK_LIBRARIES "${MINGW_LIBGOMP_STATIC};${MINGW_LIBWINPTHREAD_STATIC}")
        endif()
    endif()
endif()

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

# Add VLC bindings
target_include_directories(vlc-whisper-worker PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vlc-3.0.23/include
)

# Add samples snippets targets (excluded from default build)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../samples ${CMAKE_BINARY_DIR}/samples EXCLUDE_FROM_ALL)




