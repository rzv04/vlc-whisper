# Build-time helper: fetch the pinned Whisper model if (and only if) absent.
# Invoked by the 'installer' / 'provision_models' targets — never at configure
# time — so plain and offline builds perform no network I/O. The expected hash
# pins integrity to models/manifest.json; a mismatch aborts the download.
if(EXISTS "${MODEL_PATH}")
  message(STATUS "VW: model already present, skipping download: ${MODEL_PATH}")
  return()
endif()

if(NOT MODEL_PATH OR NOT MODEL_URL OR NOT MODEL_SHA256)
  message(FATAL_ERROR "VW: MODEL_PATH, MODEL_URL and MODEL_SHA256 must all be set")
endif()

get_filename_component(_vw_model_dir "${MODEL_PATH}" DIRECTORY)
file(MAKE_DIRECTORY "${_vw_model_dir}")

message(STATUS
  "VW: fetching ${MODEL_PATH} (sha256-pinned). "
  "Offline? Place the file manually at this path and re-run — the download "
  "is skipped whenever the file exists.")
file(DOWNLOAD
  "${MODEL_URL}"
  "${MODEL_PATH}"
  EXPECTED_HASH SHA256=${MODEL_SHA256}
  SHOW_PROGRESS)
