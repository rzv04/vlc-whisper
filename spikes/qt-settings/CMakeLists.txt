cmake_minimum_required(VERSION 3.20)

project(vlc_whisper_settings_spike VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)

add_executable(vlc-whisper-settings-spike
    src/main.cpp
)

target_link_libraries(vlc-whisper-settings-spike PRIVATE Qt6::Widgets)

target_compile_features(vlc-whisper-settings-spike PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(vlc-whisper-settings-spike PRIVATE /W4 /permissive-)
else()
    target_compile_options(vlc-whisper-settings-spike PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    set_target_properties(vlc-whisper-settings-spike PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
