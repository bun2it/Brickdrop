#include "WifiDirectManager.h"
#include "../core/Config.h"
#include "../core/Logger.h"
#include <arpa/inet.h>
#include <cstdio>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <unistd.h>

namespace BrickDrop {

WifiDirectManager &WifiDirectManager::instance() {
  static WifiDirectManager inst;
  return inst;
}

std::string WifiDirectManager::exec(const std::string &cmd) {
  FILE *p = popen((cmd + " 2>/dev/null").c_str(), "r");
  if (!p)
    return "";
  std::string out;
  char buf[1024];
  while (fgets(buf, sizeof(buf), p))
    out += buf;
  pclose(p);
  return out;
}

bool WifiDirectManager::haveTool(const std::string &name) {
  return access(("/usr/sbin/" + name).c_str(), X_OK) == 0 ||
         access(("/usr/bin/" + name).c_str(), X_OK) == 0;
}

std::string WifiDirectManager::wpa(const std::string &args) {
  return exec("wpa_cli -p /etc/wifi/sockets -i wlan0 " + args);
}

bool WifiDirectManager::supported() {
  if (!haveTool("iw"))
    return false;
  std::string l = exec("iw list");
  return l.find("P2P-GO") != std::string::npos ||
         l.find("* AP") != std::string::npos;
}

std::string WifiDirectManager::ownIp() {
  std::string iface = Config::instance().netInterface();
  if (iface.empty())
    return "";
  struct ifaddrs *list = nullptr;
  if (getifaddrs(&list) < 0)
    return "";
  std::string out;
  for (auto *p = list; p; p = p->ifa_next) {
    if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET || !p->ifa_name)
      continue;
    if (iface != p->ifa_name)
      continue;
    char buf[16] = {0};
    auto *a = (struct sockaddr_in *)p->ifa_addr;
    if (inet_ntop(AF_INET, &a->sin_addr, buf, sizeof(buf)) &&
        std::string(buf) != "127.0.0.1") {
      out = buf;
      break;
    }
  }
  freeifaddrs(list);
  return out;
}

std::string WifiDirectManager::startGroup(std::string &outMsg) {
  // SSID theo fingerprint gọn cho dễ nhận (flow thủ công).
  std::string tag = Config::instance().fingerprint().substr(0, 4);
  return startGroupWithTag(tag, outMsg);
}

std::string WifiDirectManager::startGroupWithTag(const std::string &tag,
                                                std::string &outMsg) {
  if (!haveTool("hostapd") || !haveTool("udhcpd")) {
    outMsg = "Thiếu hostapd/udhcpd trên máy";
    return "";
  }
  m_ssid = std::string(Config::kGroupPrefix) + tag;
  std::string conf = "/tmp/brickdrop_hostapd.conf";
  {
    FILE *f = fopen(conf.c_str(), "w");
    if (!f) {
      outMsg = "Không ghi được hostapd conf";
      return "";
    }
    fprintf(f,
            "interface=wlan1\ndriver=nl80211\nssid=%s\nchannel=6\nhw_mode=g\n"
            "ignore_broadcast_ssid=0\nauth_algs=1\nwpa=2\nwpa_passphrase=%s\n"
            "wpa_key_mgmt=WPA-PSK\nwpa_pairwise=CCMP\nrsn_pairwise=CCMP\n",
            m_ssid.c_str(), Config::kGroupPsk);
    fclose(f);
  }
  exec("ifconfig wlan1 up");
  exec("ifconfig wlan1 " + std::string(Config::kGroupIp) + " netmask 255.255.255.0");
  exec("killall hostapd 2>/dev/null; killall udhcpd 2>/dev/null");
  usleep(300000);
  exec("hostapd -B " + conf);
  usleep(500000);
  // udhcpd leases cho joiner
  {
    FILE *f = fopen("/tmp/brickdrop_udhcpd.conf", "w");
    if (f) {
      fprintf(f,
              "start 192.168.49.20\nend 192.168.49.254\ninterface wlan1\n"
              "pidfile /tmp/brickdrop_udhcpd.pid\n"
              "lease_file /tmp/brickdrop_udhcpd.leases\noption subnet 255.255.255.0\n"
              "option router 192.168.49.1\noption dns 192.168.49.1\n");
      fclose(f);
    }
  }
  exec("udhcpd /tmp/brickdrop_udhcpd.conf");
  sleep(1);
  std::string check = exec("ps | grep -v grep | grep hostapd | head -1");
  if (check.empty()) {
    outMsg = "hostapd không chạy được";
    m_ssid.clear();
    return "";
  }
  m_mode = LinkMode::HOST;
  outMsg = "Hotspot đang phát: " + m_ssid;
  Logger::info("BrickDrop: hosting " + m_ssid);
  return m_ssid;
}

