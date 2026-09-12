# Ubuntu/Debian x64 release packaging. Runtime architecture remains plugin -> IPC -> worker.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "VW: Linux release packaging currently supports x64 only")
endif()

set(VW_LINUX_MULTIARCH "${CMAKE_LIBRARY_ARCHITECTURE}")
if(NOT VW_LINUX_MULTIARCH)
  execute_process(
    COMMAND ${CMAKE_C_COMPILER} -print-multiarch
    OUTPUT_VARIABLE VW_LINUX_MULTIARCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
endif()
if(NOT VW_LINUX_MULTIARCH)
  message(FATAL_ERROR "VW: could not determine the Debian multiarch library directory")
endif()
if(NOT VW_LINUX_MULTIARCH MATCHES "^x86_64-")
  message(FATAL_ERROR "VW: Linux release packaging currently supports Debian amd64/x86_64 only")
endif()

set(VW_LINUX_VLC_ROOT "lib/${VW_LINUX_MULTIARCH}/vlc")
set(VW_LINUX_VLC_PLUGIN_DIR "${VW_LINUX_VLC_ROOT}/plugins/audio_filter")
set(VW_LINUX_VLC_LUA_DIR "${VW_LINUX_VLC_ROOT}/lua/extensions")
set(VW_LINUX_MODEL_DIR "share/vlc/models")

option(VW_PROVISION_MODELS "Allow build-time model download (package/provision targets)" OFF)
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
  COMMENT "Verifying/provisioning pinned Whisper tiny and Silero VAD release models..."
  VERBATIM
)

install(TARGETS vlc_whisper_plugin
  LIBRARY DESTINATION "${VW_LINUX_VLC_PLUGIN_DIR}"
)
install(TARGETS vlc-whisper-worker
  RUNTIME DESTINATION "${VW_LINUX_VLC_ROOT}"
)
install(FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/models/manifest.json"
  "${VW_MODEL_TINY}"
  "${VW_MODEL_VAD}"
  DESTINATION "${VW_LINUX_MODEL_DIR}"
)
install(FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/lua/extensions/vlc_whisper_settings.lua"
  DESTINATION "${VW_LINUX_VLC_LUA_DIR}"
)
install(FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
  "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
  "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
  DESTINATION "share/doc/vlc-whisper"
)

# Runtime discovery expects models adjacent to the worker, while the Lua extension
# uses VLC's data directory. One symlink satisfies both without duplicating models.
install(CODE "
  set(_vw_models_link \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${VW_LINUX_VLC_ROOT}/models\")
  set(_vw_models_target \"\${CMAKE_INSTALL_PREFIX}/${VW_LINUX_MODEL_DIR}\")
  if(IS_SYMLINK \"\${_vw_models_link}\")
    file(READ_SYMLINK \"\${_vw_models_link}\" _vw_existing_target)
    if(NOT _vw_existing_target STREQUAL \"\${_vw_models_target}\")
      message(FATAL_ERROR \"VW: existing models symlink points to an unexpected location\")
    endif()
  elseif(EXISTS \"\${_vw_models_link}\")
    message(FATAL_ERROR \"VW: refusing to replace existing VLC models path: \${_vw_models_link}\")
  else()
    file(CREATE_LINK \"\${_vw_models_target}\" \"\${_vw_models_link}\" SYMBOLIC)
  endif()
")

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "vlc-whisper")
set(CPACK_PACKAGE_VENDOR "VLC-Whisper Contributors")
set(CPACK_PACKAGE_CONTACT "VLC-Whisper Contributors")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Offline Whisper AI real-time caption plugin for VLC")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CPACK_PACKAGE_FILE_NAME "vlc-whisper-linux-amd64")
set(CPACK_PACKAGE_CHECKSUM "SHA256")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "VLC-Whisper Contributors")
set(CPACK_DEBIAN_PACKAGE_SECTION "video")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "vlc (>= 3.0.23), curl, ca-certificates, libvulkan1")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/debian/postinst;${CMAKE_CURRENT_SOURCE_DIR}/cmake/debian/postrm")
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)

include(CPack)
if(TARGET package)
  add_dependencies(package provision_models)
endif()
