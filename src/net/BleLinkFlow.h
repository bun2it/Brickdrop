#pragma once
// BrickDrop BleLinkFlow — điều phối Phase D: BLE handshake connectionless
// → dựng Wi-Fi link → gửi file. App gọi frame() mỗi frame và
// userSelectBlePeer() khi user bấm vào 1 máy chỉ thấy qua BLE.
//
// Handshake (không cần GATT):
//   A (initiator) phát ADV WANT_LINK nhắm vào B → B thấy, kiểm tra
//   visibility/danh bạ → B phát ADV ACCEPT nhắm vào A → cả hai bầu AP
//   deterministic: shortId nhỏ hơn làm Soft-AP (SSID "BrickDrop-<8hex>"),
//   máy còn lại scan + join đúng SSID đó (verify shortId trong tên sóng).
// Verify cuối: fingerprint đầy đủ qua discovery LocalSend trên link Wi-Fi.
#include "../localsend/LocalSendProtocol.h"
#include "BleLinkManager.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace BrickDrop {

class BleLinkFlow {
public:
  struct Cbs {
    std::function<void(const std::string &)> toast;
    std::function<void()> restartService;
    // Bắt đầu gửi file/thư mục đã stage tới thiết bị LAN (code gửi cũ).
    std::function<void(const LsDeviceInfo &)> startSend;
    std::function<std::vector<LsDeviceInfo>()> lanDevices;
    // shortId (8 hex) có trong danh bạ không (visibility=Danh bạ).
    std::function<bool(const std::string &)> isContactShortId;
    std::function<int()> visibility; // 0=Mọi người, 1=Danh bạ, 2=Tắt
  };

  void init(Cbs cbs);
  void shutdown();
  // Gọi mỗi frame. sendScreenActive=true khi đang ở màn "Gửi đi".
  void frame(bool sendScreenActive);
  // User bấm A vào 1 máy BLE trong danh sách.
  void userSelectBlePeer(const std::string &shortId);

  // Peers BLE chưa có trên LAN (để UI vẽ thêm hàng "Gần bạn").
  std::vector<BlePeer> bleOnlyPeers();
  bool busy() const { return m_state != State::IDLE; }
  std::string status() const { return m_status; }

private:
  enum class State { IDLE, WAIT_ACCEPT, WAIT_AP_ADV, JOINING, WAIT_PEER };
  void fail(const std::string &msg);
  void reset();
  // Bầu vai trò + bắt đầu dựng Wi-Fi (chạy 1 lần cho cả initiator/acceptor).
  void beginRole();
  void startJoinThread();
  bool findLanPeer(LsDeviceInfo &out);

  Cbs m_cbs;
  bool m_hasCbs = false;
  State m_state = State::IDLE;
  std::string m_peer;  // shortId 8 hex của máy kia
  bool m_initiator = false;
  int64_t m_deadlineMs = 0;
  std::string m_status;
  // Op Wi-Fi nền:
  bool m_wifiWorking = false;
  bool m_wifiOk = false;
  std::string m_wifiErr;
};

} // namespace BrickDrop
