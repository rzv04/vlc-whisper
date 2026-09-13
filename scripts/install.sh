#!/bin/sh
set -eu

REPO="rzv04/vlc-whisper"
MIN_VLC="3.0.23"
BASE="https://github.com/${REPO}/releases/latest/download"

die() {
  echo "vlc-whisper: $*" >&2
  exit 1
}

if [ "${1:-}" = "--uninstall" ]; then
  if [ "$(id -u)" = "0" ]; then
    apt-get remove -y vlc-whisper
  else
    command -v sudo >/dev/null 2>&1 || die "sudo is required to uninstall"
    sudo apt-get remove -y vlc-whisper
  fi
  exit 0
fi

[ "${1:-}" = "" ] || die "usage: install.sh [--uninstall]"
[ "$(uname -m)" = "x86_64" ] || die "this installer currently supports Linux x86_64 only"
[ -r /etc/os-release ] || die "cannot identify this Linux distribution"
. /etc/os-release
[ "${ID:-}" = "ubuntu" ] || die "this installer currently supports Ubuntu only"

case "${VERSION_ID:-}" in
  24.04)
    PACKAGE="vlc-whisper-ubuntu-24.04-amd64.deb"
    ;;
  26.04)
    PACKAGE="vlc-whisper-ubuntu-26.04-amd64.deb"
    ;;
  *)
    PACKAGE="vlc-whisper-ubuntu-${VERSION_ID:-unknown}-amd64.deb"
    ;;
esac
CHECKSUM="${PACKAGE}.sha256"

for cmd in apt-get apt-cache dpkg dpkg-query curl sha256sum awk grep dirname mktemp; do
  command -v "$cmd" >/dev/null 2>&1 || die "required command not found: $cmd"
done
if [ "$(id -u)" != "0" ]; then
  command -v sudo >/dev/null 2>&1 || die "sudo is required to install"
fi

installed="$(dpkg-query -W -f='${Version}' vlc 2>/dev/null || true)"
candidate="$(apt-cache policy vlc 2>/dev/null | awk '/Candidate:/ { print $2; exit }')"
[ -n "$candidate" ] && [ "$candidate" != "(none)" ] || die "Ubuntu APT has no VLC candidate"

if [ -z "$installed" ]; then
  if command -v snap >/dev/null 2>&1 && snap list vlc >/dev/null 2>&1; then
    echo "vlc-whisper: Snap VLC detected; the supported APT VLC will be installed alongside it." >&2
  fi
  if command -v flatpak >/dev/null 2>&1 && flatpak info org.videolan.VLC >/dev/null 2>&1; then
    echo "vlc-whisper: Flatpak VLC detected; the supported APT VLC will be installed alongside it." >&2
  fi
  dpkg --compare-versions "$candidate" ge "$MIN_VLC" || \
    die "VLC >= $MIN_VLC is required; VLC is not installed and APT candidate is $candidate"
else
  dpkg --compare-versions "$installed" ge "$MIN_VLC" || dpkg --compare-versions "$candidate" ge "$MIN_VLC" || \
    die "VLC >= $MIN_VLC is required; installed VLC is $installed and APT candidate is $candidate"
  if ! dpkg --compare-versions "$installed" ge "$MIN_VLC"; then
    echo "vlc-whisper: upgrading VLC $installed to a supported version." >&2
  fi
fi

if [ "$(id -u)" = "0" ]; then
  apt-get update
else
  sudo apt-get update
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

if ! curl -fsIL "$BASE/$PACKAGE" >/dev/null 2>&1; then
  if curl -fsIL "$BASE/vlc-whisper-linux-amd64.deb" >/dev/null 2>&1; then
    PACKAGE="vlc-whisper-linux-amd64.deb"
    CHECKSUM="vlc-whisper-linux-amd64.deb.sha256"
  else
    die "release package $PACKAGE not found for Ubuntu ${VERSION_ID:-unknown} at $BASE"
  fi
fi

curl -fsSL "$BASE/$PACKAGE" -o "$tmp/$PACKAGE"
curl -fsSL "$BASE/$CHECKSUM" -o "$tmp/$CHECKSUM"
expected="$(grep -Eo '[[:xdigit:]]{64}' "$tmp/$CHECKSUM" | head -n 1)"
[ -n "$expected" ] || die "release checksum is invalid"
actual="$(sha256sum "$tmp/$PACKAGE" | awk '{ print $1 }')"
[ "$actual" = "$expected" ] || die "release checksum mismatch"

if [ "$(id -u)" = "0" ]; then
  apt-get install -y "$tmp/$PACKAGE"
else
  sudo apt-get install -y "$tmp/$PACKAGE"
fi

installed="$(dpkg-query -W -f='${Version}' vlc 2>/dev/null || true)"
[ -n "$installed" ] && dpkg --compare-versions "$installed" ge "$MIN_VLC" || \
  die "installed VLC does not satisfy >= $MIN_VLC"

plugin_file="$(dpkg-query -L vlc-plugin-base 2>/dev/null | awk '/\/vlc\/plugins\/.*_plugin\.so$/ { print; exit }')"
[ -n "$plugin_file" ] || die "could not discover the APT VLC plugin directory"
plugin_root="$(dirname "$(dirname "$plugin_file")")"
vlc_root="$(dirname "$plugin_root")"
lua_probe="$(dpkg-query -L vlc-plugin-base 2>/dev/null | awk '/\/vlc\/lua\/extensions\/.*\.lua(c)?$/ { print; exit }')"
if [ -n "$lua_probe" ]; then
  lua_root="$(dirname "$lua_probe")"
else
  lua_root="$vlc_root/lua/extensions"
fi

[ -f "$plugin_root/audio_filter/libvlc_whisper_plugin.so" ] || die "plugin was not installed into the detected VLC tree"
[ -x "$vlc_root/vlc-whisper-worker" ] || die "worker was not installed beside the detected VLC tree"
[ -f "$lua_root/vlc_whisper_settings.lua" ] || die "Lua settings extension was not installed into the detected VLC Lua tree"
[ -f "$vlc_root/models/ggml-tiny.bin" ] || die "bundled Whisper model was not installed"

echo "vlc-whisper: installed for VLC $installed"
echo "vlc-whisper: plugin: $plugin_root/audio_filter/libvlc_whisper_plugin.so"
echo "vlc-whisper: settings: $lua_root/vlc_whisper_settings.lua"
