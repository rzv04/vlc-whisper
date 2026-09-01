# ==============================================================================
# Packaging Configuration for VLC-Whisper (CPack & NSIS)
# ==============================================================================
# ------------------------------------------------------------------------------
# Clean-checkout model provisioning.
# Model binaries are gitignored, so release packaging must never trust whichever
# local files happen to exist. The installer/portable package explicitly requires
# the pinned multilingual tiny model and Silero VAD model and verifies SHA-256 for
# both before packaging. Downloads occur only when explicitly enabled.
# ------------------------------------------------------------------------------
set(VW_MODEL_TINY "${CMAKE_CURRENT_SOURCE_DIR}/models/ggml-tiny.bin")
set(VW_MODEL_TINY_URL "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.bin")
set(VW_MODEL_TINY_SHA256 "be07e048e1e599ad46341c8d2a135645097a538221678b7acdd1b1919c6e1b21")
set(VW_MODEL_VAD "${CMAKE_CURRENT_SOURCE_DIR}/models/ggml-silero-vad.bin")
set(VW_MODEL_VAD_URL "https://huggingface.co/ggml-org/whisper-vad/resolve/main/ggml-silero-v5.1.2.bin")
set(VW_MODEL_VAD_SHA256 "29940d98d42b91fbd05ce489f3ecf7c72f0a42f027e4875919a28fb4c04ea2cf")

foreach(_vw_required_model IN ITEMS "${VW_MODEL_TINY}" "${VW_MODEL_VAD}")
  if(NOT EXISTS "${_vw_required_model}")
    message(WARNING
      "VW: required release model '${_vw_required_model}' is absent. Building the installer/provision_models target "
      "will verify an existing file or fetch it only when -DVW_PROVISION_MODELS=ON is set.")
  endif()
endforeach()

if(WIN32)
  find_program(MAKENSIS_EXECUTABLE makensis)

  if(MAKENSIS_EXECUTABLE)
    message(STATUS "Found NSIS compiler: ${MAKENSIS_EXECUTABLE}")

    if(GGML_VULKAN)
      set(VW_PACKAGE_IS_GPU 1)
    else()
      set(VW_PACKAGE_IS_GPU 0)
    endif()

    # Configure the NSIS template.
    set(NSIS_SCRIPT_IN "${CMAKE_CURRENT_SOURCE_DIR}/cmake/vw_installer.nsi.in")
    set(NSIS_SCRIPT_OUT "${CMAKE_CURRENT_BINARY_DIR}/vw_installer.nsi")
    set(VW_STAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/installer-stage")
    configure_file(${NSIS_SCRIPT_IN} ${NSIS_SCRIPT_OUT} @ONLY)

    # Zero-network default: both release models MUST already be provisioned unless
    # the maintainer explicitly enables one-time pinned downloads.
    option(VW_PROVISION_MODELS "Allow build-time model download (installer/provision targets)" OFF)

    add_custom_target(provision_models
      COMMAND ${CMAKE_COMMAND}
              -DMODEL_PATH=${VW_MODEL_TINY}
              -DMODEL_URL=${VW_MODEL_TINY_URL}
              -DMODEL_SHA256=${VW_MODEL_TINY_SHA256}
              -DALLOW_DOWNLOAD=${VW_PROVISION_MODELS}
              -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/vw_provision_model.cmake
      COMMAND ${CMAKE_COMMAND}
              -DMODEL_PATH=${VW_MODEL_VAD}
              -DMODEL_URL=${VW_MODEL_VAD_URL}
              -DMODEL_SHA256=${VW_MODEL_VAD_SHA256}
              -DALLOW_DOWNLOAD=${VW_PROVISION_MODELS}
              -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/vw_provision_model.cmake
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Verifying/provisioning pinned Whisper tiny and Silero VAD release models..."
      VERBATIM
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
              -DMODEL_PATH=${VW_MODEL_VAD}
              -DMODEL_URL=${VW_MODEL_VAD_URL}
              -DMODEL_SHA256=${VW_MODEL_VAD_SHA256}
              -DALLOW_DOWNLOAD=${VW_PROVISION_MODELS}
              -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/vw_provision_model.cmake
      COMMAND ${CMAKE_COMMAND}
              -DWORKER_DIR=${CMAKE_BINARY_DIR}/worker
              -DPLUGIN_PATH=${VW_PLUGIN_DLL}
              -DVW_REQUIRE_CPU_FALLBACK=${VW_REQUIRE_CPU_FALLBACK}
              -DWHISPER_MODEL_PATH=${VW_MODEL_TINY}
              -DWHISPER_MODEL_SHA256=${VW_MODEL_TINY_SHA256}
              -DVAD_MODEL_PATH=${VW_MODEL_VAD}
              -DVAD_MODEL_SHA256=${VW_MODEL_VAD_SHA256}
              -DSTAGE_DIR=${VW_STAGE_DIR}
              -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
              -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/vw_check_workers.cmake
      COMMAND ${MAKENSIS_EXECUTABLE} ${NSIS_SCRIPT_OUT}
      DEPENDS vlc_whisper_plugin vlc-whisper-worker ${VW_CPU_FALLBACK_TARGET}
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
      COMMENT "Compiling standalone Windows setup installer with verified workers and models..."
      VERBATIM
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
  # When GPU preset is active, also bundle the CPU fallback staged by the installer target.
  if(GGML_VULKAN)
    install(FILES "${CMAKE_BINARY_DIR}/worker/vlc-whisper-worker-cpu.exe" DESTINATION .)
  endif()

  # Explicit allowlist: never package arbitrary gitignored models from the maintainer checkout.
  install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/models/manifest.json"
    "${VW_MODEL_TINY}"
    "${VW_MODEL_VAD}"
    DESTINATION models
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
  # CPack's `package` target is otherwise independent of the generated CPU
  # fallback, so a direct `cmake --build . --target package` could omit it.
  if(TARGET package AND TARGET vw_cpu_worker_fallback)
    add_dependencies(package vw_cpu_worker_fallback)
  endif()
endif()
