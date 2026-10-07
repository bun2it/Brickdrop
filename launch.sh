#!/bin/sh
# BrickDrop launcher cho TrimUI Brick Pro (theo chuẩn App: launch.sh + config.json).
cd "$(dirname "$0")"
export LD_LIBRARY_PATH="$(dirname "$0")/lib:/mnt/SDCARD/System/lib:/usr/lib:$LD_LIBRARY_PATH"
exec ./bin/brickdrop
