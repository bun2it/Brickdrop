#include "Config.h"
#include "DeviceIdentity.h"
#include "Logger.h"
#include <cstdlib>
#include <fstream>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <unistd.h>

namespace BrickDrop {

Config &Config::instance() {
  static Config inst;
  return inst;
}

void Config::init() {
  struct stat st;
  if (stat("/mnt/SDCARD", &st) == 0 && S_ISDIR(st.st_mode)) {
    m_sdRoot = "/mnt/SDCARD";
  } else {
    const char *home = getenv("HOME");
    m_sdRoot = home ? home : ".";
  }
  load();
}

std::string Config::stateDir() {
  std::string d = m_sdRoot + "/.brickdrop";
  mkdir(d.c_str(), 0755);
  return d;
}

std::string Config::readFile1(const std::string &p) {
  std::ifstream in(p);
  std::string s;
  if (std::getline(in, s)) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
      s.pop_back();
  }
  return s;
}

void Config::writeFile1(const std::string &p, const std::string &s) {
  std::ofstream out(p, std::ios::trunc);
  out << s;
}

void Config::load() {
  if (m_loaded)
    return;
  m_loaded = true;
  std::string d = stateDir();
  m_alias = readFile1(d + "/alias");
  if (m_alias.empty()) {
    static const char *kAdj[] = {"Nice",  "Sweet",  "Neat",  "Brave", "Calm",
                                 "Eager", "Gentle", "Happy", "Kind",  "Lively",
                                 "Merry", "Proud",  "Quick", "Silly", "Tidy",
                                 "Witty", "Zesty",  "Clever"};
    static const char *kFruit[] = {"Orange", "Apple", "Banana", "Mango",
                                   "Peach",  "Grape", "Lemon",  "Melon",
                                   "Berry",  "Kiwi",  "Papaya", "Cherry",
                                   "Plum",   "Pear",  "Lychee", "Coconut"};
    unsigned seed = static_cast<unsigned>(time(nullptr) ^ getpid());
    auto pick = [&](int n) {
      seed = seed * 1103515245 + 12345;
      return (seed >> 16) % (unsigned)n;
    };
    m_alias = std::string(kAdj[pick(18)]) + " " + kFruit[pick(16)];
    writeFile1(d + "/alias", m_alias);
  }
  m_saveDir = readFile1(d + "/savedir");
  if (m_saveDir.empty()) {
    m_saveDir = m_sdRoot + "/BrickDrop";
    mkdir(m_saveDir.c_str(), 0755);
    writeFile1(d + "/savedir", m_saveDir);
  }
}

std::string Config::alias() {
  load();
  return m_alias;
}
void Config::setAlias(const std::string &a) {
  load();
  m_alias = a;
  writeFile1(stateDir() + "/alias", a);
}
void Config::regenerateAlias() {
  static const char *kAdj[] = {"Nice",  "Sweet",  "Neat",  "Brave", "Calm",
                               "Eager", "Gentle", "Happy", "Kind",  "Lively",
                               "Merry", "Proud",  "Quick", "Silly", "Tidy",
                               "Witty", "Zesty",  "Clever"};
  static const char *kFruit[] = {"Orange", "Apple", "Banana", "Mango",
                                 "Peach",  "Grape", "Lemon",  "Melon",
                                 "Berry",  "Kiwi",  "Papaya", "Cherry",
                                 "Plum",   "Pear",  "Lychee", "Coconut"};
  unsigned seed =
      static_cast<unsigned>(time(nullptr) ^ getpid() ^ rand());
  auto pick = [&](int n) {
    seed = seed * 1103515245 + 12345;
    return (seed >> 16) % (unsigned)n;
  };
  setAlias(std::string(kAdj[pick(18)]) + " " + kFruit[pick(16)]);
}
std::string Config::fingerprint() {
  // ID ổn định từ phần cứng (MAC-hash), không còn random mỗi máy.
  return DeviceIdentity::instance().deviceId();
}
std::string Config::saveDir() {
  load();
  return m_saveDir;
}
void Config::setSaveDir(const std::string &d) {
  load();
  m_saveDir = d;
  writeFile1(stateDir() + "/savedir", d);
}

int Config::visibility() {
  load();
  std::string v = readFile1(stateDir() + "/visibility");
  if (v == "1")
    return 1;
  if (v == "2")
    return 2;
  return 0;
}
void Config::setVisibility(int v) {
  load();
  if (v < 0)
    v = 0;
  if (v > 2)
    v = 2;
  writeFile1(stateDir() + "/visibility", std::to_string(v));
}

std::vector<Config::Contact> Config::contacts() {
  load();
  std::vector<Contact> out;
  std::ifstream in(stateDir() + "/contacts");
  std::string line;
  while (std::getline(in, line)) {
    // fp|alias|ip|port
    size_t a = line.find('|');
    size_t b = line.find('|', a + 1);
    size_t c = line.find('|', b + 1);
    if (a == std::string::npos || b == std::string::npos || c == std::string::npos)
      continue;
    Contact ct;
    ct.fp = line.substr(0, a);
    ct.alias = line.substr(a + 1, b - a - 1);
    ct.ip = line.substr(b + 1, c - b - 1);
    try {
      ct.port = std::stoi(line.substr(c + 1));
    } catch (...) {
      ct.port = 53317;
    }
    if (!ct.fp.empty())
      out.push_back(ct);
  }
  return out;
}

