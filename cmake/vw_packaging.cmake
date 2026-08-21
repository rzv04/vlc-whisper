# ==============================================================================
# Packaging Configuration for VLC-Whisper (CPack & NSIS)
# ==============================================================================

if(WIN32)
  find_program(MAKENSIS_EXECUTABLE makensis)

  if(MAKENSIS_EXECUTABLE)
    message(STATUS "Found NSIS compiler: ${MAKENSIS_EXECUTABLE}")

    # Configure the NSIS template
    set(NSIS_SCRIPT_IN "${CMAKE_CURRENT_SOURCE_DIR}/cmake/vw_installer.nsi.in")
    set(NSIS_SCRIPT_OUT "${CMAKE_CURRENT_BINARY_DIR}/vw_installer.nsi")

    configure_file(${NSIS_SCRIPT_IN} ${NSIS_SCRIPT_OUT} @ONLY)

    # Custom target to compile the standalone NSIS installer
    add_custom_target(installer
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
