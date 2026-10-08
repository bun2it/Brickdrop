#pragma once
// BrickDrop OTA — kiểm tra + tải + cài bản cập nhật từ GitHub releases.
// Port gọn từ RomCloud src/ota/UpdateManager (bỏ OS bundles/mpv deps).
//
// An toàn cài đặt: binary đang chạy được thay bằng rename (atomic),
// KHÔNG cp trực tiếp (kernel báo ETXTBSY "Text file busy" — đã verify).
// Dữ liệu user (.brickdrop/, thư mục nhận file) nằm ngoài app dir nên
// OTA đè lên app không mất gì.
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace BrickDrop {

#ifndef BRICKDROP_VERSION
#define BRICKDROP_VERSION "0.0.0-dev"
#endif
constexpr const char *OTA_GITHUB_REPO = "bun2it/Brickdrop";
constexpr const char *OTA_MANIFEST_URL =
    "https://raw.githubusercontent.com/bun2it/Brickdrop/main/version.json";
// App thoát với code này sau khi OTA xong → launch.sh chạy lại bản mới.
constexpr int OTA_RESTART_EXIT_CODE = 42;

enum class OtaState {
  IDLE,
  CHECKING,
  UPDATE_AVAILABLE,
  UP_TO_DATE,
  DOWNLOADING,
  INSTALLING,
  COMPLETED,
  FAILED
};

struct OtaInfo {
  std::string remoteVersion;
  std::string fullZipUrl;
  std::string changelog;
  std::string releaseDate;
  std::string sha256;
};

struct OtaProgress {
  OtaState state = OtaState::IDLE;
  uint64_t bytesDownloaded = 0;
  uint64_t totalBytes = 0;
  double progressPct = 0.0;
  std::string errorMessage;
  std::string newVersion;
  std::string currentStep;
};

class UpdateManager {
public:
  static UpdateManager &instance();

  bool init();
  void shutdown();

  // Kiểm tra manifest OTA (nền). Callback chạy trên thread nền.
  void checkForUpdatesAsync(
      std::function<void(bool hasUpdate, const OtaInfo &info)> cb = nullptr);
  bool checkForUpdatesSync(OtaInfo &outInfo);

  // Tải + cài (nền). Tiến trình xem qua getProgress().
  bool startUpdate(const OtaInfo &info);
  void cancelUpdate();

  OtaProgress getProgress() const;
  bool isUpdateAvailable() const { return m_hasUpdate.load(); }
  bool cancelRequested() const { return m_cancelRequested.load(); }
  // Cho curl progress callback cập nhật % (gọi từ worker thread).
  void reportDownload(uint64_t done, uint64_t total, double pct);
  OtaInfo getLatestInfo() const;
  std::string getCurrentVersion() const { return BRICKDROP_VERSION; }

  // Thư mục gốc của app (/mnt/SDCARD/Apps/BrickDrop), suy từ /proc/self/exe.
  static std::string appRoot();
  static bool isVersionNewer(const std::string &remote,
                             const std::string &current);

private:
  UpdateManager() = default;
  ~UpdateManager();

  void runDownloadWorker(OtaInfo info);
  bool installFullZip(const std::string &zipPath, const OtaInfo &info);
  bool downloadFile(const std::string &url, const std::string &destPath,
                    uint64_t *outSize, bool trackProgress);
  bool httpGet(const std::string &url, std::string &outBody, long timeoutSec);

  mutable std::mutex m_mutex;
  std::atomic<bool> m_isRunning{false};
  std::atomic<bool> m_cancelRequested{false};
  std::atomic<bool> m_hasUpdate{false};
  OtaInfo m_latestInfo;
  OtaProgress m_progress;
  std::thread m_workerThread;
};

} // namespace BrickDrop
