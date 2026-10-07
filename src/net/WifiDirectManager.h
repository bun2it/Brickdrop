#pragma once
// BrickDrop WifiDirectManager — offline mode "gặp ngoài đường là send":
// 1 máy PHÁT nhóm (hostapd wlan1 + udhcpd 192.168.49.1), 1 máy THAM GIA
// (wpa_cli join SSID BrickDrop-* với PSK cố định, không cần gõ pass).
// Chạy lệnh hệ thống qua popen; các op chặn chạy trên thread nền của caller.
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
  bool stopGroup(std::string &outMsg);
  // Quét nhóm BrickDrop-* gần đây. Chặn ~5-8s.
  std::vector<WifiGroup> scanGroups();
  // Tham gia nhóm (PSK cố định). Chặn tới ~20s.
  bool joinGroup(const std::string &ssid, std::string &outMsg);
  bool leaveGroup(std::string &outMsg);

private:
  WifiDirectManager() = default;
  LinkMode m_mode = LinkMode::NONE;
  std::string m_ssid;
  static std::string exec(const std::string &cmd);
  static std::string wpa(const std::string &args);
  static bool haveTool(const std::string &name);
};

} // namespace BrickDrop
