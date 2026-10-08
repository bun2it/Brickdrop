#pragma once
// BrickDrop WifiDirectManager — offline mode "gặp ngoài đường là send":
// 1 máy PHÁT nhóm (hostapd wlan1 + udhcpd 192.168.49.1), 1 máy THAM GIA
// (wpa_cli join SSID BrickDrop-* với PSK cố định, không cần gõ pass).
// Chạy lệnh hệ thống qua popen; các op chặn chạy trên thread nền của caller.
// Điều kiện của Tai: dùng hotspot thì mất internet → thoát app / ngắt hotspot
// phải khôi phục WiFi gốc NGAY (saveOriginalWifi + restoreOriginalWifi).
#include <functional>
#include <string>
#include <vector>

namespace BrickDrop {

struct WifiGroup {
  std::string ssid;
  int signalDbm = -100;
};

enum class LinkMode { NONE, STATION, HOST, JOINED };

class WifiDirectManager {
public:
  static WifiDirectManager &instance();

  bool supported(); // iw list có P2P-GO/AP không
  LinkMode mode() const { return m_mode; }
  std::string groupSsid() const { return m_ssid; }
  std::string ownIp(); // IP hiện tại (ưu tiên wlan1 rồi wlan0)

  // Phát nhóm, trả về SSID đã dựng (rỗng nếu lỗi). Chặn ~3-5s.
  std::string startGroup(std::string &outMsg);
  // Phase D (BLE handshake): phát nhóm với tag cho trước trong SSID
  // ("BrickDrop-<tag>", tag = 8 hex shortId) để bên join verify đúng máy.
  std::string startGroupWithTag(const std::string &tag, std::string &outMsg);
  bool stopGroup(std::string &outMsg);
  // Quét nhóm BrickDrop-* gần đây. Chặn ~5-8s.
  std::vector<WifiGroup> scanGroups();
  // Tham gia nhóm (PSK cố định). Chặn tới ~20s.
  bool joinGroup(const std::string &ssid, std::string &outMsg);
  bool leaveGroup(std::string &outMsg);

  // Lưu SSID WiFi gốc 1 lần khi app khởi động (trước khi đụng vào hotspot).
  // Bỏ qua nếu đang dính trong nhóm BrickDrop-* (thoát bẩn lần trước).
  void saveOriginalWifi();
  // Khôi phục WiFi gốc: tắt hostapd/udhcpd, xóa profile BrickDrop-* đã lưu
  // (kẻo wpa tự join lại → mất internet), rồi về lại SSID gốc. Chặn ~12s
  // best-effort (wpa_supplicant vẫn tự thử tiếp nếu chưa xong).
  void restoreOriginalWifi(std::string &outMsg);

private:
  WifiDirectManager() = default;
  LinkMode m_mode = LinkMode::NONE;
  std::string m_ssid;
  std::string m_origSsid; // SSID WiFi gốc trước khi dùng hotspot
  static std::string exec(const std::string &cmd);
  static std::string wpa(const std::string &args);
  static bool haveTool(const std::string &name);
  // id của network trong `wpa_cli list_networks` theo SSID (rỗng nếu không có).
  static std::string findNetworkId(const std::string &ssid);
  // Xóa các profile BrickDrop-* đã save_config.
  static void removeBrickDropNetworks();
};

} // namespace BrickDrop
