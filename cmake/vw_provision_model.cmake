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

if(NOT ALLOW_DOWNLOAD)
  message(FATAL_ERROR
    "VW: models/ggml-tiny.en.bin is required but not provisioned, and build-time "
    "downloads are disabled (zero-network default). Either place the file at:\n"
    "  ${MODEL_PATH}\n"
    "or re-configure with -DVW_PROVISION_MODELS=ON to allow a one-time "
    "sha256-pinned download (models/manifest.json).")
endif()

get_filename_component(_vw_model_dir "${MODEL_PATH}" DIRECTORY)
file(MAKE_DIRECTORY "${_vw_model_dir}")

message(STATUS
  "VW: fetching ${MODEL_PATH} (sha256-pinned). "
  "Offline? Place the file manually at this path and re-run — the download "
  "is skipped whenever the file exists.")
# Download to a .part sibling and rename only after the hash check passes, so
# a failed/interrupted download can never leave a corrupt file at MODEL_PATH
# (the EXISTS short-circuit above would otherwise trust it forever).
set(_vw_model_part "${MODEL_PATH}.part")
file(DOWNLOAD
  "${MODEL_URL}"
  "${_vw_model_part}"
  EXPECTED_HASH SHA256=${MODEL_SHA256}
  SHOW_PROGRESS)
file(RENAME "${_vw_model_part}" "${MODEL_PATH}")
