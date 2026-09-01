#!/bin/sh
set -e

# Default download URL for Silero VAD GGML model
SRC_URL="https://huggingface.co/ggml-org/whisper-vad/resolve/main/ggml-silero-v5.1.2.bin"
# Pinned SHA-256 from models/manifest.json (silero-vad)
EXPECTED_SHA256="29940d98d42b91fbd05ce489f3ecf7c72f0a42f027e4875919a28fb4c04ea2cf"

SCRIPT_DIR="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 && pwd -P)"
TARGET_FILE="${1:-$SCRIPT_DIR/ggml-silero-vad.bin}"
PART_FILE="${TARGET_FILE}.part"

hash_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | cut -d' ' -f1
    else
        return 1
    fi
}

verify_file() {
    actual_sha256=$(hash_file "$1") || return 1
    [ "$actual_sha256" = "$EXPECTED_SHA256" ]
}

if [ -f "$TARGET_FILE" ]; then
    if verify_file "$TARGET_FILE"; then
        echo "Silero VAD model already exists at: $TARGET_FILE (SHA-256 verified)"
        exit 0
    fi
    echo "Error: existing VAD model SHA-256 mismatch at $TARGET_FILE." >&2
    exit 1
fi

echo "Downloading Silero VAD model from $SRC_URL ..."
if command -v curl >/dev/null 2>&1; then
    curl -L --progress-bar "$SRC_URL" -o "$PART_FILE"
elif command -v wget >/dev/null 2>&1; then
    wget --no-config --quiet --show-progress "$SRC_URL" -O "$PART_FILE"
else
    echo "Error: curl or wget is required to download model files." >&2
    exit 1
fi

if ! verify_file "$PART_FILE"; then
    actual_sha256=$(hash_file "$PART_FILE" 2>/dev/null || printf '%s' unavailable)
    echo "Error: VAD model SHA-256 mismatch. expected $EXPECTED_SHA256, got $actual_sha256" >&2
    rm -f "$PART_FILE"
    exit 1
fi

mv -f "$PART_FILE" "$TARGET_FILE"
echo "Saved Silero VAD model to $TARGET_FILE (SHA-256 verified)"
