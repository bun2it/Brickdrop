#!/bin/bash
# BrickDrop cross build cho TrimUI Brick Pro (aarch64-linux-gnu.2.33).
# Dùng zig + sysroot mượn từ RomCloud (RC_SYSROOT) vì cùng SDL2/curl/ABI.
set -e
cd "$(dirname "$0")"

RC_SYSROOT="${RC_SYSROOT:-/Users/tai/RomCloud/sysroot}"
ZIG="${ZIG:-}"
if [ -z "$ZIG" ]; then
  if command -v zig &>/dev/null; then ZIG="zig";
  elif [ -f "/Users/tai/.gemini/antigravity-ide/brain/00d3b56c-d55c-4262-81a8-0cf5fe35825f/tools/zig-macos-aarch64-0.13.0/zig" ]; then
    ZIG="/Users/tai/.gemini/antigravity-ide/brain/00d3b56c-d55c-4262-81a8-0cf5fe35825f/tools/zig-macos-aarch64-0.13.0/zig";
  else echo "ERROR: zig not found" >&2; exit 1;
  fi
fi
echo "=== BrickDrop cross build (aarch64) sysroot=$RC_SYSROOT ==="
mkdir -p bin
GIT_SHA="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
VER="$(grep '"version"' version.json 2>/dev/null | head -n 1 | awk -F'"' '{print $4}')"
[ -z "$VER" ] && VER="0.0.0-dev"
$ZIG c++ -target aarch64-linux-gnu.2.33 -std=c++17 -O2 -Wall \
  -DGIT_COMMIT_HASH=\"$GIT_SHA\" \
  -DBRICKDROP_VERSION=\"$VER\" \
  -Isrc -I"$RC_SYSROOT/include" -I"$RC_SYSROOT/include/SDL2" \
  src/main.cpp \
  src/core/Logger.cpp \
  src/core/Config.cpp \
  src/core/DeviceIdentity.cpp \
  src/input/Input.cpp \
  src/explorer/DirLister.cpp \
  src/net/WifiDirectManager.cpp \
  src/net/BleLinkManager.cpp \
  src/net/BleLinkFlow.cpp \
  src/ota/UpdateManager.cpp \
  src/localsend/LocalSendManager.cpp \
  src/ui/App.cpp \
  src/ui/IconCache.cpp \
  -L"$RC_SYSROOT/lib" \
  -lSDL2 -lSDL2_ttf -lSDL2_image -lcurl -lssl -lcrypto -lpthread -ldl -lm \
  -o bin/brickdrop
ls -lh bin/brickdrop
file bin/brickdrop
