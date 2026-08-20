#!/bin/sh
set -e

# Default download URL for Silero VAD GGML model
SRC_URL="https://huggingface.co/ggml-org/whisper-vad/resolve/main/ggml-silero-v5.1.2.bin"

SCRIPT_DIR="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 && pwd -P)"
TARGET_FILE="${1:-$SCRIPT_DIR/ggml-silero-vad.bin}"

if [ -f "$TARGET_FILE" ]; then
    echo "Silero VAD model already exists at: $TARGET_FILE"
    exit 0
fi

echo "Downloading Silero VAD model from $SRC_URL ..."
if command -v curl >/dev/null 2>&1; then
    curl -L --progress-bar "$SRC_URL" -o "$TARGET_FILE"
elif command -v wget >/dev/null 2>&1; then
    wget --no-config --quiet --show-progress "$SRC_URL" -O "$TARGET_FILE"
else
    echo "Error: curl or wget is required to download model files." >&2
    exit 1
fi

echo "Saved Silero VAD model to $TARGET_FILE"
