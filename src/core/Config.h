#pragma once
// BrickDrop Config — thay AppConfig RomCloud: alias, fingerprint, save dir,
// network iface. Persist dưới <sdRoot>/.brickdrop/ (device) hoặc dir chạy (PC).
#include <cstdint>
#include <string>

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
  std::string fingerprint(); // 32 hex, tạo 1 lần
  std::string saveDir();     // nơi nhận file, default <sdRoot>/BrickDrop
  void setSaveDir(const std::string &d);

  // Iface có IP ưu tiên cho LocalSend: wlan1 (AP/GO) > wlan0 (STA) > p2p* > "".
  std::string netInterface();

  static constexpr const char *kGroupPrefix = "BrickDrop-";
  static constexpr const char *kGroupPsk = "brickdrop1";
  static constexpr const char *kGroupIp = "192.168.49.1";
  static constexpr const char *kDeviceModel = "TrimUI Brick Pro";

private:
  Config() = default;
  std::string m_sdRoot;
  std::string m_alias;
  std::string m_fp;
  std::string m_saveDir;
  bool m_loaded = false;
  void load();
  static std::string readFile1(const std::string &p);
  static void writeFile1(const std::string &p, const std::string &s);
  static std::string randomHex(int n);
};

} // namespace BrickDrop
