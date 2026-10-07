#include "Config.h"
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

std::string Config::randomHex(int n) {
  static const char hex[] = "0123456789abcdef";
  std::string out;
  std::ifstream ur("/dev/urandom", std::ios::binary);
  unsigned char buf[64];
  if (ur && n <= 64 && ur.read(reinterpret_cast<char *>(buf), n)) {
    for (int i = 0; i < n; ++i)
      out += hex[buf[i] & 0xF];
    return out;
  }
  unsigned seed = static_cast<unsigned>(time(nullptr) ^ getpid());
  for (int i = 0; i < n; ++i) {
    seed = seed * 1103515245 + 12345;
    out += hex[(seed >> 16) & 0xF];
  }
  return out;
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
  m_fp = readFile1(d + "/fp");
  if (m_fp.size() != 32) {
    m_fp = randomHex(32);
    writeFile1(d + "/fp", m_fp);
    Logger::info("BrickDrop: generated new fingerprint");
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
  load();
  return m_fp;
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

} // namespace BrickDrop