// Parse "key=value" từ output `wpa_cli status` (dòng đầu hoặc sau \n).
static std::string wpaVal(const std::string &st, const char *key) {
  std::string pat = std::string(key) + "=";
  size_t p = st.find("\n" + pat);
  size_t v;
  if (p != std::string::npos) {
    v = p + 1 + pat.size();
  } else if (st.compare(0, pat.size(), pat) == 0) {
    v = pat.size();
  } else {
    return "";
  }
  size_t e = st.find('\n', v);
  return st.substr(v, e == std::string::npos ? e : e - v);
}

void WifiDirectManager::saveOriginalWifi() {
  if (!m_origSsid.empty())
    return; // chỉ lưu 1 lần mỗi lần chạy app
  std::string ssid = wpaVal(wpa("status"), "ssid");
  std::string prefix = Config::kGroupPrefix;
  // Bỏ qua nếu đang dính trong nhóm BrickDrop-* (thoát bẩn lần trước) —
  // lúc đó "gốc" thật sự không biết được, để trống cho an toàn.
  if (!ssid.empty() && ssid.compare(0, prefix.size(), prefix) != 0)
    m_origSsid = ssid;
  Logger::info(std::string("BrickDrop: original wifi ssid=") +
               (m_origSsid.empty() ? "(none)" : m_origSsid));
}

std::string WifiDirectManager::findNetworkId(const std::string &ssid) {
  std::string list = wpa("list_networks");
  size_t pos = 0;
  bool first = true;
  while (pos < list.size()) {
    size_t e = list.find('\n', pos);
    std::string line = list.substr(pos, e == std::string::npos ? e : e - pos);
    pos = e == std::string::npos ? list.size() : e + 1;
    if (first) {
      first = false;
      continue; // dòng header
    }
    // Format: network id \t ssid \t bssid \t flags
    size_t t = line.find('\t');
    if (t == std::string::npos)
      continue;
    size_t t2 = line.find('\t', t + 1);
    std::string id = line.substr(0, t);
    std::string s =
        line.substr(t + 1, t2 == std::string::npos ? t2 : t2 - t - 1);
    if (!id.empty() && s == ssid)
      return id;
  }
  return "";
}

void WifiDirectManager::removeBrickDropNetworks() {
  std::string prefix = Config::kGroupPrefix;
  std::string list = wpa("list_networks");
  std::vector<std::string> ids;
  size_t pos = 0;
  bool first = true;
  while (pos < list.size()) {
    size_t e = list.find('\n', pos);
    std::string line = list.substr(pos, e == std::string::npos ? e : e - pos);
    pos = e == std::string::npos ? list.size() : e + 1;
    if (first) {
      first = false;
      continue;
    }
    size_t t = line.find('\t');
    if (t == std::string::npos)
      continue;
    size_t t2 = line.find('\t', t + 1);
    std::string id = line.substr(0, t);
    std::string s =
        line.substr(t + 1, t2 == std::string::npos ? t2 : t2 - t - 1);
    if (!id.empty() && s.compare(0, prefix.size(), prefix) == 0)
      ids.push_back(id);
  }
  for (auto &id : ids)
    wpa("remove_network " + id);
  if (!ids.empty())
    wpa("save_config");
}

void WifiDirectManager::restoreOriginalWifi(std::string &outMsg) {
  // 1. Dừng hotspot: hostapd/udhcpd + hạ wlan1.
  exec("killall hostapd 2>/dev/null; killall udhcpd 2>/dev/null");
  exec("ifconfig wlan1 0.0.0.0 down 2>/dev/null; ifconfig wlan1 up");
  m_mode = LinkMode::NONE;
  m_ssid.clear();
  // 2. Xóa profile BrickDrop-* đã save_config — nếu không wpa_supplicant
  //    sẽ tự join lại hotspot BrickDrop → vẫn mất internet.
  removeBrickDropNetworks();
  // 3. Về lại WiFi gốc.
  outMsg = "Đã ngắt hotspot";
  if (m_origSsid.empty()) {
    wpa("reconnect"); // không biết gốc: để wpa tự quyết
    return;
  }
  std::string id = findNetworkId(m_origSsid);
  if (id.empty()) {
    outMsg = "Đã ngắt hotspot (không thấy WiFi " + m_origSsid + " đã lưu)";
    return;
  }
  wpa("select_network " + id); // chỉ bật đúng mạng gốc + reconnect
  wpa("save_config");
  // Chờ kết nối lại (best-effort ~12s; wpa_supplicant vẫn tự thử tiếp
  // dù hàm này đã trả về).
  for (int i = 0; i < 12; ++i) {
    sleep(1);
    std::string st = wpa("status");
    if (wpaVal(st, "wpa_state") == "COMPLETED" &&
        wpaVal(st, "ssid") == m_origSsid) {
      outMsg = "Đã về lại WiFi: " + m_origSsid;
      Logger::info("BrickDrop: wifi restored to " + m_origSsid);
      return;
    }
  }
  outMsg = "Đang kết nối lại WiFi: " + m_origSsid + "...";
}

