# BrickDrop — AirDrop-style offline file sharing for TrimUI Brick Pro

App độc lập tách từ LocalSend P2P của RomCloud. Mục tiêu: **2 Brick gặp nhau
ngoài đường, bật chế độ lên là send — không cần router, không cần internet.**

Luồng định hướng (như AirDrop):
1. **Nhóm offline**: 1 máy phát WiFi (`hostapd wlan1`), máy kia tham gia bằng
   PSK cố định (không cần gõ phím).
2. **LocalSend**: truyền file qua protocol LocalSend v2 tương thích RomCloud
   và app LocalSend gốc.
3. **BLE handshake** (phase sau): phát hiện máy gần + trao đổi tự động.

## Trạng thái (2026-10-07)

* [x] P0: app SDL 1024x768 chạy trên Brick Pro, selftest PASS
  (`brickdrop --selftest`: Config, DirLister, LocalSend `/info`, `iw list` AP/P2P).
* [x] Explorer chọn file gửi + **chọn thư mục nhận** (port logic từ RomCloud
  `FileExplorer`/`FileSystemManager`, bỏ nhập liệu: tạo thư mục auto-tên).
* [x] Gửi/nhận LocalSend + modal duyệt A/B + màn tiến trình.
* [x] Offline: phát nhóm / quét / tham gia / rời (PSK cố định `brickdrop1`).
* [ ] Kiểm thử truyền file Brick↔Brick thực tế + ảnh chụp màn hình.
* [ ] BLE handshake.

## Điều khiển (tay cầm Brick, layout Nintendo)

| Phím | Chức năng chung |
|---|---|
| D-pad | Di chuyển |
| A (phải) | Chọn / vào thư mục / duyệt nhận |
| B (dưới) | Lên thư mục / về / từ chối |
| Y (trái) | Quét máy / chốt thư mục nhận / tên mới / dọn xong |
| X (trên) | Tạo thư mục (màn thư mục nhận) |
| MENU | Thoát app (màn chính) |

Trên PC test bằng bàn phím: mũi tên + Enter (A) + Esc (B) + X/Y.

## Build & deploy

```sh
./build_pc.sh   # test trên Mac/Linux (cần SDL2 + SDL2_ttf + curl)
./build.sh      # cross aarch64 (cần zig + RC_SYSROOT=/Users/tai/RomCloud/sysroot)
./deploy.sh     # adb push vào /mnt/SDCARD/Apps/BrickDrop
```

Trên máy: `brickdrop --selftest` để kiểm tra không cần màn hình.

## Cấu trúc

* `src/localsend/` — protocol + discovery + transfer (gốc RomCloud `2ce4be2`,
  đã tách dep: xem `docs/ORIGIN.md`).
* `src/explorer/DirLister.*` — duyệt thư mục POSIX (logic từ RomCloud).
* `src/net/WifiDirectManager.*` — phát/tham gia nhóm offline.
* `src/input/` — gamepad + bàn phím (mapping từ RomCloud `InputManager`).
* `src/ui/` — các màn HOME / gửi / nhận / offline / tiến trình.
* `src/core/` — Logger + Config (alias, fingerprint, thư mục nhận).

Phần cứng đã xác nhận: xem README cũ và `docs/ORIGIN.md`.
Chi tiết chip/driver (XR829, `iw list` AP/P2P-GO, BT combo): đã audit qua adb.
