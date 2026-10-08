# BrickDrop — Kế hoạch phát triển

Cập nhật: 2026-10-07

## Trạng thái tổng quan

- [x] **Giai đoạn 0 — Vá lỗi blocker** (xong 2026-10-07)
- [ ] **Giai đoạn 1 — Test Brick↔Brick thực tế**
- [ ] **Giai đoạn 2 — BLE handshake**
- [ ] **Giai đoạn 3 — Hoàn thiện**

## Giai đoạn 0 — Vá lỗi blocker ✅

File chính: `src/localsend/LocalSendManager.cpp` (+ `.h`)

| # | Lỗi | Fix |
|---|-----|-----|
| 1 | RCE qua tên file zip (`system()`) | `fork` + `execlp`, không qua shell |
| 2 | Stale `reqIdx` trong `handleFileUpload` | lookup lại theo `sessionId`+`fileId` mỗi lần (`getState`/`setState`/`getCopy`) |
| 3 | `randomHex` data race | `thread_local rng` |
| 4 | Ghi đè file trùng tên | `uniquePath()` → `tên (2).ext` |
| 5 | `handleCancel` xóa cả file DONE | chỉ `unlink` khi `state != DONE` |
| 6 | `sendFileMetaAsync` trả token mồ côi | tách `sendFileMetaWithProgress`, trả `shared_ptr<LsSendProgress>` |
| 7 | Vệ sinh | xóa rule `.zip` chết, media → `Imgs/`/`Videos/`/`Music/`, gọn ternary |

Chưa build kiểm chứng — chạy `./build_pc.sh` trên máy dev.

## Giai đoạn 1 — Test Brick↔Brick thực tế

Checklist:

1. Máy A: OFFLINE → Phát nhóm; máy B: join bằng PSK (thủ công trước).
2. Gửi file nhỏ A→B, B duyệt nhận, verify nội dung.
3. Gửi file lớn (vài trăm MB), xem tiến trình.
4. Cancel giữa chừng từ B → file dở được dọn.
5. Gửi file trùng tên → có ` (2)`.
6. 2 máy trùng alias → discovery vẫn phân biệt (fingerprint).
7. Nhận multi-file từ app LocalSend gốc (chỉ file đầu được nhận — hạn chế đã biết).
8. Chụp màn hình từng màn.

## Giai đoạn 2 — BLE handshake

### 2.1 Khảo sát phần cứng (qua adb)

- Chip XR829 hỗ trợ Bluetooth 2.1/4.0/4.1 dual-mode ở mức phần cứng → có BLE,
  HCI qua UART (theo product brief của Xradio).
- Chưa rõ firmware TrimUI có bật BT / có BlueZ không. Chạy trên máy:

```sh
adb shell "hciconfig -a 2>&1 | head -30"
adb shell "ls /sys/class/bluetooth 2>&1; which bluetoothd"
adb shell "dmesg 2>/dev/null | grep -iE 'bluetooth|hci|xr829' | head -20"
```

- Nếu không có hci0: kiểm tra kernel config (`/proc/config.gz | grep BT_HCIUART`),
  device tree UART, firmware patchram.

### 2.2 Thiết kế (nếu có hci0 + LE)

Module mới `src/ble/BleManager.{h,cpp}` — raw HCI socket, không thêm dependency
(hợp với phong cách codebase hiện tại):

- **Advertise**: Flags + 128-bit Service UUID BrickDrop + Service Data chứa 4 ký
  tự đầu fingerprint. Vừa trong 31 byte của BLE advertisement.
- **Scan**: lọc theo Service UUID, lấy RSSI để biết "gần".
- **Luồng**: SSID nhóm WiFi = `BrickDrop-<fp4>` — chính là 4 ký tự trong
  advertisement → máy quét thấy peer là suy ra SSID, join thẳng bằng PSK cố
  định, bỏ qua bước `wpa_cli scan` vốn hay chập chờn.
- Màn OFFLINE: thêm danh sách peer BLE gần đây + RSSI; A = join thẳng.
- Không cần GATT ở v1 (alias/fingerprint đầy đủ lấy qua LocalSend `/info`
  sau khi vào WiFi).

### 2.3 Test

2 máy để gần nhau → tự thấy nhau qua BLE → join → gửi file không cần quét
WiFi thủ công.

## Giai đoạn 3 — Hoàn thiện

- Dọn nốt comment/di sản RomCloud.
- `postProcessUpload`: xem lại việc tự bung mọi `.zip` (kể cả ROM arcade) —
  cờ `extract` trong `heuristicMap` hiện không được dùng.
- Mở rộng `--selftest` (thêm check BLE nếu có).
- Cập nhật README.
