cmake_minimum_required(VERSION 3.20)

project(vlc_whisper_bergamot_translation_spike LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

option(VW_BERGAMOT_BUILD_ENGINE "Build the Bergamot-backed CLI in addition to spike core tests" ON)
set(VW_BERGAMOT_SOURCE_DIR "" CACHE PATH "Optional existing bergamot-translator checkout")

add_library(vw_bergamot_spike_core STATIC
    src/vw_bergamot_spike_core.cpp
)
target_include_directories(vw_bergamot_spike_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(BUILD_TESTING)
    add_executable(vw_bergamot_spike_core_tests
        tests/test_spike_core.cpp
    )
    target_link_libraries(vw_bergamot_spike_core_tests PRIVATE vw_bergamot_spike_core)
    add_test(NAME vw_bergamot_spike_core_tests COMMAND vw_bergamot_spike_core_tests)
endif()

if(VW_BERGAMOT_BUILD_ENGINE)
    # Keep all upstream knobs local to this standalone spike. Nothing here leaks into
    # VLC-Whisper's root build because this directory is not added by the root CMake.
    set(COMPILE_WASM OFF CACHE BOOL "" FORCE)
    set(COMPILE_TESTS OFF CACHE BOOL "" FORCE)
    set(COMPILE_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    set(COMPILE_PYTHON OFF CACHE BOOL "" FORCE)
    set(COMPILE_CUDA OFF CACHE BOOL "" FORCE)

    # Bergamot's native Linux build needs a discoverable BLAS implementation. Its
    # upstream USE_STATIC_LIBS=ON setting can prevent system BLAS discovery on some
    # distributions, so prefer normal system linkage for this Linux-only spike build.
    if(UNIX AND NOT APPLE)
        set(USE_STATIC_LIBS OFF CACHE BOOL "" FORCE)
    endif()

    if(VW_BERGAMOT_SOURCE_DIR)
        if(NOT EXISTS "${VW_BERGAMOT_SOURCE_DIR}/CMakeLists.txt")
            message(FATAL_ERROR "VW_BERGAMOT_SOURCE_DIR does not contain a Bergamot CMakeLists.txt")
        endif()
        add_subdirectory("${VW_BERGAMOT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_bergamot" EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(
            bergamot_source
            GIT_REPOSITORY https://github.com/browsermt/bergamot-translator.git
            # Pin an immutable commit. Deliberately do not use GIT_SHALLOW here:
            # a raw commit SHA may become unreachable by a future shallow clone.
            GIT_TAG 9271618ebbdc5d21ac4dc4df9e72beb7ce644774
            GIT_PROGRESS TRUE
            GIT_SUBMODULES_RECURSE TRUE
        )
        FetchContent_MakeAvailable(bergamot_source)
    endif()

    if(NOT TARGET bergamot-translator)
        message(FATAL_ERROR "Bergamot target 'bergamot-translator' was not created")
    endif()

    add_library(vw_bergamot_engine STATIC
        src/vw_bergamot_engine.cpp
    )
    target_include_directories(vw_bergamot_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(vw_bergamot_engine PRIVATE bergamot-translator)

    add_executable(vw-bergamot-translate-spike
        src/main.cpp
    )
    target_link_libraries(vw-bergamot-translate-spike PRIVATE
        vw_bergamot_spike_core
        vw_bergamot_engine
    )

    if(BUILD_TESTING)
        foreach(non_finite_budget nan inf)
            add_test(
                NAME vw_bergamot_rejects_budget_${non_finite_budget}
                COMMAND vw-bergamot-translate-spike benchmark
                    --model-config unused.yml
                    --budget-ms ${non_finite_budget}
                    unused.srt
            )
            set_tests_properties(
                vw_bergamot_rejects_budget_${non_finite_budget}
                PROPERTIES
                    WILL_FAIL TRUE
                    PASS_REGULAR_EXPRESSION "--budget-ms expects a positive finite number"
            )
        endforeach()

        foreach(count_option warmup repeat limit)
            add_test(
                NAME vw_bergamot_rejects_signed_${count_option}
                COMMAND vw-bergamot-translate-spike benchmark
                    --model-config unused.yml
                    --${count_option} -1
                    unused.srt
            )
            set_tests_properties(
                vw_bergamot_rejects_signed_${count_option}
                PROPERTIES
                    WILL_FAIL TRUE
                    PASS_REGULAR_EXPRESSION "--${count_option} expects a non-negative integer"
            )
        endforeach()
    endif()
endif()
