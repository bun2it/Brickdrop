#include "BleLinkManager.h"
#include "../core/Config.h"
#include "../core/DeviceIdentity.h"
#include "../core/Logger.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

// Không phụ thuộc BlueZ headers (cross-compile an toàn): tự định nghĩa.
#ifndef AF_BLUETOOTH
#define AF_BLUETOOTH 31
#endif
#define BTPROTO_HCI 1

namespace BrickDrop {

std::string BleLinkManager::s_bringup = "hciattach /dev/ttyS1 xr829";

BleLinkManager &BleLinkManager::instance() {
  static BleLinkManager inst;
  return inst;
}

namespace {
// sockaddr_hci layout chuẩn Linux (tránh <bluetooth/hci.h>).
struct SockAddrHci {
  uint16_t hci_family;
  uint16_t hci_dev;
  uint16_t hci_channel;
};

int64_t monoMs() {
  struct timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

bool execQuiet(const std::string &cmd) {
  // Lệnh cố định, không nội suy input user → không RCE.
  int rc = system((cmd + " >/dev/null 2>&1").c_str());
  (void)rc;
  return true;
}
} // namespace

std::string BleLinkManager::ownShortId() {
  std::string fp = Config::instance().fingerprint();
  return fp.size() >= 8 ? fp.substr(0, 8) : std::string("00000000");
}

bool BleLinkManager::hciPresent() {
  return access("/sys/class/bluetooth/hci0", F_OK) == 0;
}

bool BleLinkManager::hciCmd(int fd, uint8_t ogf, uint16_t ocf,
                            const uint8_t *params, uint8_t plen) {
  uint8_t pkt[4 + 32];
  pkt[0] = 0x01; // HCI command packet
  uint16_t opcode = (uint16_t)(((uint16_t)ogf << 10) | ocf);
  pkt[1] = (uint8_t)(opcode & 0xFF);
  pkt[2] = (uint8_t)((opcode >> 8) & 0xFF);
  pkt[3] = plen;
  if (plen && params)
    memcpy(pkt + 4, params, plen);
  ssize_t n = write(fd, pkt, 4 + plen);
  return n == (ssize_t)(4 + plen);
}

void BleLinkManager::initAsync() {
  if (m_initStarted.exchange(true))
    return;
  std::thread([this]() {
    // 1. Bring-up hci0 nếu chưa có (hciattach tự daemonize).
    if (!hciPresent()) {
      Logger::info("BrickDrop BLE: bring-up hci0: " + s_bringup);
      // Chạy nền (hciattach không -n cũng tự daemonize).
      int brc = system((s_bringup + " >/dev/null 2>&1 &").c_str());
      (void)brc;
      for (int i = 0; i < 60 && !hciPresent(); ++i)
        usleep(100000);
    }
    if (!hciPresent()) {
      Logger::warn("BrickDrop BLE: không có hci0, tắt BLE (LAN-only)");
      return;
    }
    execQuiet("hciconfig hci0 up");
    usleep(300000);
    // 2. Mở HCI socket gắn vào hci0.
    if (!openHci()) {
      Logger::warn("BrickDrop BLE: không mở được HCI socket");
      return;
    }
    m_running.store(true);
    m_ready.store(true);
    Logger::info("BrickDrop BLE: sẵn sàng");
    worker();
  }).detach();
}

bool BleLinkManager::openHci() {
  // SOCK_CLOEXEC chỉ có trên Linux → dùng fcntl cho portable (macOS build).
  int fd = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
  if (fd < 0)
    return false;
  int dfd = fcntl(fd, F_GETFD, 0);
  if (dfd >= 0)
    fcntl(fd, F_SETFD, dfd | FD_CLOEXEC);
  SockAddrHci addr{};
  addr.hci_family = AF_BLUETOOTH;
  addr.hci_dev = 0; // hci0
  addr.hci_channel = 0;
  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(fd);
    return false;
  }
  // Non-blocking để poll điều khiển nhịp.
  int fl = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, fl | O_NONBLOCK);
  m_fd = fd;
  return true;
}

void BleLinkManager::shutdown() {
  m_running.store(false);
  if (m_fd >= 0) {
    uint8_t dis = 0;
    hciCmd(m_fd, 0x08, 0x000C, &dis, 1); // scan off
    hciCmd(m_fd, 0x08, 0x000A, &dis, 1); // adv off
    close(m_fd);
    m_fd = -1;
  }
  m_ready.store(false);
}

void BleLinkManager::advertiseIdle() {
  std::lock_guard<std::mutex> l(m_mtx);
  m_advFlags = 0;
  m_advTarget.clear();
  m_advDirty = true;
}

