#pragma once
// BrickDrop BleLinkManager — BLE phát hiện + handshake connectionless.
// Raw HCI socket (AF_BLUETOOTH), KHÔNG cần BlueZ D-Bus hay bluetoothd chạy.
// Thread nền: duty-cycle scan LE, parse ADV có magic "BD", maintain peer map.
//
// ADV (manufacturer specific, ver=1):
//   [magic 'B''D' (2)] [ver (1)] [shortId: 8 ASCII hex (8)]
//   [flags (1)] [targetId: 8 ASCII hex (8)]
// Scan response: complete local name = alias.
//
// Flags: bit0 WANT_LINK (muốn nối tới targetId), bit1 IS_AP (đang phát
// Soft-AP), bit2 ACCEPT (đồng ý nối với targetId).
//
// Tự bring-up hci0 lúc init (hciattach) nếu chưa có — user không làm tay.
// Hết pin: chỉ scan tích cực khi màn "Gửi đi" mở hoặc đang handshake;
// bình thường scan nền duty-cycle nhẹ + advertise interval 1s.
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace BrickDrop {

enum BleAdvFlags : uint8_t {
  BLE_F_WANT_LINK = 0x01,
  BLE_F_IS_AP = 0x02,
  BLE_F_ACCEPT = 0x04,
};

struct BlePeer {
  std::string shortId; // 8 hex == 8 ký tự đầu fingerprint máy kia
  std::string alias;   // từ scan response (có thể rỗng)
  int rssi = -100;
  uint8_t flags = 0;
  std::string targetId; // 8 hex, có nghĩa khi flags có WANT_LINK/ACCEPT
  int64_t lastSeenMs = 0;
};

class BleLinkManager {
public:
  static BleLinkManager &instance();

  // Bring-up hci0 + mở HCI socket + thread scan + bật ADV. Chạy nền, trả về
  // ngay; ready() cho biết khi nào xong. Thất bại (không có BT) → BLE tắt
  // lặng lẽ, app vẫn chạy LAN-only.
  void initAsync();
  bool ready() const { return m_ready.load(); }
  void shutdown();

  // true khi đang ở màn "Gửi đi" → scan mạnh. Khi false vẫn scan nền
  // duty-cycle nhẹ để máy kia bắt tay được dù mình đang ở HOME.
  void setActive(bool active) { m_active.store(active); }
  // true khi đang trong 1 handshake → scan liên tục + ADV 100ms cho nhanh.
  void setLinkActive(bool b) { m_linkActive.store(b); }

  // Peers tươi (<15s), đã trừ chính mình.
  std::vector<BlePeer> peers();

  // Chế độ ADV (thread-safe; worker thread áp dụng).
  void advertiseIdle();
  void advertiseWantLink(const std::string &targetShortId);
  void advertiseAccept(const std::string &targetShortId);
  void setHosting(bool hosting);

  // Có máy nào muốn nối tới mình (WANT_LINK nhắm vào shortId mình).
  // Trả shortId máy yêu cầu (rỗng nếu không có); mỗi request chỉ trả 1 lần.
  std::string takeLinkRequest();

  // shortId của chính mình: 8 ký tự đầu fingerprint.
  static std::string ownShortId();
  static bool hciPresent();
  // Lệnh bring-up hci0 (mặc định theo cộng đồng XR829; chỉnh khi cần).
  static void setBringupCmd(const std::string &cmd) { s_bringup = cmd; }

private:
  BleLinkManager() = default;
  void worker();
  bool openHci();
  void programAdv(); // program ADV params + data + enable (worker thread)
  void setScan(bool on);
  void onHciEvent(const uint8_t *pkt, size_t len);
  void parseAdv(const uint8_t *data, uint8_t dlen, int rssi);
  static bool hciCmd(int fd, uint8_t ogf, uint16_t ocf, const uint8_t *params,
                     uint8_t plen);

  std::atomic<bool> m_initStarted{false};
  std::atomic<bool> m_ready{false};
  std::atomic<bool> m_running{false};
  std::atomic<bool> m_active{false};
  std::atomic<bool> m_linkActive{false};
  int m_fd = -1;

  std::mutex m_mtx;
  std::map<std::string, BlePeer> m_peers;
  // Trạng thái ADV muốn phát:
  uint8_t m_advFlags = 0;
  std::string m_advTarget; // 8 hex
  bool m_hosting = false;
  bool m_advDirty = true;
  // Request đang chờ xử lý (từ worker):
  std::string m_incomingReq;
  int64_t m_incomingReqMs = 0;

  static std::string s_bringup;
};

} // namespace BrickDrop
