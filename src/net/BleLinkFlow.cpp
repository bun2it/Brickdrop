#include "BleLinkFlow.h"
#include "../core/Config.h"
#include "../core/Logger.h"
#include "WifiDirectManager.h"
#include <ctime>
#include <thread>

namespace BrickDrop {

namespace {
int64_t monoMs() {
  struct timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
} // namespace

void BleLinkFlow::init(Cbs cbs) {
  m_cbs = std::move(cbs);
  m_hasCbs = true;
}

void BleLinkFlow::shutdown() { reset(); }

void BleLinkFlow::reset() {
  m_state = State::IDLE;
  m_peer.clear();
  m_initiator = false;
  m_status.clear();
  m_wifiWorking = false;
  m_wifiOk = false;
  m_wifiErr.clear();
  BleLinkManager::instance().setLinkActive(false);
  BleLinkManager::instance().advertiseIdle();
}

void BleLinkFlow::fail(const std::string &msg) {
  if (m_hasCbs && m_cbs.toast)
    m_cbs.toast(msg);
  Logger::warn("BrickDrop BLE link fail: " + msg);
  reset();
}

void BleLinkFlow::userSelectBlePeer(const std::string &shortId) {
  if (m_state != State::IDLE || shortId.size() < 8)
    return;
  if (!BleLinkManager::instance().ready()) {
    fail("Bluetooth chưa sẵn sàng");
    return;
  }
  m_peer = shortId.substr(0, 8);
  m_initiator = true;
  auto &ble = BleLinkManager::instance();
  ble.setLinkActive(true);
  ble.advertiseWantLink(m_peer);
  m_state = State::WAIT_ACCEPT;
  m_deadlineMs = monoMs() + 15000;
  m_status = "Đang gửi yêu cầu tới BD-" + m_peer + "...";
  Logger::info("BrickDrop BLE: request link -> " + m_peer);
}

std::vector<BlePeer> BleLinkFlow::bleOnlyPeers() {
  std::vector<BlePeer> out;
  if (!m_hasCbs)
    return out;
  auto peers = BleLinkManager::instance().peers();
  auto lan = m_cbs.lanDevices ? m_cbs.lanDevices() : std::vector<LsDeviceInfo>{};
  for (auto &p : peers) {
    bool onLan = false;
    for (auto &d : lan) {
      if (d.fingerprint.size() >= 8 && d.fingerprint.substr(0, 8) == p.shortId) {
        onLan = true;
        break;
      }
    }
    if (!onLan)
      out.push_back(p);
  }
  return out;
}

bool BleLinkFlow::findLanPeer(LsDeviceInfo &out) {
  if (!m_cbs.lanDevices)
    return false;
  for (auto &d : m_cbs.lanDevices()) {
    if (d.fingerprint.size() >= 8 && d.fingerprint.substr(0, 8) == m_peer) {
      out = d;
      return true;
    }
  }
  return false;
}

// Bầu vai trò deterministic: shortId nhỏ hơn làm Soft-AP.
// Cả hai máy cùng tính ra 1 kết quả → không cần thương lượng thêm.
void BleLinkFlow::beginRole() {
  std::string mine = BleLinkManager::ownShortId();
  bool iAmAp = mine < m_peer;
  if (iAmAp) {
    Logger::info("BrickDrop BLE: tôi làm AP (" + mine + " < " + m_peer + ")");
    m_status = "Đang dựng Wi-Fi...";
    m_wifiWorking = true;
    m_wifiOk = false;
    m_wifiErr.clear();
    std::thread([this, mine]() {
      std::string msg;
      // SSID nhúng shortId để bên join verify đúng máy đã handshake.
      std::string ssid = WifiDirectManager::instance().startGroupWithTag(mine, msg);
      if (!ssid.empty()) {
        BleLinkManager::instance().setHosting(true);
        if (m_hasCbs && m_cbs.restartService)
          m_cbs.restartService();
        m_wifiOk = true;
      } else {
        m_wifiErr = msg;
      }
      m_wifiWorking = false;
    }).detach();
    m_state = State::WAIT_PEER;
    m_deadlineMs = monoMs() + 45000;
  } else {
    Logger::info("BrickDrop BLE: tôi join AP của " + m_peer);
    m_status = "Chờ BD-" + m_peer + " phát Wi-Fi...";
    m_state = State::WAIT_AP_ADV;
    m_deadlineMs = monoMs() + 30000;
  }
}

void BleLinkFlow::startJoinThread() {
  m_state = State::JOINING;
  m_status = "Đang vào Wi-Fi của BD-" + m_peer + "...";
  m_deadlineMs = monoMs() + 40000;
  m_wifiWorking = true;
  m_wifiOk = false;
  m_wifiErr.clear();
  std::string peer = m_peer;
  std::thread([this, peer]() {
    std::string want = std::string(Config::kGroupPrefix) + peer;
    std::string found;
    // Quét vài vòng cho chắc (AP vừa dựng có thể chưa hiện ngay).
    for (int t = 0; t < 3 && found.empty(); ++t) {
      auto groups = WifiDirectManager::instance().scanGroups();
      for (auto &g : groups) {
        if (g.ssid == want) {
          found = g.ssid;
          break;
        }
      }
    }
    std::string msg;
    bool ok = false;
    if (!found.empty())
      ok = WifiDirectManager::instance().joinGroup(found, msg);
    else
      msg = "Không thấy Wi-Fi của BD-" + peer;
    if (ok) {
      if (m_hasCbs && m_cbs.restartService)
        m_cbs.restartService();
      m_wifiOk = true;
    } else {
      m_wifiErr = msg;
    }
    m_wifiWorking = false;
  }).detach();
}

void BleLinkFlow::frame(bool sendScreenActive) {
  auto &ble = BleLinkManager::instance();
  ble.setActive(sendScreenActive);
  if (!m_hasCbs || !ble.ready())
    return;
  int64_t now = monoMs();

  // --- Bên nhận: có máy nào WANT_LINK nhắm vào mình không? ---
  if (m_state == State::IDLE) {
    std::string req = ble.takeLinkRequest();
    if (!req.empty()) {
      int v = m_cbs.visibility ? m_cbs.visibility() : 0;
      bool ok = (v == 0) ||
                (v == 1 && m_cbs.isContactShortId && m_cbs.isContactShortId(req));
      if (ok) {
        m_peer = req;
        m_initiator = false;
        ble.setLinkActive(true);
        ble.advertiseAccept(req);
        if (m_cbs.toast)
          m_cbs.toast("BD-" + req + " muốn kết nối...");
        Logger::info("BrickDrop BLE: accept link <- " + req);
        beginRole();
      }
      // visibility=Tắt (2): bỏ qua lặng lẽ.
    }
    return;
  }

  // --- Bên khởi tạo: chờ ACCEPT ---
  if (m_state == State::WAIT_ACCEPT) {
    if (now > m_deadlineMs) {
      fail("Không kết nối được, thử lại");
      return;
    }
    std::string mine = BleLinkManager::ownShortId();
    for (auto &p : ble.peers()) {
      if (p.shortId == m_peer && (p.flags & BLE_F_ACCEPT) &&
          p.targetId == mine) {
        Logger::info("BrickDrop BLE: accept <- " + m_peer);
        beginRole();
        return;
      }
    }
    return;
  }

  // --- Bên join: chờ bit IS_AP trong ADV của máy AP ---
  if (m_state == State::WAIT_AP_ADV) {
    if (now > m_deadlineMs) {
      fail("Không kết nối được, thử lại");
      return;
    }
    for (auto &p : ble.peers()) {
      if (p.shortId == m_peer && (p.flags & BLE_F_IS_AP)) {
        startJoinThread();
        return;
      }
    }
    return;
  }

  // --- Đang join Wi-Fi: chờ thread nền ---
  if (m_state == State::JOINING) {
    if (m_wifiWorking)
      return;
    if (m_wifiOk) {
      m_state = State::WAIT_PEER;
      m_deadlineMs = now + 25000;
      m_status = "Đã vào Wi-Fi, đang tìm thiết bị...";
    } else {
      fail(m_wifiErr.empty() ? "Vào Wi-Fi thất bại" : m_wifiErr);
    }
    return;
  }

  // --- Chờ peer xuất hiện qua discovery LocalSend trên link mới ---
  if (m_state == State::WAIT_PEER) {
    // Bên AP: thread dựng AP có thể còn chạy.
    if (m_wifiWorking)
      return;
    // AP dựng lỗi (bên join luôn có m_wifiOk=true khi tới đây).
    if (!m_wifiOk) {
      fail(m_wifiErr.empty() ? "Dựng Wi-Fi thất bại" : m_wifiErr);
      return;
    }
    if (now > m_deadlineMs) {
      fail("Không thấy thiết bị sau khi nối Wi-Fi");
      return;
    }
    LsDeviceInfo dev;
    if (findLanPeer(dev)) {
      // Verify xong (fingerprint khớp shortId) → link OK.
      ble.setLinkActive(false);
      ble.advertiseIdle(); // giữ bit IS_AP nếu đang host
      if (m_initiator) {
        if (m_cbs.startSend)
          m_cbs.startSend(dev);
      } else if (m_cbs.toast) {
        m_cbs.toast("Đã kết nối với BD-" + m_peer);
      }
      Logger::info("BrickDrop BLE: link OK với " + m_peer);
      m_state = State::IDLE;
      m_peer.clear();
      m_status.clear();
      m_initiator = false;
    }
    return;
  }
}

} // namespace BrickDrop
