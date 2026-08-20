@echo off
setlocal

set "SRC_URL=https://huggingface.co/ggml-org/whisper-vad/resolve/main/ggml-silero-v5.1.2.bin"
set "SCRIPT_DIR=%~dp0"
set "TARGET_FILE=%SCRIPT_DIR%ggml-silero-vad.bin"

if exist "%TARGET_FILE%" (
    echo Silero VAD model already exists at: %TARGET_FILE%
    exit /b 0
)

echo Downloading Silero VAD model from %SRC_URL% ...
curl -L --progress-bar "%SRC_URL%" -o "%TARGET_FILE%"
if %ERRORLEVEL% neq 0 (
    echo Error: Failed to download Silero VAD model. >&2
    exit /b 1
)

echo Saved Silero VAD model to %TARGET_FILE%
