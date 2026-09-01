@echo off
setlocal

set "SRC_URL=https://huggingface.co/ggml-org/whisper-vad/resolve/main/ggml-silero-v5.1.2.bin"
set "EXPECTED_SHA256=29940d98d42b91fbd05ce489f3ecf7c72f0a42f027e4875919a28fb4c04ea2cf"
set "SCRIPT_DIR=%~dp0"
set "TARGET_FILE=%SCRIPT_DIR%ggml-silero-vad.bin"
set "PART_FILE=%TARGET_FILE%.part"

if exist "%TARGET_FILE%" goto verify_existing

:download
echo Downloading Silero VAD model from %SRC_URL% ...
curl -L --progress-bar "%SRC_URL%" -o "%PART_FILE%"
if %ERRORLEVEL% neq 0 (
    echo Error: Failed to download Silero VAD model. >&2
    del /f /q "%PART_FILE%" 2>nul
    exit /b 1
)

call :verify_hash "%PART_FILE%"
if errorlevel 1 (
    echo Error: VAD model SHA-256 mismatch. expected %EXPECTED_SHA256%, got %ACTUAL_SHA256% >&2
    del /f /q "%PART_FILE%" 2>nul
    exit /b 1
)

move /y "%PART_FILE%" "%TARGET_FILE%" >nul
if %ERRORLEVEL% neq 0 (
    echo Error: Failed to promote VAD model part file. >&2
    del /f /q "%PART_FILE%" 2>nul
    exit /b 1
)

echo Saved Silero VAD model to %TARGET_FILE% (SHA-256 verified)
exit /b 0

:verify_existing
call :verify_hash "%TARGET_FILE%"
if errorlevel 1 (
    echo Error: existing VAD model SHA-256 mismatch at %TARGET_FILE%. >&2
    exit /b 1
)
echo Silero VAD model already exists at: %TARGET_FILE% (SHA-256 verified)
exit /b 0

:verify_hash
set "ACTUAL_SHA256="
for /f "tokens=*" %%a in ('certUtil -hashfile "%~1" SHA256 ^| findstr /r /v /c:"hash of" /c:"CertUtil"') do set "ACTUAL_SHA256=%%a"
set "ACTUAL_SHA256=%ACTUAL_SHA256: =%"
if /I "%ACTUAL_SHA256%"=="%EXPECTED_SHA256%" exit /b 0
exit /b 1