void BleLinkManager::advertiseWantLink(const std::string &targetShortId) {
  std::lock_guard<std::mutex> l(m_mtx);
  m_advFlags = BLE_F_WANT_LINK;
  m_advTarget = targetShortId;
  m_advDirty = true;
}

void BleLinkManager::advertiseAccept(const std::string &targetShortId) {
  std::lock_guard<std::mutex> l(m_mtx);
  m_advFlags = BLE_F_ACCEPT;
  m_advTarget = targetShortId;
  m_advDirty = true;
}

void BleLinkManager::setHosting(bool hosting) {
  std::lock_guard<std::mutex> l(m_mtx);
  if (m_hosting != hosting) {
    m_hosting = hosting;
    m_advDirty = true;
  }
}

std::vector<BlePeer> BleLinkManager::peers() {
  std::vector<BlePeer> out;
  std::string mine = ownShortId();
  int64_t now = monoMs();
  std::lock_guard<std::mutex> l(m_mtx);
  for (auto &kv : m_peers) {
    const BlePeer &p = kv.second;
    if (p.shortId == mine)
      continue;
    if (now - p.lastSeenMs > 15000)
      continue; // stale
    out.push_back(p);
  }
  return out;
}

std::string BleLinkManager::takeLinkRequest() {
  std::lock_guard<std::mutex> l(m_mtx);
  if (m_incomingReq.empty())
    return "";
  if (monoMs() - m_incomingReqMs > 10000) {
    m_incomingReq.clear();
    return "";
  }
  std::string r = m_incomingReq;
  m_incomingReq.clear();
  return r;
}

// Program ADV params + data + scan response. Chỉ worker thread gọi.
void BleLinkManager::programAdv() {
  uint8_t flags;
  std::string target;
  bool hosting;
  bool linkActive;
  {
    std::lock_guard<std::mutex> l(m_mtx);
    flags = m_advFlags;
    target = m_advTarget;
    hosting = m_hosting;
    linkActive = m_linkActive.load();
    m_advDirty = false;
  }
  uint8_t f = flags | (hosting ? BLE_F_IS_AP : 0);
  std::string sid = ownShortId();
  std::string tgt = target.size() >= 8 ? target.substr(0, 8) : "00000000";

  // LE Set Advertising Parameters (OGF 0x08, OCF 0x0006), 15 bytes.
  // Handshake: interval 100ms cho nhanh; bình thường 1s cho đỡ tốn pin.
  uint16_t iv = linkActive ? 0x00A0 : 0x0640;
  uint8_t ap[15] = {0};
  ap[0] = (uint8_t)(iv & 0xFF);
  ap[1] = (uint8_t)((iv >> 8) & 0xFF);
  ap[2] = (uint8_t)(iv & 0xFF);
  ap[3] = (uint8_t)((iv >> 8) & 0xFF);
  ap[4] = 0x00;             // ADV_IND
  ap[5] = 0x00;             // public addr
  ap[12] = 0x07;            // all 3 channels
  hciCmd(m_fd, 0x08, 0x0006, ap, sizeof(ap));

  // LE Set Advertising Data (OCF 0x0008): len + 31 bytes.
  uint8_t d[31] = {0};
  int i = 0;
  d[i++] = 0x02;
  d[i++] = 0x01;
  d[i++] = 0x06; // Flags AD: LE General Disc, BR/EDR not supported
  d[i++] = 0x15; // manuf len = 21
  d[i++] = 0xFF; // Manufacturer Specific
  d[i++] = 'B';
  d[i++] = 'D';
  d[i++] = 0x01; // protocol ver
  memcpy(d + i, sid.data(), 8);
  i += 8;
  d[i++] = f;
  memcpy(d + i, tgt.data(), 8);
  i += 8;
  uint8_t p[32] = {0};
  p[0] = (uint8_t)i;
  memcpy(p + 1, d, (size_t)i);
  hciCmd(m_fd, 0x08, 0x0008, p, (uint8_t)(i + 1));

  // LE Set Scan Response Data (OCF 0x0009): complete local name = alias.
  std::string alias = Config::instance().alias();
  if (alias.size() > 24)
    alias.resize(24);
  uint8_t sr[32] = {0};
  sr[0] = (uint8_t)(alias.size() + 1);
  sr[1] = 0x09;
  memcpy(sr + 2, alias.data(), alias.size());
  hciCmd(m_fd, 0x08, 0x0009, sr, (uint8_t)(alias.size() + 2));

  uint8_t en = 1;
  hciCmd(m_fd, 0x08, 0x000A, &en, 1); // LE Set Advertise Enable
}

