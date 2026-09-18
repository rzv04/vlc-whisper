find_package(Qt6 6.2 COMPONENTS Widgets Network QUIET)
if(NOT Qt6_FOUND)
    message(STATUS "Qt6 Widgets/Network not found: standalone settings target is unavailable")
    return()
endif()

add_executable(vlc-whisper-settings src/vw_qt_settings_main.cpp)
target_link_libraries(vlc-whisper-settings PRIVATE Qt6::Widgets Qt6::Network)
target_compile_features(vlc-whisper-settings PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(vlc-whisper-settings PRIVATE /W4 /permissive-)
else()
    target_compile_options(vlc-whisper-settings PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    set_target_properties(vlc-whisper-settings PROPERTIES WIN32_EXECUTABLE TRUE)
    set(VW_SETTINGS_DEPLOY_DIR "${CMAKE_BINARY_DIR}/settings-deploy" CACHE INTERNAL "Qt settings deployment directory")
    find_program(VW_WINDEPLOYQT_EXECUTABLE NAMES windeployqt windeployqt6)
    if(VW_WINDEPLOYQT_EXECUTABLE)
        add_custom_target(vw_settings_deploy
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${VW_SETTINGS_DEPLOY_DIR}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${VW_SETTINGS_DEPLOY_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:vlc-whisper-settings>"
                    "${VW_SETTINGS_DEPLOY_DIR}/vlc-whisper-settings.exe"
            COMMAND ${VW_WINDEPLOYQT_EXECUTABLE} --no-translations --no-system-d3d-compiler
                    "${VW_SETTINGS_DEPLOY_DIR}/vlc-whisper-settings.exe"
            DEPENDS vlc-whisper-settings
            COMMENT "Deploying Qt runtime for VLC-Whisper Settings"
            VERBATIM)
    else()
        message(STATUS "windeployqt not found: Windows installer will fail closed if settings deployment is requested")
    endif()
endif()
