#!/bin/bash
# Đóng gói release BrickDrop cho TrimUI Brick Pro.
# Rule (theo RomCloud): MỖI VERSION CHỈ 1 FILE ZIP FULL DUY NHẤT
# (BrickDrop-vX.Y.Z.zip) — OTA trong app dùng chính file này.
set -e
cd "$(dirname "$0")"

VERSION=$(grep '"version"' version.json | head -n 1 | awk -F'"' '{print $4}')
if [ -z "$VERSION" ]; then
  echo "ERROR: không đọc được version từ version.json" >&2
  exit 1
fi

echo "=== Packaging BrickDrop v${VERSION} ==="

if [ ! -f bin/brickdrop ]; then
  echo "ERROR: bin/brickdrop not found. Run ./build.sh first." >&2
  exit 1
fi
# Release gate: cảnh báo nếu binary chưa strip (dev build nặng hơn).
if file bin/brickdrop | grep -q "not stripped"; then
  echo "WARNING: bin/brickdrop is NOT stripped (dev build)." >&2
  echo "WARNING: strip binary trước khi release cho nhẹ." >&2
fi

DIST_DIR="dist"
STAGING_DIR="$DIST_DIR/staging"
ZIP_NAME="BrickDrop-v${VERSION}.zip"
ROOT="$(pwd)"

rm -rf "$STAGING_DIR"
mkdir -p "$STAGING_DIR/Apps/BrickDrop/bin"
mkdir -p "$STAGING_DIR/Apps/BrickDrop/assets/fonts"
mkdir -p "$STAGING_DIR/Apps/BrickDrop/assets/icons"

cp bin/brickdrop "$STAGING_DIR/Apps/BrickDrop/bin/"
cp assets/fonts/NotoSans-Regular.ttf "$STAGING_DIR/Apps/BrickDrop/assets/fonts/"
cp assets/icons/*.png "$STAGING_DIR/Apps/BrickDrop/assets/icons/"
cp launch.sh "$STAGING_DIR/Apps/BrickDrop/launch.sh"
cp config.json "$STAGING_DIR/Apps/BrickDrop/config.json"
if [ -f icon.png ]; then
  cp icon.png "$STAGING_DIR/Apps/BrickDrop/icon.png"
fi

chmod +x "$STAGING_DIR/Apps/BrickDrop/launch.sh" \
         "$STAGING_DIR/Apps/BrickDrop/bin/brickdrop"

mkdir -p "$DIST_DIR"
rm -f "$DIST_DIR"/BrickDrop-v*.zip
cd "$STAGING_DIR"
if command -v zip &>/dev/null; then
  zip -qr "$ROOT/$DIST_DIR/$ZIP_NAME" Apps
else
  tar -a -cf "$ROOT/$DIST_DIR/$ZIP_NAME" Apps
fi
cd "$ROOT"
rm -rf "$STAGING_DIR"

SHA=$(shasum -a 256 "$DIST_DIR/$ZIP_NAME" 2>/dev/null | awk '{print $1}')
if [ -z "$SHA" ]; then
  SHA=$(sha256sum "$DIST_DIR/$ZIP_NAME" 2>/dev/null | awk '{print $1}')
fi

echo "=== Release package OK ==="
ls -lh "$DIST_DIR/$ZIP_NAME"
echo "SHA256: $SHA"
echo ""
echo "Các bước phát hành:"
echo "  1. Copy SHA256 trên vào \"sha256\" trong version.json"
 echo "  2. Tạo GitHub release v${VERSION} ở bun2it/BrickDrop, upload file zip"
 echo "  3. Commit + push version.json (manifest OTA)"
