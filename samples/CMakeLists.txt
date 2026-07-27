# Dynamically configure CMake build targets for all C snippets in samples/snippets/

file(GLOB SNIPPET_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/snippets/*.c")

foreach(SNIPPET_SRC ${SNIPPET_SOURCES})
    # Extract file basename without extension (e.g. whisper_pcm from snippets/whisper_pcm.c)
    get_filename_component(SNIPPET_NAME ${SNIPPET_SRC} NAME_WE)
    set(TARGET_NAME "sample_${SNIPPET_NAME}")

    # Define executable target for snippet
    add_executable(${TARGET_NAME} ${SNIPPET_SRC})

    # Do not build samples by default during 'cmake --build build'
    set_target_properties(${TARGET_NAME} PROPERTIES EXCLUDE_FROM_ALL TRUE)

    # Add header include directories for whisper.cpp and VLC

    target_include_directories(${TARGET_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../protocol
        ${CMAKE_CURRENT_SOURCE_DIR}/../protocol/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../worker/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../plugin/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../worker/third_party/whisper.cpp/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../worker/third_party/vlc-3.0.23/include
    )

    # Link against protocol & whisper library targets
    if(TARGET vw_protocol)
        target_link_libraries(${TARGET_NAME} PRIVATE vw_protocol)
    endif()

    if(TARGET whisper)
        target_link_libraries(${TARGET_NAME} PRIVATE whisper)
        if(OpenMP_CXX_FOUND)
            target_link_libraries(${TARGET_NAME} PRIVATE OpenMP::OpenMP_CXX)
        endif()
    endif()

    if(WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_link_libraries(${TARGET_NAME} PRIVATE -static -static-libgcc -static-libstdc++)
    endif()

    list(APPEND ALL_SAMPLE_TARGETS ${TARGET_NAME})
endforeach()

if(ALL_SAMPLE_TARGETS)
    add_custom_target(samples DEPENDS ${ALL_SAMPLE_TARGETS})
endif()

