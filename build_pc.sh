#!/bin/bash
# BrickDrop PC build (macOS/Linux): để test UI + logic trước khi cross-compile.
set -e
cd "$(dirname "$0")"
mkdir -p build_pc

CXX="${CXX:-c++}"
SDL_FLAGS="$(sdl2-config --cflags --libs 2>/dev/null || echo '-I/opt/homebrew/include -L/opt/homebrew/lib -lSDL2')"
$CXX -std=c++17 -O1 -g -Wall \
  $SDL_FLAGS \
  -I/opt/homebrew/include \
  src/main.cpp \
  src/core/Logger.cpp \
  src/core/Config.cpp \
  src/input/Input.cpp \
  src/explorer/DirLister.cpp \
  src/net/WifiDirectManager.cpp \
  src/localsend/LocalSendManager.cpp \
  src/ui/App.cpp \
  -lSDL2_ttf -lcurl -lpthread \
  -o build_pc/brickdrop

echo "=== PC build OK: build_pc/brickdrop ==="
ls -lh build_pc/brickdrop
