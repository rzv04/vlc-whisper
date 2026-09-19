if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.20)
    project(vlc_whisper_settings VERSION 0.1.0 LANGUAGES CXX)
    include(CTest)
endif()

set(VW_SETTINGS_DEPLOY_DIR "" CACHE PATH "Pre-deployed Windows VLC-Whisper Settings runtime directory")

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

if(BUILD_TESTING AND NOT WIN32)
    add_test(NAME test_settings_headless COMMAND $<TARGET_FILE:vlc-whisper-settings> --smoke-test)
    set_tests_properties(test_settings_headless PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/test-settings-config;XDG_DATA_HOME=${CMAKE_BINARY_DIR}/test-settings-data")
endif()

if(WIN32)
    set_target_properties(vlc-whisper-settings PROPERTIES WIN32_EXECUTABLE TRUE)
    if(NOT VW_SETTINGS_DEPLOY_DIR)
        set(VW_SETTINGS_DEPLOY_DIR "${CMAKE_BINARY_DIR}/settings-deploy" CACHE PATH
            "Pre-deployed Windows VLC-Whisper Settings runtime directory" FORCE)
    endif()
    find_program(VW_WINDEPLOYQT_EXECUTABLE NAMES windeployqt windeployqt6)
    if(VW_WINDEPLOYQT_EXECUTABLE)
        add_custom_target(vw_settings_deploy
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${VW_SETTINGS_DEPLOY_DIR}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${VW_SETTINGS_DEPLOY_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:vlc-whisper-settings>"
                    "${VW_SETTINGS_DEPLOY_DIR}/vlc-whisper-settings.exe"
            COMMAND ${VW_WINDEPLOYQT_EXECUTABLE} --release --no-translations
                    "${VW_SETTINGS_DEPLOY_DIR}/vlc-whisper-settings.exe"
            DEPENDS vlc-whisper-settings
            COMMENT "Deploying Qt runtime for VLC-Whisper Settings"
            VERBATIM)
    else()
        message(STATUS "windeployqt not found: build the app normally, then provide VW_SETTINGS_DEPLOY_DIR to packaging")
    endif()
endif()
