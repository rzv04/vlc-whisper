# Build-time helper: verify a pinned model and fetch it only when absent.
# Invoked by the installer / provision targets — never at configure time — so
# plain and offline builds perform no network I/O. Existing files are always
# hashed before reuse; an untracked local file can never bypass release integrity.
if(NOT MODEL_PATH OR NOT MODEL_URL OR NOT MODEL_SHA256)
  message(FATAL_ERROR "VW: MODEL_PATH, MODEL_URL and MODEL_SHA256 must all be set")
endif()

if(EXISTS "${MODEL_PATH}")
  file(SHA256 "${MODEL_PATH}" _vw_existing_sha256)
  if(NOT _vw_existing_sha256 STREQUAL MODEL_SHA256)
    message(FATAL_ERROR
      "VW: existing model SHA-256 mismatch for ${MODEL_PATH}\n"
      "  expected: ${MODEL_SHA256}\n"
      "  actual:   ${_vw_existing_sha256}\n"
      "Remove or replace the stale/corrupt file before packaging.")
  endif()
  message(STATUS "VW: model already present and SHA-256 verified: ${MODEL_PATH}")
  return()
endif()

if(NOT ALLOW_DOWNLOAD)
  message(FATAL_ERROR
    "VW: required model is not provisioned, and build-time downloads are disabled "
    "(zero-network default). Either place the pinned file at:\n"
    "  ${MODEL_PATH}\n"
    "or re-configure with -DVW_PROVISION_MODELS=ON to allow a one-time "
    "SHA-256-pinned download.")
endif()

get_filename_component(_vw_model_dir "${MODEL_PATH}" DIRECTORY)
file(MAKE_DIRECTORY "${_vw_model_dir}")

message(STATUS
  "VW: fetching ${MODEL_PATH} (SHA-256 pinned). "
  "Offline? Place the file manually at this path and re-run; existing files are verified before reuse.")
# Download to a .part sibling and rename only after the hash check passes, so
# a failed/interrupted download can never leave a corrupt file at MODEL_PATH.
set(_vw_model_part "${MODEL_PATH}.part")
file(DOWNLOAD
  "${MODEL_URL}"
  "${_vw_model_part}"
  EXPECTED_HASH SHA256=${MODEL_SHA256}
  SHOW_PROGRESS)
file(RENAME "${_vw_model_part}" "${MODEL_PATH}")
