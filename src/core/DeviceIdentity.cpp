#include "DeviceIdentity.h"
#include "Config.h"
#include "Logger.h"
#include <openssl/sha.h>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace BrickDrop {

namespace {
// Domain riêng của BrickDrop (RomCloud dùng "RomCloud-v2"): khác nhau để
// hash của cùng 1 máy không tương quan giữa 2 app.
constexpr const char *kDomain = "BrickDrop-v1";
}

DeviceIdentity &DeviceIdentity::instance() {
  static DeviceIdentity inst;
  return inst;
}

std::string DeviceIdentity::trimLower(const std::string &s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos)
    return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  std::string out = s.substr(a, b - a + 1);
  for (auto &c : out)
    c = (char)std::tolower((unsigned char)c);
  return out;
}

bool DeviceIdentity::isValidHardwareId(const std::string &id) {
  if (id.size() < 8)
    return false;
  if (id == "unknown" || id == "none" || id == "ffffffffffff" ||
      id == "deadbeef")
    return false;
  bool allZero = true;
  for (char c : id)
    if (c != '0') {
      allZero = false;
      break;
    }
  return !allZero;
}

std::string DeviceIdentity::tryMacAddress() {
  // eth0 trước (ổn định hơn WiFi), rồi wlan0/wlan1 (TrimUI Brick Pro).
  const char *paths[] = {"/sys/class/net/eth0/address",
                         "/sys/class/net/wlan0/address",
                         "/sys/class/net/wlan1/address", nullptr};
  for (int i = 0; paths[i]; ++i) {
    std::ifstream f(paths[i]);
    if (!f.is_open())
      continue;
    std::string mac;
    std::getline(f, mac);
    // Chuẩn hóa: trim, lowercase, bỏ dấu ':' → 12 hex.
    std::string norm;
    for (char c : trimLower(mac))
      if (c != ':')
        norm += c;
    if (isValidHardwareId(norm))
      return norm;
  }
  return "";
}

std::string DeviceIdentity::tryMachineId() {
  const char *paths[] = {"/etc/machine-id", "/var/lib/dbus/machine-id",
                         nullptr};
  for (int i = 0; paths[i]; ++i) {
    std::ifstream f(paths[i]);
    if (!f.is_open())
      continue;
    std::string id;
    std::getline(f, id);
    id = trimLower(id);
    if (isValidHardwareId(id))
      return id;
  }
  return "";
}

std::string DeviceIdentity::getOrCreateInstallId() {
  std::string path = Config::instance().stateDir() + "/device_id";
  {
    std::ifstream in(path);
    std::string id;
    if (std::getline(in, id)) {
      id = trimLower(id);
      if (isValidHardwareId(id))
        return id;
    }
  }
  // Random 32 hex từ /dev/urandom, persist để ổn định.
  static const char hex[] = "0123456789abcdef";
  std::string id;
  std::ifstream ur("/dev/urandom", std::ios::binary);
  unsigned char buf[16] = {0};
  if (ur)
    ur.read(reinterpret_cast<char *>(buf), sizeof(buf));
  for (int i = 0; i < 16; ++i) {
    id += hex[(buf[i] >> 4) & 0xF];
    id += hex[buf[i] & 0xF];
  }
  std::ofstream out(path, std::ios::trunc);
  out << id;
  return id;
}

std::string DeviceIdentity::hashWithDomain(const std::string &rawId) {
  std::string toHash = std::string(kDomain) + ":" + rawId;
  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char *>(toHash.c_str()),
         toHash.size(), hash);
  std::ostringstream oss;
  for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
    oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
  return oss.str();
}

std::string DeviceIdentity::deviceId() {
  if (m_init)
    return m_deviceId;
  m_init = true;
  std::string hw = tryMacAddress();
  if (!hw.empty()) {
    m_source = "mac";
  } else if (!(hw = tryMachineId()).empty()) {
    m_source = "machine_id";
  } else {
    hw = getOrCreateInstallId();
    m_source = "install_id";
  }
  m_deviceId = hashWithDomain(hw);
  Logger::info("BrickDrop: device id " + shortId() + " (source: " + m_source +
               ")");
  return m_deviceId;
}

std::string DeviceIdentity::shortId() {
  std::string full = deviceId();
  return "BD-" + full.substr(0, 8);
}

std::string DeviceIdentity::idSource() {
  deviceId(); // đảm bảo đã init
  return m_source;
}

} // namespace BrickDrop
