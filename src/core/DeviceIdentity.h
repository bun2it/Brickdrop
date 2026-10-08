#pragma once
// BrickDrop DeviceIdentity — ID thiết bị ổn định từ phần cứng.
// Port gọn từ RomCloud diagnostics/DeviceIdentity: SHA-256(domain + rawId),
// ưu tiên MAC → machine-id → install ID (persist). KHÔNG bao giờ gửi MAC thô,
// chỉ gửi hash. Domain khác RomCloud để khỏi tương quan.
#include <string>

namespace BrickDrop {

class DeviceIdentity {
public:
  static DeviceIdentity &instance();
  // 64 hex SHA256("BrickDrop-v1:" + hardwareId). Ổn định qua các lần chạy.
  std::string deviceId();
  // Dạng ngắn để hiển thị: "BD-" + 8 hex đầu.
  std::string shortId();
  // Nguồn ID: "mac" | "machine_id" | "install_id".
  std::string idSource();

private:
  DeviceIdentity() = default;
  bool m_init = false;
  std::string m_deviceId;
  std::string m_source;
  static std::string tryMacAddress();
  static std::string tryMachineId();
  static std::string getOrCreateInstallId();
  static std::string hashWithDomain(const std::string &rawId);
  static bool isValidHardwareId(const std::string &id);
  static std::string trimLower(const std::string &s);
};

} // namespace BrickDrop