static std::string contactFile(const std::string &dir) { return dir + "/contacts"; }

bool Config::addContact(const std::string &fp, const std::string &alias,
                        const std::string &ip, int port) {
  load();
  if (fp.empty())
    return false;
  auto v = contacts();
  for (auto &c : v) {
    if (c.fp == fp) {
      c.alias = alias;
      c.ip = ip;
      c.port = port > 0 ? port : 53317;
    }
  }
  bool found = false;
  for (auto &c : v)
    if (c.fp == fp)
      found = true;
  if (!found) {
    Contact c;
    c.fp = fp;
    c.alias = alias;
    c.ip = ip;
    c.port = port > 0 ? port : 53317;
    v.push_back(c);
  }
  std::string tmp;
  for (auto &c : v) {
    std::string a = c.alias;
    for (auto &ch : a)
      if (ch == '|')
        ch = ' ';
    tmp += c.fp + "|" + a + "|" + c.ip + "|" + std::to_string(c.port) + "\n";
  }
  writeFile1(contactFile(stateDir()), tmp);
  return true;
}

bool Config::removeContact(const std::string &fp) {
  load();
  auto v = contacts();
  std::string tmp;
  bool removed = false;
  for (auto &c : v) {
    if (c.fp == fp) {
      removed = true;
      continue;
    }
    tmp += c.fp + "|" + c.alias + "|" + c.ip + "|" + std::to_string(c.port) + "\n";
  }
  if (removed)
    writeFile1(contactFile(stateDir()), tmp);
  return removed;
}

bool Config::isContact(const std::string &fp) {
  if (fp.empty())
    return false;
  for (auto &c : contacts())
    if (c.fp == fp)
      return true;
  return false;
}

bool Config::updateContactNet(const std::string &fp, const std::string &ip,
                              int port) {  load();
  if (fp.empty() || ip.empty())
    return false;
  auto v = contacts();
  bool changed = false;
  for (auto &c : v) {
    if (c.fp == fp && (c.ip != ip || c.port != port)) {
      c.ip = ip;
      c.port = port > 0 ? port : 53317;
      changed = true;
    }
  }
  if (!changed)
    return false;
  std::string tmp;
  for (auto &c : v) {
    std::string a = c.alias;
    for (auto &ch : a)
      if (ch == '|')
        ch = ' ';
    tmp += c.fp + "|" + a + "|" + c.ip + "|" + std::to_string(c.port) + "\n";
  }
  writeFile1(stateDir() + "/contacts", tmp);
  return true;
}

static std::string ipOf(const std::string &iface) {
  struct ifaddrs *list = nullptr;
  if (getifaddrs(&list) < 0)
    return "";
  std::string out;
  for (auto *p = list; p; p = p->ifa_next) {
    if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET)
      continue;
    if (iface != p->ifa_name)
      continue;
    char buf[16] = {0};
    auto *a = (struct sockaddr_in *)p->ifa_addr;
    if (inet_ntop(AF_INET, &a->sin_addr, buf, sizeof(buf))) {
      if (std::string(buf) != "127.0.0.1") {
        out = buf;
        break;
      }
    }
  }
  freeifaddrs(list);
  return out;
}

std::string Config::netInterface() {
  // Ưu tiên iface AP/GO (wlan1) vì offline mode phát nhóm ở đó.
  const char *cands[] = {"wlan1", "wlan0", "p2p-wlan0-0", nullptr};
  for (int i = 0; cands[i]; ++i) {
    if (!ipOf(cands[i]).empty())
      return cands[i];
  }
  // Fallback: iface đầu tiên có IP không loopback.
  struct ifaddrs *list = nullptr;
  if (getifaddrs(&list) == 0) {
    std::string out;
    for (auto *p = list; p; p = p->ifa_next) {
      if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET || !p->ifa_name)
        continue;
      char buf[16] = {0};
      auto *a = (struct sockaddr_in *)p->ifa_addr;
      if (inet_ntop(AF_INET, &a->sin_addr, buf, sizeof(buf)) &&
          std::string(buf) != "127.0.0.1") {
        out = p->ifa_name;
        break;
      }
    }
    freeifaddrs(list);
    return out;
  }
  return "";
}

// Preset gửi: file/folder đã chọn sẵn để chờ gửi (persist qua lần mở app).
std::string Config::sendPresetFile() {
  load();
  return readFile1(stateDir() + "/sendfile");
}
void Config::setSendPresetFile(const std::string &p) {
  load();
  writeFile1(stateDir() + "/sendfile", p);
}
std::string Config::sendPresetFolder() {
  load();
  return readFile1(stateDir() + "/sendfolder");
}
void Config::setSendPresetFolder(const std::string &p) {
  load();
  writeFile1(stateDir() + "/sendfolder", p);
}

} // namespace BrickDrop
