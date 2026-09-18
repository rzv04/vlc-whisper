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
    launcher = text("lua/extensions/vlc_whisper_settings.lua")
    win_installer = text("cmake/vw_installer.nsi.in")
    linux_packaging = text("cmake/vw_packaging_linux.cmake")

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
    assert "model-command" in settings_cpp
    assert "QNetwork" not in settings_cpp
    assert "WinHttp" not in settings_cpp and "winhttp" not in settings_cpp.lower()

    assert "vlc-whisper-settings" in settings_cmake
    assert "spike" not in settings_cmake.lower()
    assert not (ROOT / "spikes/qt-settings").exists(), "production branch must remove the Qt spike"

    lowered = launcher.lower()
    for forbidden in ("while ", "os.clock", "dlg:update", "sleep("):
        assert forbidden not in lowered, f"blocking launcher primitive remains: {forbidden}"
    assert launcher.count("vlc.dialog(") == 1, "Lua may only use a one-shot launch-error dialog"
    assert "Engine:" not in launcher and "Translation (to):" not in launcher
    assert "vlc-whisper-settings.exe" in launcher
    assert "/usr/bin/vlc-whisper-settings" in launcher
    assert "try reinstalling vlc-whisper" in lowered

    assert "vlc-whisper-settings" in win_installer
    assert "vlc-whisper\\settings.json" in win_installer
    assert "vlc-whisper-settings" in linux_packaging


if __name__ == "__main__":
    main()
