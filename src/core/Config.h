#pragma once
// BrickDrop Config — thay AppConfig RomCloud: alias, fingerprint, save dir,
// network iface. Persist dưới <sdRoot>/.brickdrop/ (device) hoặc dir chạy (PC).
#include <cstdint>
#include <string>
#include <vector>

namespace BrickDrop {

class Config {
public:
  static Config &instance();

  // Gọi 1 lần ở main: phát hiện sdRoot (/mnt/SDCARD nếu có, else $HOME, else cwd).
  void init();
  const std::string &sdRoot() const { return m_sdRoot; }
  std::string stateDir(); // <sdRoot>/.brickdrop (tự tạo)

  std::string alias();
  void setAlias(const std::string &a);
  void regenerateAlias(); // random mới, không cần gõ phím
  std::string fingerprint(); // 64 hex SHA256, ổn định từ phần cứng
                             // (DeviceIdentity: MAC → machine-id → install ID)
  std::string saveDir();     // nơi nhận file, default <sdRoot>/BrickDrop
  void setSaveDir(const std::string &d);

  // Iface có IP ưu tiên cho LocalSend: wlan1 (AP/GO) > wlan0 (STA) > p2p* > "".
  std::string netInterface();

  // Chế độ hiển thị kiểu AirDrop: 0=Mọi người, 1=Đã lưu, 2=Tắt.
  int visibility();
  void setVisibility(int v);

  struct Contact {
    std::string fp;
    std::string alias;
    std::string ip;
    int port = 53317;
  };
  std::vector<Contact> contacts();
  bool addContact(const std::string &fp, const std::string &alias,
                  const std::string &ip, int port);
  bool removeContact(const std::string &fp);
  bool isContact(const std::string &fp);
  // Cập nhật ip/port của contact đã lưu (IP đổi khi qua mạng khác).
  // Trả true nếu có thay đổi (để caller sync lại trusted peers).
  bool updateContactNet(const std::string &fp, const std::string &ip, int port);

  // Preset gửi: file/folder đã chọn sẵn để chờ gửi (persist qua lần mở app).
  std::string sendPresetFile();
  void setSendPresetFile(const std::string &p);
  std::string sendPresetFolder();
  void setSendPresetFolder(const std::string &p);

  static constexpr const char *kGroupPrefix = "BrickDrop-";
  static constexpr const char *kGroupPsk = "brickdrop1";
  static constexpr const char *kGroupIp = "192.168.49.1";
  static constexpr const char *kDeviceModel = "TrimUI Brick Pro";

private:
  Config() = default;
  std::string m_sdRoot;
  std::string m_alias;
  std::string m_saveDir;
  bool m_loaded = false;
  void load();
  static std::string readFile1(const std::string &p);
  static void writeFile1(const std::string &p, const std::string &s);
};

} // namespace BrickDrop
