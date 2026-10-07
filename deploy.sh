#!/bin/sh
# Deploy BrickDrop lên TrimUI Brick Pro qua adb vào /mnt/SDCARD/Apps/BrickDrop.
set -e
cd "$(dirname "$0")"
APP=/mnt/SDCARD/Apps/BrickDrop
adb shell "mkdir -p $APP/bin $APP/assets/fonts"
adb push bin/brickdrop $APP/bin/brickdrop
adb push assets/fonts/NotoSans-Regular.ttf $APP/assets/fonts/NotoSans-Regular.ttf
adb push launch.sh $APP/launch.sh
adb push config.json $APP/config.json
if [ -f icon.png ]; then
  adb push icon.png $APP/icon.png
fi
adb shell "chmod +x $APP/bin/brickdrop $APP/launch.sh && sync"
echo "=== Deployed to $APP ==="
