# Verifies required Windows release inputs before NSIS compilation.
# Fails hard so a misconfigured build never ships stale workers or unverified model weights.
if(NOT DEFINED WORKER_DIR)
  message(FATAL_ERROR "WORKER_DIR not defined")
endif()
set(_gpu "${WORKER_DIR}/vlc-whisper-worker.exe")
set(_cpu "${WORKER_DIR}/vlc-whisper-worker-cpu.exe")

# When VW_REQUIRE_CPU_FALLBACK is ON (GPU package), both workers must exist.
# For explicit CPU-only builds, the CPU worker is required and the GPU worker may be absent.
if(VW_REQUIRE_CPU_FALLBACK)
  if(NOT EXISTS "${_gpu}")
    message(FATAL_ERROR "Missing GPU worker '${_gpu}'. The production Windows release must not silently degrade to CPU-only.")
  endif()
  if(NOT EXISTS "${_cpu}")
    message(FATAL_ERROR
      "Missing CPU fallback worker '${_cpu}'. The GPU installer must always bundle the CPU worker for loader-less fallback.")
  endif()
else()
  if(NOT EXISTS "${_cpu}")
    message(FATAL_ERROR "Missing CPU worker '${_cpu}' for CPU-only package build")
  endif()
endif()

if(NOT DEFINED PLUGIN_PATH OR NOT EXISTS "${PLUGIN_PATH}")
  message(FATAL_ERROR "Missing plugin '${PLUGIN_PATH}'")
endif()

function(vw_verify_release_model model_path expected_sha256 label)
  if(NOT model_path OR NOT expected_sha256)
    message(FATAL_ERROR "Missing ${label} model path/hash configuration")
  endif()
  if(NOT EXISTS "${model_path}")
    message(FATAL_ERROR "Missing required ${label} model '${model_path}'")
  endif()
  file(SHA256 "${model_path}" _actual_sha256)
  if(NOT _actual_sha256 STREQUAL expected_sha256)
    message(FATAL_ERROR
      "${label} model SHA-256 mismatch for '${model_path}'\n"
      "  expected: ${expected_sha256}\n"
      "  actual:   ${_actual_sha256}")
  endif()
  message(STATUS "Verified ${label} model: ${model_path}")
endfunction()

vw_verify_release_model("${WHISPER_MODEL_PATH}" "${WHISPER_MODEL_SHA256}" "Whisper tiny")
vw_verify_release_model("${VAD_MODEL_PATH}" "${VAD_MODEL_SHA256}" "Silero VAD")

message(STATUS "Release inputs validated: gpu=${_gpu} cpu=${_cpu} plugin=${PLUGIN_PATH}")
