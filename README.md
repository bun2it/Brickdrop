# BrickDrop — AirDrop-style offline file sharing for TrimUI Brick

Standalone app tách từ LocalSend P2P của RomCloud, mục tiêu: **2 Brick gặp nhau
ngoài đường, bật chế độ lên là send — không cần router, không cần internet.**

Luồng định hướng (như AirDrop):
1. **Bluetooth/BLE**: phát hiện máy gần + handshake ban đầu.
2. **Wi-Fi Direct / SoftAP**: tự lập kênh Wi-Fi riêng giữa 2 máy, truyền tốc độ cao.

## Phần cứng đã xác nhận (TrimUI Brick, XR829 SDIO)

* `iw list`: hỗ trợ `AP`, `P2P-client`, `P2P-GO`, `P2P-device`;
  combo `#{ managed } <= 2, #{ AP } <= 1` và `#{ managed } <= 2, #{ P2P-client, P2P-GO } <= 1`
  (`#channels <= 1`).
* Driver tạo sẵn 2 vif: `wlan0=managed` + `wlan1=AP`.
* Stock có sẵn `hostapd`, `udhcpd`, `dnsmasq`, `wpa_cli v2.6` (đủ lệnh `p2p_*`).
* BT combo `XR829` (`BT 4.2 BR/EDR+BLE`), fw `fw_xr829_bt.bin`, `bt_init.sh_xr829`
  (`hciattach ttyS1 xradio`) — `hci0` phải init tay, stock không auto-start.

## Source gốc (seed)

`src/localsend/` copy verbatim từ RomCloud:

* `LocalSendManager.h` / `LocalSendManager.cpp` — discovery (UDP multicast
  `224.0.0.167:53317` + broadcast fallback), transfer (TCP `:53317`,
  `POST /api/localsend/v2/prepare-upload|upload|cancel|info|register`).
* `LocalSendProtocol.h` — constants + struct `LsDeviceInfo/LsFileMeta/LsUploadRequest`.

Xem `docs/ORIGIN.md` để biết commit gốc + dependency cần tách stub khi build độc lập.

## Roadmap

* [ ] P0: tách stub cho các dep RomCloud (`AppConfig`, `Logger`, `DatabaseManager`,
  `RomIndexer`, `RomOrganizer`, `UIManager`) → build binary độc lập `brickdrop`.
* [ ] P1: `WifiDirectManager` — offline mode: 1 máy phát (`hostapd wlan1` +
  `udhcpd 192.168.49.1`), 1 máy thu (`wpa_cli` join) → reuse LocalSend TCP.
* [ ] P2: `p2p_group_add` / `p2p_connect` chuẩn Wi-Fi Direct thay SoftAP tay.
* [ ] P3: BLE handshake (`hciattach` + GATT advertise `alias/fingerprint/GO creds`).
