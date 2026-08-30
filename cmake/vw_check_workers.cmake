# Verifies that required Windows worker binaries exist before NSIS compilation.
# Fails hard so a misconfigured build never ships a workerless or single-worker installer.
if(NOT DEFINED WORKER_DIR)
  message(FATAL_ERROR "WORKER_DIR not defined")
endif()
set(_gpu "${WORKER_DIR}/vlc-whisper-worker.exe")
set(_cpu "${WORKER_DIR}/vlc-whisper-worker-cpu.exe")
# When VW_REQUIRE_CPU_FALLBACK is ON (GPU package), both must exist.
# For pure CPU builds, GPU may be absent but CPU must exist.
if(VW_REQUIRE_CPU_FALLBACK)
  if(NOT EXISTS "${_gpu}")
    message(FATAL_ERROR "Missing GPU worker '${_gpu}'. Build windows-x64-release first.")
  endif()
  if(NOT EXISTS "${_cpu}")
    message(FATAL_ERROR
      "Missing CPU fallback worker '${_cpu}'. The GPU installer must always bundle the CPU worker for loader-less fallback. "
      "Build windows-x64-release-cpu and copy vlc-whisper-worker-cpu.exe into ${WORKER_DIR}/ "
      "(see README 'Building the Windows Installer').")
  endif()
else()
  if(NOT EXISTS "${_gpu}" AND NOT EXISTS "${_cpu}")
    message(FATAL_ERROR "No worker binary found in ${WORKER_DIR} (expected vlc-whisper-worker.exe or vlc-whisper-worker-cpu.exe)")
  endif()
endif()
# Also require plugin and models for packaging
if(NOT DEFINED PLUGIN_PATH OR NOT EXISTS "${PLUGIN_PATH}")
  message(FATAL_ERROR "Missing plugin '${PLUGIN_PATH}'")
endif()
foreach(_model IN LISTS REQUIRED_MODELS)
  if(NOT EXISTS "${_model}")
    message(FATAL_ERROR "Missing required model '${_model}' — run 'cmake --build --preset windows-x64-release --target provision_models' or place it manually")
  endif()
endforeach()
message(STATUS "Worker inputs validated: gpu=${_gpu} cpu=${_cpu}")