bool WifiDirectManager::stopGroup(std::string &outMsg) {
  // "Ngắt kết nối" = dừng hotspot + về lại internet ngay (điều kiện của Tai).
  restoreOriginalWifi(outMsg);
  return true;
}

static int parseSignal(const std::string &s) {
  try {
    return std::stoi(s);
  } catch (...) {
    return -100;
  }
}

std::vector<WifiGroup> WifiDirectManager::scanGroups() {
  std::vector<WifiGroup> out;
  wpa("scan");
  for (int i = 0; i < 8; ++i) {
    sleep(1);
    std::string r = wpa("scan_results");
    if (r.find('\n') != r.rfind('\n'))
      break;
  }
  std::string res = wpa("scan_results");
  size_t pos = 0;
  bool first = true;
  while (pos < res.size()) {
    size_t e = res.find('\n', pos);
    std::string line = res.substr(pos, e == std::string::npos ? e : e - pos);
    pos = e == std::string::npos ? res.size() : e + 1;
    if (first) {
      first = false;
      continue;
    }
    // bssid \t freq \t signal \t flags \t ssid
    size_t t1 = line.find('\t'), t2, t3, t4;
    if (t1 == std::string::npos)
      continue;
    t2 = line.find('\t', t1 + 1);
    t3 = line.find('\t', t2 + 1);
    t4 = line.find('\t', t3 + 1);
    if (t2 == std::string::npos || t3 == std::string::npos || t4 == std::string::npos)
      continue;
    std::string ssid = line.substr(t4 + 1);
    std::string prefix = Config::kGroupPrefix;
    if (ssid.compare(0, prefix.size(), prefix) != 0)
      continue;
    WifiGroup g;
    g.ssid = ssid;
    g.signalDbm = parseSignal(line.substr(t2 + 1, t3 - t2 - 1));
    bool dup = false;
    for (auto &x : out)
      if (x.ssid == g.ssid) {
        dup = true;
        if (g.signalDbm > x.signalDbm)
          x = g;
      }
    if (!dup)
      out.push_back(g);
  }
  return out;
}

bool WifiDirectManager::joinGroup(const std::string &ssid, std::string &outMsg) {
  if (ssid.empty()) {
    outMsg = "Chưa chọn hotspot";
    return false;
  }
  // Xóa profile trùng để khỏi đánh nhau.
  {
    std::string list = wpa("list_networks");
    size_t pos = 0;
    bool first = true;
    while (pos < list.size()) {
      size_t e = list.find('\n', pos);
      std::string line = list.substr(pos, e == std::string::npos ? e : e - pos);
      pos = e == std::string::npos ? list.size() : e + 1;
      if (first) {
        first = false;
        continue;
      }
      size_t t = line.find('\t');
      if (t == std::string::npos)
        continue;
      size_t t2 = line.find('\t', t + 1);
      std::string id = line.substr(0, t);
      std::string s = line.substr(t + 1, t2 - t - 1);
      if (s == ssid)
        wpa("remove_network " + id);
    }
  }
  std::string id = wpa("add_network");
  size_t e = id.find('\n');
  id = id.substr(0, e);
  while (!id.empty() && (id.back() < '0' || id.back() > '9'))
    id.pop_back();
  if (id.empty()) {
    outMsg = "Không tạo được profile mạng";
    return false;
  }
  wpa("set_network " + id + " ssid '\"" + ssid + "\"'");
  wpa("set_network " + id + " psk '\"" + std::string(Config::kGroupPsk) + "\"'");
  wpa("enable_network " + id);
  wpa("save_config");
  wpa("reconnect");
  for (int i = 0; i < 20; ++i) {
    sleep(1);
    std::string st = wpa("status");
    auto val = [&](const char *k) {
      std::string pat = std::string(k) + "=";
      size_t p = st.find("\n" + pat);
      if (p == std::string::npos && st.compare(0, pat.size(), pat) != 0)
        return std::string();
      size_t v = (p == std::string::npos) ? pat.size() : p + 1 + pat.size();
      size_t ee = st.find('\n', v);
      return st.substr(v, ee == std::string::npos ? ee : ee - v);
    };
    if (val("wpa_state") == "COMPLETED") {
      m_mode = LinkMode::JOINED;
      m_ssid = ssid;
      std::string ip = val("ip_address");
      outMsg = "Đã kết nối " + ssid + (ip.empty() ? "" : " (" + ip + ")");
      return true;
    }
  }
  outMsg = "Kết nối thất bại: " + ssid;
  return false;
}

bool WifiDirectManager::leaveGroup(std::string &outMsg) { return stopGroup(outMsg); }

} // namespace BrickDrop
