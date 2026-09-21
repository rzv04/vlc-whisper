#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def text(path: str) -> str:
    p = ROOT / path
    assert p.is_file(), f"missing production file: {path}"
    return p.read_text(encoding="utf-8")


def main() -> None:
    settings_cmake = text("settings/CMakeLists.txt")
    settings_cpp = text("settings/src/vw_qt_settings_main.cpp")
    plugin_cmake = text("plugin/CMakeLists.txt")
    launcher_bridge = text("plugin/src/vw_settings_launcher_module.c")
    settings_bridge = text("plugin/src/vw_settings_file.c")
    config_bridge = text("plugin/include/vw_settings_config_override.h")
    launcher = text("lua/extensions/vlc_whisper_settings.lua")
    win_installer = text("cmake/vw_installer.nsi.in")
    linux_packaging = text("cmake/vw_packaging_linux.cmake")
    linux_postinst = text("cmake/debian/postinst")
    linux_installer = text("scripts/install.sh")
    readme = text("README.md")

    for key in (
        "whisper-backend",
        "model-path",
        "whisper-language",
        "whisper-threads",
        "whisper-logging",
        "whisper-show-paused",
        "whisper-translate-enabled",
        "whisper-translate-from",
        "whisper-translate-to",
        "whisper-translate-mode",
    ):
        assert key in settings_cpp, key

    assert "QSaveFile" in settings_cpp
    assert "LOCALAPPDATA" in settings_cpp
    assert "XDG_CONFIG_HOME" in settings_cpp
    assert ".config/vlc-whisper" in settings_cpp
    assert "model-command" in settings_bridge  # Legacy playback command consumer remains supported.
    assert "--settings-download" in settings_cpp  # Qt now owns an independent worker child.
    assert "model-path-download-base" in settings_cpp and "model-path-download-base" in settings_bridge
    assert "QNetwork" not in settings_cpp
    assert "WinHttp" not in settings_cpp and "winhttp" not in settings_cpp.lower()
    assert "curl" not in settings_cpp.lower(), "settings GUI must not own HTTP/model downloads"
    assert "QTimer" in settings_cpp, "runtime status refresh must remain bounded inside the Qt process"
    assert "downloading" in settings_cpp and "verifying" in settings_cpp

    request_download = settings_cpp.split("void vw_request_download()", 1)[1].split("void vw_request_abort()", 1)[0]
    assert "vw_active_model_path_for_download()" in request_download
    assert "QFileInfo::exists(vw_model_download_base_path_)" not in request_download, (
        "each new download request must refresh its rollback marker"
    )
    assert "vw_write_small_file(vw_model_download_base_path_, marker)" in request_download
    assert "if (!preserve_model_download_base && !vw_download_pending_)" in settings_cpp, (
        "Apply must not discard rollback state while a model request is pending"
    )

    assert "vlc-whisper-settings" in settings_cmake
    assert "--smoke-test" in settings_cpp and "QT_QPA_PLATFORM=offscreen" in settings_cmake
    assert "QProcess::startDetached" in settings_cpp
    assert "--launch-detached" in settings_cpp
    assert "src/vw_settings_launcher_module.c" in plugin_cmake
    assert "vw_settings_launcher_module_override.h" in plugin_cmake
    assert "add_library(vlc_whisper_settings_launcher" not in plugin_cmake
    assert "--launch-detached" in launcher_bridge
    assert "CreateProcessW" in launcher_bridge
    assert "WNOHANG" in launcher_bridge, "POSIX bootstrap acknowledgement must have a deadline"
    assert "TerminateProcess" in launcher_bridge, "timed-out Windows bootstrap must not launch settings later"
    for forbidden in ("system(", "ShellExecute", "cmd.exe", "powershell", "start \"\""):
        assert forbidden.lower() not in launcher_bridge.lower(), f"native bridge uses shell launcher: {forbidden}"
    assert "spike" not in settings_cmake.lower()
    assert not (ROOT / "spikes/qt-settings").exists(), "production branch must remove the Qt spike"

    lowered = launcher.lower()
    for forbidden in ("while ", "os.clock", "dlg:update", "sleep(", "wait(", "poll(", "os.execute", "io.popen"):
        assert forbidden not in lowered, f"blocking/shell launcher primitive remains: {forbidden}"
    assert 'vlc.stream("vlc-whisper-settings://launch")' in launcher
    assert launcher.count("vlc.dialog(") == 1, "Lua may only use a one-shot launch-error dialog"
    assert "error_dlg:hide()" in launcher, "repeated launch failures must not stack dialogs"
    assert "Engine:" not in launcher and "Translation (to):" not in launcher
    assert "try reinstalling vlc-whisper" in lowered

    assert "vw_json_document_valid" in settings_bridge
    assert "vw_json_key_equals" in settings_bridge
    assert "vw_settings_ack_model_command" in settings_bridge
    assert "vw_settings_ack_model_command" in config_bridge
    assert "vw_settings_last_model_command" in config_bridge
    assert "translate-enabled-effective" in settings_bridge and "translate-enabled-effective" in settings_cpp

    assert "vlc-whisper-settings" in win_installer
    assert "settings.json" in win_installer and "reset-settings" in win_installer
    assert 'ClearErrors\n  FileOpen $0 "$INSTALL_USER_APPDATA\\reset-settings" w' in win_installer
    assert "IsOwnedSettingsProcessRunning" in win_installer
    assert 'RMDir /r /REBOOTOK "$INSTDIR\\vlc-whisper-settings"' in win_installer
    assert "vlc-whisper-settings" in linux_packaging
    assert "libqt6widgets6" in linux_packaging and "libqt6network6" in linux_packaging
    assert "vw_require_settings_for_package" in linux_packaging
    for runtime_file in ("Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "Qt6Network.dll", "platforms/qwindows.dll"):
        assert runtime_file in text("cmake/vw_check_workers.cmake"), runtime_file
    assert "runuser" in linux_postinst and "runuser" in linux_installer
    assert "|| true" not in linux_postinst, "post-install reset failures must fail the package"
    assert "settings.json" in linux_installer and "reset-settings" in linux_installer
    install_position = linux_installer.index('apt-get install -y "$tmp/$PACKAGE"')
    reset_position = linux_installer.index('rm -f "$settings_dir/settings.json"')
    assert install_position < reset_position, "settings must only reset after installation succeeds"
    assert 'subgraph SETTINGS["Standalone Settings Process"]' in readme


if __name__ == "__main__":
    main()