void BleLinkManager::setScan(bool on) {
  // LE Set Scan Parameters (OCF 0x000B): active scan để lấy scan response.
  uint8_t sp[7] = {0x01, 0x80, 0x00, 0x40, 0x00, 0x00, 0x00};
  hciCmd(m_fd, 0x08, 0x000B, sp, sizeof(sp));
  // LE Set Scan Enable (OCF 0x000C): enable + filter duplicates.
  uint8_t se[2] = {(uint8_t)(on ? 1 : 0), 0x01};
  hciCmd(m_fd, 0x08, 0x000C, se, sizeof(se));
}

void BleLinkManager::onHciEvent(const uint8_t *pkt, size_t len) {
  if (len < 3 || pkt[0] != 0x04)
    return; // không phải HCI event
  if (pkt[1] != 0x3E)
    return; // không phải LE Meta Event
  const uint8_t *p = pkt + 3;
  size_t plen = pkt[2];
  if (plen < 2 || p[0] != 0x02)
    return; // không phải LE Advertising Report
  uint8_t nrep = p[1];
  p += 2;
  const uint8_t *end = pkt + len;
  for (int r = 0; r < nrep; ++r) {
    // evt_type(1) addr_type(1) addr(6) data_len(1) data(N) rssi(1)
    if (p + 10 > end)
      break;
    uint8_t dlen = p[9];
    if (p + 10 + dlen + 1 > end)
      break;
    int8_t rssi = (int8_t)p[10 + dlen];
    parseAdv(p + 10, dlen, (int)rssi);
    p += 10 + dlen + 1;
  }
}

void BleLinkManager::parseAdv(const uint8_t *data, uint8_t dlen, int rssi) {
  std::string sid, target, alias;
  uint8_t flags = 0;
  bool ours = false;
  size_t i = 0;
  while (i + 2 <= (size_t)dlen) {
    uint8_t l = data[i];
    if (l == 0 || i + 1 + (size_t)l > (size_t)dlen)
      break;
    uint8_t type = data[i + 1];
    const uint8_t *v = data + i + 2;
    uint8_t vl = (uint8_t)(l - 1);
    if (type == 0xFF && vl >= 20 && v[0] == 'B' && v[1] == 'D' && v[2] == 0x01) {
      sid.assign((const char *)v + 3, 8);
      flags = v[11];
      target.assign((const char *)v + 12, 8);
      ours = true;
    } else if (type == 0x09 && vl > 0) {
      alias.assign((const char *)v, vl);
    }
    i += 1 + (size_t)l;
  }
  if (!ours)
    return;
  std::string mine = ownShortId();
  if (sid == mine)
    return; // chính mình (nghe lại ADV của mình)
  int64_t now = monoMs();
  std::lock_guard<std::mutex> l(m_mtx);
  // WANT_LINK nhắm vào mình → ghi lại để App xử lý (auto-accept theo
  // visibility/danh bạ).
  if ((flags & BLE_F_WANT_LINK) && target == mine) {
    m_incomingReq = sid;
    m_incomingReqMs = now;
  }
  BlePeer &pr = m_peers[sid];
  pr.shortId = sid;
  pr.flags = flags;
  pr.targetId = target;
  if (!alias.empty())
    pr.alias = alias;
  pr.rssi = rssi;
  pr.lastSeenMs = now;
}

void BleLinkManager::worker() {
  programAdv();
  // LE Set Scan Parameters 1 lần (setScan gọi lại mỗi lần bật/tắt).
  bool scanOn = false;
  int64_t lastToggle = 0;
  uint8_t buf[260];
  while (m_running.load()) {
    // Áp dụng ADV mới nếu có (programAdv tự clear dirty).
    bool dirty = false;
    {
      std::lock_guard<std::mutex> l(m_mtx);
      dirty = m_advDirty;
    }
    if (dirty)
      programAdv();
    // Duty-cycle scan: handshake/màn Gửi đi → mạnh; nền → nhẹ.
    int64_t now = monoMs();
    bool linkActive = m_linkActive.load();
    bool active = m_active.load();
    int onMs, offMs;
    if (linkActive) {
      onMs = 1000000;
      offMs = 0; // scan liên tục khi handshake
    } else if (active) {
      onMs = 2500;
      offMs = 1000;
    } else {
      onMs = 1500;
      offMs = 5000;
    }
    if (now - lastToggle >= (scanOn ? onMs : offMs)) {
      scanOn = !scanOn;
      // linkActive: không bao giờ tắt.
      if (linkActive)
        scanOn = true;
      lastToggle = now;
      setScan(scanOn);
    }
    struct pollfd pfd{};
    pfd.fd = m_fd;
    pfd.events = POLLIN;
    int pr = poll(&pfd, 1, 200);
    if (pr > 0 && (pfd.revents & POLLIN)) {
      ssize_t n = read(m_fd, buf, sizeof(buf));
      if (n > 0)
        onHciEvent(buf, (size_t)n);
    }
  }
}

} // namespace BrickDrop
