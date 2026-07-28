macro(add_vw_test test_name)
    add_executable(${test_name} ${ARGN})
    target_include_directories(${test_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/protocol/include
        ${CMAKE_SOURCE_DIR}/worker/include
        ${CMAKE_SOURCE_DIR}/plugin/include
        ${CMAKE_SOURCE_DIR}/worker/third_party/whisper.cpp/include
        ${CMAKE_SOURCE_DIR}/tests/include
    )
    target_link_libraries(${test_name} PRIVATE vw_protocol)
    target_compile_options(${test_name} PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ${test_name} COMMAND ${test_name})
endmacro()

add_vw_test(test_protocol_codec unit/test_protocol_codec.c)
add_vw_test(test_protocol_validate unit/test_protocol_validate.c)
add_vw_test(test_queue unit/test_queue.c)
add_vw_test(test_segment_builder unit/test_segment_builder.c ${CMAKE_SOURCE_DIR}/worker/src/vw_segment_builder.c)
add_vw_test(test_caption_timing unit/test_caption_timing.c)

add_vw_test(test_worker_ipc integration/test_worker_ipc.c)
add_vw_test(test_worker_lifecycle integration/test_worker_lifecycle.c)
