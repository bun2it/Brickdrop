#!/bin/bash
# BrickDrop PC build (macOS/Linux): để test UI + logic trước khi cross-compile.
set -e
cd "$(dirname "$0")"
mkdir -p build_pc

CXX="${CXX:-c++}"
SDL_FLAGS="$(sdl2-config --cflags --libs 2>/dev/null || echo '-I/opt/homebrew/include -L/opt/homebrew/lib -lSDL2')"
VER="$(grep '"version"' version.json 2>/dev/null | head -n 1 | awk -F'"' '{print $4}')"
[ -z "$VER" ] && VER="0.0.0-dev"
$CXX -std=c++17 -O1 -g -Wall \
  -DBRICKDROP_VERSION="$VER" \
  $SDL_FLAGS \
  -I/opt/homebrew/include \
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
  -lSDL2_ttf -lSDL2_image -lcurl -lcrypto -lpthread \
  -o build_pc/brickdrop

echo "=== PC build OK: build_pc/brickdrop ==="
ls -lh build_pc/brickdrop
