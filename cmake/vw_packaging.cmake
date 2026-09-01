# ==============================================================================
# Packaging Configuration for VLC-Whisper (CPack & NSIS)
# ==============================================================================
# ------------------------------------------------------------------------------
# Clean-checkout model provisioning.
# models/*.bin are gitignored; the NSIS installer requires ggml-tiny.bin.
# The model is fetched ONLY when the 'installer' (or 'provision_models') target
# is built and the file is absent — plain configure/build never touch network,
# keeping offline builds offline. Integrity pinned to the sha256 recorded in
# models/manifest.json (mismatch aborts the download).
# ------------------------------------------------------------------------------
set(VW_MODEL_TINY "${CMAKE_CURRENT_SOURCE_DIR}/models/ggml-tiny.bin")
set(VW_MODEL_TINY_URL "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.bin")
set(VW_MODEL_TINY_SHA256 "be07e048e1e599ad46341c8d2a135645097a538221678b7acdd1b1919c6e1b21")

if(NOT EXISTS "${VW_MODEL_TINY}")
  message(WARNING
    "VW: models/ggml-tiny.bin is absent. Building the 'installer' target will "
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

    # Zero-network default: the model MUST be provisioned locally (manual
    # placement or `--target provision_models` run once while online).
    # Downloads happen only when -DVW_PROVISION_MODELS=ON is set explicitly.
    option(VW_PROVISION_MODELS "Allow build-time model download (installer/provision targets)" OFF)

    add_custom_target(provision_models
      COMMAND ${CMAKE_COMMAND}
              -DMODEL_PATH=${VW_MODEL_TINY}
              -DMODEL_URL=${VW_MODEL_TINY_URL}
              -DMODEL_SHA256=${VW_MODEL_TINY_SHA256}
              -DALLOW_DOWNLOAD=${VW_PROVISION_MODELS}
              -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/vw_provision_model.cmake
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Provisioning models/ggml-tiny.bin (skipped when present)..."
    )

    # GPU package must bundle CPU fallback for Vulkan loader-less systems.
    # Validation is build-time fatal: installer never trusts stale destination files.
    set(VW_WORKER_GPU "${CMAKE_BINARY_DIR}/worker/vlc-whisper-worker.exe")
    set(VW_WORKER_CPU "${CMAKE_BINARY_DIR}/worker/vlc-whisper-worker-cpu.exe")
    set(VW_PLUGIN_DLL "${CMAKE_BINARY_DIR}/plugin/libvlc_whisper_plugin.dll")
    # VW_REQUIRE_CPU_FALLBACK: when the GPU preset (GGML_VULKAN=ON) is built,
    # both workers are required. Pure CPU preset only requires the CPU worker.
    set(VW_REQUIRE_CPU_FALLBACK OFF)
    if(GGML_VULKAN)
      set(VW_REQUIRE_CPU_FALLBACK ON)
    endif()
    set(VW_CPU_FALLBACK_TARGET "")
    if(GGML_VULKAN)
      # Build the CPU worker in an isolated sub-build, then copy it beside the GPU worker before
      # validation. This makes the GPU installer self-contained instead of relying on a manually
      # copied artifact from a different preset.
      set(VW_CPU_FALLBACK_BUILD_DIR "${CMAKE_BINARY_DIR}/cpu-fallback")
      set(_vw_cpu_configure_args
        -S "${CMAKE_SOURCE_DIR}"
        -B "${VW_CPU_FALLBACK_BUILD_DIR}"
        -G "${CMAKE_GENERATOR}"
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DBUILD_TESTING=OFF
        -DVW_WITH_VULKAN=OFF
      )
      if(CMAKE_TOOLCHAIN_FILE)
        list(APPEND _vw_cpu_configure_args -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
      endif()
      add_custom_target(vw_cpu_worker_fallback
        COMMAND ${CMAKE_COMMAND} ${_vw_cpu_configure_args}
        COMMAND ${CMAKE_COMMAND} --build "${VW_CPU_FALLBACK_BUILD_DIR}" --target vlc-whisper-worker
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${VW_CPU_FALLBACK_BUILD_DIR}/worker/vlc-whisper-worker-cpu.exe" "${VW_WORKER_CPU}"
        BYPRODUCTS "${VW_WORKER_CPU}"
        COMMENT "Building and staging the Windows CPU fallback worker"
        VERBATIM
      )
      set(VW_CPU_FALLBACK_TARGET vw_cpu_worker_fallback)
    endif()


    add_custom_target(installer
      COMMAND ${CMAKE_COMMAND}
              -DMODEL_PATH=${VW_MODEL_TINY}
              -DMODEL_URL=${VW_MODEL_TINY_URL}
              -DMODEL_SHA256=${VW_MODEL_TINY_SHA256}
              -DALLOW_DOWNLOAD=${VW_PROVISION_MODELS}
              -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/vw_provision_model.cmake
      COMMAND ${CMAKE_COMMAND}
              -DWORKER_DIR=${CMAKE_BINARY_DIR}/worker
              -DPLUGIN_PATH=${VW_PLUGIN_DLL}
              -DREQUIRED_MODELS=${VW_MODEL_TINY}
              -DVW_REQUIRE_CPU_FALLBACK=${VW_REQUIRE_CPU_FALLBACK}
              -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/vw_check_workers.cmake
      COMMAND ${MAKENSIS_EXECUTABLE} ${NSIS_SCRIPT_OUT}
      DEPENDS vlc_whisper_plugin vlc-whisper-worker ${VW_CPU_FALLBACK_TARGET}
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
      COMMENT "Compiling standalone Windows setup installer with NSIS (validated GPU+CPU workers)..."
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
  # When GPU preset is active, also bundle the CPU fallback if it was produced via the CPU preset copy.
  if(GGML_VULKAN)
    install(FILES "${CMAKE_BINARY_DIR}/worker/vlc-whisper-worker-cpu.exe" DESTINATION . OPTIONAL)
  endif()
  install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/models"
    DESTINATION .
    FILES_MATCHING PATTERN "*.bin" PATTERN "*.json"
  )
  install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/lua"
    DESTINATION .
  )
  install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
    DESTINATION .
  )

  include(CPack)
endif()
