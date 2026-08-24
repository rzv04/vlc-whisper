# ==============================================================================
# Packaging Configuration for VLC-Whisper (CPack & NSIS)
# ==============================================================================
# ------------------------------------------------------------------------------
# Clean-checkout model provisioning.
# models/*.bin are gitignored; the NSIS installer requires ggml-tiny.en.bin.
# The model is fetched ONLY when the 'installer' (or 'provision_models') target
# is built and the file is absent — plain configure/build never touch network,
# keeping offline builds offline. Integrity pinned to the sha256 recorded in
# models/manifest.json (mismatch aborts the download).
# ------------------------------------------------------------------------------
set(VW_MODEL_TINY_EN "${CMAKE_CURRENT_SOURCE_DIR}/models/ggml-tiny.en.bin")
set(VW_MODEL_TINY_EN_URL "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.en.bin")
set(VW_MODEL_TINY_EN_SHA256 "c78c86576ed16665798939f268b20902c347d21098f98d71be68b3d61e0b0486")

if(NOT EXISTS "${VW_MODEL_TINY_EN}")
  message(STATUS
    "VW: models/ggml-tiny.en.bin is absent. Building the 'installer' target will "
    "fetch it automatically (sha256-pinned); 'provision_models' fetches it standalone. "
    "CPack ZIP omits it until provisioned.")
endif()

if(WIN32)
  find_program(MAKENSIS_EXECUTABLE makensis)

  if(MAKENSIS_EXECUTABLE)
    message(STATUS "Found NSIS compiler: ${MAKENSIS_EXECUTABLE}")

    # Configure the NSIS template
    set(NSIS_SCRIPT_IN "${CMAKE_CURRENT_SOURCE_DIR}/cmake/vw_installer.nsi.in")
    set(NSIS_SCRIPT_OUT "${CMAKE_CURRENT_BINARY_DIR}/vw_installer.nsi")

    configure_file(${NSIS_SCRIPT_IN} ${NSIS_SCRIPT_OUT} @ONLY)

    add_custom_target(provision_models
      COMMAND ${CMAKE_COMMAND}
              -DMODEL_PATH=${VW_MODEL_TINY_EN}
              -DMODEL_URL=${VW_MODEL_TINY_EN_URL}
              -DMODEL_SHA256=${VW_MODEL_TINY_EN_SHA256}
              -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/vw_provision_model.cmake
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Provisioning models/ggml-tiny.en.bin (skipped when present)..."
    )

    # Installer auto-provisions its mandatory model as a pre-step: building the
    # installer is an explicit opt-in to producing a distributable, so fetching
    # its pinned dependency here never surprises plain/offline builds.
    add_custom_target(installer
      COMMAND ${CMAKE_COMMAND}
              -DMODEL_PATH=${VW_MODEL_TINY_EN}
              -DMODEL_URL=${VW_MODEL_TINY_EN_URL}
              -DMODEL_SHA256=${VW_MODEL_TINY_EN_SHA256}
              -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/vw_provision_model.cmake
      COMMAND ${MAKENSIS_EXECUTABLE} ${NSIS_SCRIPT_OUT}
      DEPENDS vlc_whisper_plugin vlc-whisper-worker
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
      COMMENT "Compiling standalone Windows setup installer with NSIS..."
    )
  else()
    message(STATUS "makensis not found: NSIS installer target will not be registered.")
  endif()
  # CPack generic packaging configuration
  set(CPACK_PACKAGE_NAME "vlc-whisper")
  set(CPACK_PACKAGE_VENDOR "VLC-Whisper Contributors")
  set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
  set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
  set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Offline Whisper AI Real-Time Caption Plugin for VLC")
  set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}")
  set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
  set(CPACK_GENERATOR "ZIP")

  # Install rules for release archive
  install(TARGETS vlc_whisper_plugin
    RUNTIME DESTINATION plugins/audio_filter
    LIBRARY DESTINATION plugins/audio_filter
  )
  install(TARGETS vlc-whisper-worker
    RUNTIME DESTINATION .
  )
  install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/models"
    DESTINATION .
    FILES_MATCHING PATTERN "*.bin" PATTERN "*.json"
  )
  install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
    DESTINATION .
  )

  include(CPack)
endif()
