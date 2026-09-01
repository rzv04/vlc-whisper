# vw_memcheck_gate.cmake - CTest memcheck gate (VW-034)
# Runs a fresh CPU-only CTest memcheck pass and fails on unsuppressed defects.

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED CTEST_BUILD_DIR OR CTEST_BUILD_DIR STREQUAL "")
  message(FATAL_ERROR "vw_memcheck_gate: CTEST_BUILD_DIR is required")
endif()

find_program(_vw_valgrind valgrind)
if(NOT _vw_valgrind)
  message(STATUS "vw_memcheck_gate: valgrind not found, skipping")
  return()
endif()

if(NOT EXISTS "${CTEST_BUILD_DIR}/DartConfiguration.tcl")
  message(FATAL_ERROR "vw_memcheck_gate: no configured CTest build at ${CTEST_BUILD_DIR}")
endif()

file(GLOB_RECURSE _vw_old_logs
  "${CTEST_BUILD_DIR}/Testing/Temporary/MemoryChecker.*.log"
  "${CTEST_BUILD_DIR}/Testing/**/MemoryChecker.*.log"
)
if(_vw_old_logs)
  file(REMOVE ${_vw_old_logs})
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env VW_FORCE_CPU=1
          ctest --test-dir "${CTEST_BUILD_DIR}" -T memcheck --output-on-failure
  RESULT_VARIABLE _vw_ctest_rc
  OUTPUT_VARIABLE _vw_ctest_out
  ERROR_VARIABLE _vw_ctest_err
)
if(NOT _vw_ctest_rc EQUAL 0)
  message("${_vw_ctest_out}")
  message("${_vw_ctest_err}")
  message(FATAL_ERROR "vw_memcheck_gate: CTest memcheck failed with exit code ${_vw_ctest_rc}")
endif()

file(GLOB_RECURSE _vw_logs
  "${CTEST_BUILD_DIR}/Testing/Temporary/MemoryChecker.*.log"
  "${CTEST_BUILD_DIR}/Testing/**/MemoryChecker.*.log"
)
if(NOT _vw_logs)
  message(FATAL_ERROR "vw_memcheck_gate: CTest produced no MemoryChecker logs")
endif()

set(_vw_failed 0)
set(_vw_checked 0)
foreach(_log IN LISTS _vw_logs)
  math(EXPR _vw_checked "${_vw_checked} + 1")
  file(READ "${_log}" _content)

  string(REGEX MATCH "definitely lost:[ \t]*([0-9,]+) bytes" _def_match "${_content}")
  if(_def_match)
    string(REGEX REPLACE ".*definitely lost:[ \t]*([0-9,]+) bytes.*" "\\1" _def_bytes "${_content}")
    string(REPLACE "," "" _def_bytes "${_def_bytes}")
    if(_def_bytes GREATER 0)
      message(STATUS "vw_memcheck_gate: definite leak ${_def_bytes} bytes in ${_log}")
      set(_vw_failed 1)
    endif()
  endif()

  string(REGEX MATCH "ERROR SUMMARY:[ \t]*([0-9,]+) errors" _err_match "${_content}")
  if(_err_match)
    string(REGEX REPLACE ".*ERROR SUMMARY:[ \t]*([0-9,]+) errors.*" "\\1" _err_count "${_content}")
    string(REPLACE "," "" _err_count "${_err_count}")
    if(_err_count GREATER 0)
      message(STATUS "vw_memcheck_gate: ${_err_count} Valgrind errors in ${_log}")
      set(_vw_failed 1)
    endif()
  endif()
endforeach()

if(_vw_failed)
  message(FATAL_ERROR "vw_memcheck_gate: defects found in ${_vw_checked} MemoryChecker log(s)")
endif()

message(STATUS "vw_memcheck_gate: PASS - ${_vw_checked} MemoryChecker log(s) clean")
