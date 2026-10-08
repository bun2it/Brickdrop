#include "UpdateManager.h"
#include "../core/Logger.h"
#include "../localsend/LocalSendProtocol.h"
#include <curl/curl.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <vector>

namespace BrickDrop {

UpdateManager &UpdateManager::instance() {
  static UpdateManager inst;
  return inst;
}

UpdateManager::~UpdateManager() { shutdown(); }

bool UpdateManager::init() {
  std::lock_guard<std::mutex> l(m_mutex);
  m_progress.state = OtaState::IDLE;
  m_hasUpdate = false;
  Logger::info(std::string("BrickDrop OTA: current v") + BRICKDROP_VERSION);
  return true;
}

void UpdateManager::shutdown() { cancelUpdate(); }

// Thư mục gốc app: /proc/self/exe = <root>/bin/brickdrop → lùi 2 cấp.
std::string UpdateManager::appRoot() {
  char buf[1024] = {0};
  ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n > 0) {
    std::string p(buf, (size_t)n);
    size_t s = p.find_last_of('/');
    if (s != std::string::npos) {
      p = p.substr(0, s); // <root>/bin
      s = p.find_last_of('/');
      if (s != std::string::npos)
        return p.substr(0, s); // <root>
    }
  }
  return "/mnt/SDCARD/Apps/BrickDrop"; // fallback TrimUI
}

bool UpdateManager::isVersionNewer(const std::string &remote,
                                   const std::string &current) {
  std::string r = remote, c = current;
  if (!r.empty() && (r.front() == 'v' || r.front() == 'V'))
    r.erase(0, 1);
  if (!c.empty() && (c.front() == 'v' || c.front() == 'V'))
    c.erase(0, 1);
  auto parts = [](const std::string &s) {
    std::vector<int> v;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, '.')) {
      try {
        v.push_back(std::stoi(item));
      } catch (...) {
        v.push_back(0);
      }
    }
    while (v.size() < 3)
      v.push_back(0);
    return v;
  };
  auto rp = parts(r), cp = parts(c);
  for (size_t i = 0; i < 3; ++i) {
    if (rp[i] > cp[i])
      return true;
    if (rp[i] < cp[i])
      return false;
  }
  return false;
}

namespace {
size_t curlWriteStr(void *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *s = (std::string *)userdata;
  s->append((const char *)ptr, size * nmemb);
  return size * nmemb;
}
size_t curlWriteFile(void *ptr, size_t size, size_t nmemb, void *userdata) {
  FILE *f = (FILE *)userdata;
  return fwrite(ptr, size, nmemb, f);
}
} // namespace

bool UpdateManager::httpGet(const std::string &url, std::string &outBody,
                            long timeoutSec) {
  CURL *ch = curl_easy_init();
  if (!ch)
    return false;
  std::string body;
  curl_easy_setopt(ch, CURLOPT_URL, url.c_str());
  curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, curlWriteStr);
  curl_easy_setopt(ch, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(ch, CURLOPT_CONNECTTIMEOUT, timeoutSec);
  curl_easy_setopt(ch, CURLOPT_TIMEOUT, timeoutSec + 10);
  curl_easy_setopt(ch, CURLOPT_USERAGENT, "BrickDrop-OTA/1.0");
  curl_easy_setopt(ch, CURLOPT_HTTPHEADER, nullptr);
  struct curl_slist *hdrs = nullptr;
  hdrs = curl_slist_append(hdrs, "Cache-Control: no-cache");
  curl_easy_setopt(ch, CURLOPT_HTTPHEADER, hdrs);
  CURLcode rc = curl_easy_perform(ch);
  long code = 0;
  curl_easy_getinfo(ch, CURLINFO_RESPONSE_CODE, &code);
  curl_slist_free_all(hdrs);
  curl_easy_cleanup(ch);
  if (rc != CURLE_OK || code != 200)
    return false;
  outBody = body;
  return true;
}

bool UpdateManager::checkForUpdatesSync(OtaInfo &outInfo) {
  Logger::info("BrickDrop OTA: checking...");
  std::string url =
      std::string(OTA_MANIFEST_URL) + "?t=" + std::to_string(std::time(nullptr));
  std::string body;
  if (!httpGet(url, body, 8)) {
    Logger::info("BrickDrop OTA: no manifest (offline?)");
    return false;
  }
  OtaInfo info;
  LsJson::getString(body, "version", info.remoteVersion);
  LsJson::getString(body, "full_zip_url", info.fullZipUrl);
  LsJson::getString(body, "changelog", info.changelog);
  LsJson::getString(body, "release_date", info.releaseDate);
  LsJson::getString(body, "sha256", info.sha256);
  if (info.remoteVersion.empty())
    return false;
  if (info.fullZipUrl.empty()) {
    info.fullZipUrl = std::string("https://github.com/") + OTA_GITHUB_REPO +
                      "/releases/download/v" + info.remoteVersion +
                      "/BrickDrop-v" + info.remoteVersion + ".zip";
  }
  if (!isVersionNewer(info.remoteVersion, BRICKDROP_VERSION)) {
    std::lock_guard<std::mutex> l(m_mutex);
    m_progress.state = OtaState::UP_TO_DATE;
    return false;
  }
  {
    std::lock_guard<std::mutex> l(m_mutex);
    m_latestInfo = info;
    m_hasUpdate = true;
    m_progress.state = OtaState::UPDATE_AVAILABLE;
  }
  outInfo = info;
  Logger::info("BrickDrop OTA: update available v" + info.remoteVersion);
  return true;
}

void UpdateManager::checkForUpdatesAsync(
    std::function<void(bool, const OtaInfo &)> cb) {
  std::thread([this, cb]() {
    OtaInfo info;
    bool has = checkForUpdatesSync(info);
    if (cb)
      cb(has, info);
  }).detach();
}

bool UpdateManager::startUpdate(const OtaInfo &info) {
  bool expected = false;
  if (!m_isRunning.compare_exchange_strong(expected, true)) {
    Logger::warn("BrickDrop OTA: update already in progress");
    return false;
  }
  m_cancelRequested = false;
  {
    std::lock_guard<std::mutex> l(m_mutex);
    m_progress = OtaProgress();
    m_progress.state = OtaState::DOWNLOADING;
    m_progress.newVersion = info.remoteVersion;
    m_progress.currentStep = "Đang tải bản cập nhật v" + info.remoteVersion + "...";
  }
  if (m_workerThread.joinable())
    m_workerThread.join();
  m_workerThread = std::thread(&UpdateManager::runDownloadWorker, this, info);
  return true;
}

void UpdateManager::cancelUpdate() {
  m_cancelRequested = true;
  if (m_workerThread.joinable())
    m_workerThread.join();
  m_isRunning = false;
}

OtaProgress UpdateManager::getProgress() const {
  std::lock_guard<std::mutex> l(m_mutex);
  return m_progress;
}

OtaInfo UpdateManager::getLatestInfo() const {
  std::lock_guard<std::mutex> l(m_mutex);
  return m_latestInfo;
}

void UpdateManager::reportDownload(uint64_t done, uint64_t total, double pct) {
  std::lock_guard<std::mutex> l(m_mutex);
  // Chỉ cập nhật khi tăng ít nhất 1% để đỡ spam.
  if (pct >= m_progress.progressPct + 1.0 || pct >= 100.0) {
    m_progress.bytesDownloaded = done;
    m_progress.totalBytes = total;
    m_progress.progressPct = pct;
  }
}

namespace {
struct DlCtx {
  UpdateManager *um;
  uint64_t *outSize;
};
int dlXfer(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
           curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
  DlCtx *ctx = (DlCtx *)clientp;
  if (ctx->um->cancelRequested())
    return 1;
  // Cập nhật % tải theo mốc 5% (đỡ spam mutex).
  if (dltotal > 0) {
    double pct = (double)dlnow * 100.0 / (double)dltotal;
    ctx->um->reportDownload(dlnow, dltotal, pct);
  }
  return 0;
}
} // namespace

bool UpdateManager::downloadFile(const std::string &url,
                                 const std::string &destPath, uint64_t *outSize,
                                 bool trackProgress) {
  FILE *f = fopen(destPath.c_str(), "wb");
  if (!f)
    return false;
  CURL *ch = curl_easy_init();
  if (!ch) {
    fclose(f);
    return false;
  }
  DlCtx ctx{this, outSize};
  curl_easy_setopt(ch, CURLOPT_URL, url.c_str());
  curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, curlWriteFile);
  curl_easy_setopt(ch, CURLOPT_WRITEDATA, f);
  curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(ch, CURLOPT_CONNECTTIMEOUT, 15L);
  curl_easy_setopt(ch, CURLOPT_TIMEOUT, 0L); // tải lớn: không timeout cứng
  curl_easy_setopt(ch, CURLOPT_USERAGENT, "BrickDrop-OTA/1.0");
  curl_easy_setopt(ch, CURLOPT_NOPROGRESS, trackProgress ? 0L : 1L);
  if (trackProgress) {
    curl_easy_setopt(ch, CURLOPT_XFERINFOFUNCTION, dlXfer);
    curl_easy_setopt(ch, CURLOPT_XFERINFODATA, &ctx);
  }
  CURLcode rc = curl_easy_perform(ch);
  long code = 0;
  curl_off_t dl = 0;
  curl_easy_getinfo(ch, CURLINFO_RESPONSE_CODE, &code);
  curl_easy_getinfo(ch, CURLINFO_SIZE_DOWNLOAD_T, &dl);
  curl_easy_cleanup(ch);
  fclose(f);
  if (outSize)
    *outSize = (uint64_t)dl;
  if (rc != CURLE_OK || code < 200 || code >= 300) {
    unlink(destPath.c_str());
    return false;
  }
  // Cập nhật tiến trình lần cuối.
  if (trackProgress) {
    std::lock_guard<std::mutex> l(m_mutex);
    m_progress.bytesDownloaded = (uint64_t)dl;
    m_progress.totalBytes = (uint64_t)dl;
    m_progress.progressPct = 100.0;
  }
  return true;
}

static uint64_t otaFreeBytes(const std::string &path) {
  struct statvfs sv;
  if (::statvfs(path.c_str(), &sv) != 0)
    return 0;
  return (uint64_t)sv.f_bavail * (uint64_t)sv.f_frsize;
}

void UpdateManager::runDownloadWorker(OtaInfo info) {
  Logger::info("BrickDrop OTA: downloading v" + info.remoteVersion);
  std::string root = appRoot();
  std::string zipPath = root + "/ota_update.zip";

  uint64_t got = 0;
  if (!downloadFile(info.fullZipUrl, zipPath, &got, true)) {
    std::lock_guard<std::mutex> l(m_mutex);
    m_progress.state =
        m_cancelRequested ? OtaState::IDLE : OtaState::FAILED;
    if (m_progress.state == OtaState::FAILED)
      m_progress.errorMessage = "Tải bản cập nhật thất bại. Kiểm tra mạng!";
    m_isRunning = false;
    return;
  }
  // Zip < 1MB coi như lỗi.
  if (got < 1000000) {
    unlink(zipPath.c_str());
    std::lock_guard<std::mutex> l(m_mutex);
    m_progress.state = OtaState::FAILED;
    m_progress.errorMessage = "File tải về không hợp lệ.";
    m_isRunning = false;
    return;
  }
  // Verify SHA256 nếu manifest có.
  if (!info.sha256.empty()) {
    std::string sum = LsUtil::sha256OfFile(zipPath);
    if (sum != info.sha256) {
      unlink(zipPath.c_str());
      std::lock_guard<std::mutex> l(m_mutex);
      m_progress.state = OtaState::FAILED;
      m_progress.errorMessage = "Sai mã kiểm tra (sha256). Thử lại.";
      Logger::error("BrickDrop OTA: sha256 mismatch");
      m_isRunning = false;
      return;
    }
  }
  // Cần chỗ trống: 2× zip + 16MB.
  uint64_t need = got * 2 + 16ULL * 1024 * 1024;
  uint64_t freeB = otaFreeBytes(root);
  if (freeB > 0 && freeB < need) {
    unlink(zipPath.c_str());
    std::lock_guard<std::mutex> l(m_mutex);
    m_progress.state = OtaState::FAILED;
    m_progress.errorMessage = "Thẻ nhớ không đủ chỗ trống.";
    m_isRunning = false;
    return;
  }
  {
    std::lock_guard<std::mutex> l(m_mutex);
    m_progress.state = OtaState::INSTALLING;
    m_progress.currentStep = "Đang cài đặt v" + info.remoteVersion + "...";
    m_progress.progressPct = 100.0;
  }
  if (!installFullZip(zipPath, info)) {
    unlink(zipPath.c_str());
    std::lock_guard<std::mutex> l(m_mutex);
    if (m_progress.errorMessage.empty())
      m_progress.errorMessage = "Cài đặt thất bại.";
    m_progress.state = OtaState::FAILED;
    m_isRunning = false;
    return;
  }
  unlink(zipPath.c_str());
  {
    std::lock_guard<std::mutex> l(m_mutex);
    m_progress.state = OtaState::COMPLETED;
    m_progress.currentStep = "Cập nhật thành công!";
  }
  m_isRunning = false;
  Logger::info("BrickDrop OTA: completed v" + info.remoteVersion);
}

// Bung full-zip đè lên appRoot. Binary đang chạy được thay bằng rename
// (atomic) — cp trực tiếp sẽ dính ETXTBSY "Text file busy".
bool UpdateManager::installFullZip(const std::string &zipPath,
                                   const OtaInfo & /*info*/) {
  std::string root = appRoot();
  std::string tmpDir = root + "/.ota_tmp";
  auto setErr = [this](const std::string &msg) {
    std::lock_guard<std::mutex> l(m_mutex);
    m_progress.errorMessage = msg;
  };

  system(("rm -rf '" + tmpDir + "' 2>/dev/null").c_str());
  mkdir(tmpDir.c_str(), 0755);

  // 1. Bung zip vào thư mục tạm (unzip → fallback busybox unzip).
  int rc = system(("unzip -o '" + zipPath + "' 'Apps/BrickDrop/*' -d '" +
                   tmpDir + "' >/dev/null 2>&1")
                      .c_str());
  std::string staged = tmpDir + "/Apps/BrickDrop";
  std::string stagedBin = staged + "/bin/brickdrop";
  struct stat st{};
  bool stagedOk =
      (stat(stagedBin.c_str(), &st) == 0 && (uint64_t)st.st_size > 1000000);
  if (!stagedOk && rc != 0) {
    rc = system(("busybox unzip -o '" + zipPath + "' 'Apps/BrickDrop/*' -d '" +
                 tmpDir + "' >/dev/null 2>&1")
                    .c_str());
    stagedOk =
        (stat(stagedBin.c_str(), &st) == 0 && (uint64_t)st.st_size > 1000000);
  }
  if (!stagedOk) {
    system(("rm -rf '" + tmpDir + "' 2>/dev/null").c_str());
    setErr("Giải nén bản cập nhật thất bại.");
    Logger::error("BrickDrop OTA: unzip failed");
    return false;
  }
  if (rc != 0)
    Logger::warn("BrickDrop OTA: unzip warning (exit != 0) nhưng đủ file");

  // 2. Chép đè các file tĩnh (assets, launch.sh, config.json, icon).
  // Trừ bin/brickdrop — xử lý riêng bằng rename ở bước 3.
  rc = system(("cd '" + staged + "' && cp -a assets launch.sh config.json "
                                  "icon.png '" +
               root + "/' 2>/dev/null")
                  .c_str());
  if (rc != 0) {
    // Thử từng món (có thể thiếu icon.png).
    system(("cp -a '" + staged + "/assets/.' '" + root + "/assets/' "
                         "2>/dev/null")
               .c_str());
    system(("cp -a '" + staged + "/launch.sh' '" + root + "/launch.sh' "
                         "2>/dev/null")
               .c_str());
    system(("cp -a '" + staged + "/config.json' '" + root + "/config.json' "
                         "2>/dev/null")
               .c_str());
    system(("cp -a '" + staged + "/icon.png' '" + root + "/icon.png' "
                         "2>/dev/null")
               .c_str());
  }

  // 3. Thay binary đang chạy bằng rename (atomic, không ETXTBSY).
  std::string newBin = root + "/bin/brickdrop.new";
  rc = system(("cp -a '" + stagedBin + "' '" + newBin + "' && chmod +x '" +
               newBin + "'").c_str());
  if (rc != 0) {
    system(("rm -rf '" + tmpDir + "' 2>/dev/null").c_str());
    setErr("Chép binary mới thất bại.");
    return false;
  }
  if (rename(newBin.c_str(), (root + "/bin/brickdrop").c_str()) != 0) {
    system(("rm -rf '" + tmpDir + "' 2>/dev/null").c_str());
    setErr("Thay binary mới thất bại.");
    return false;
  }

  system(("rm -rf '" + tmpDir + "' 2>/dev/null").c_str());
  system(("chmod +x '" + root + "/bin/brickdrop' '" + root + 
          "/launch.sh' 2>/dev/null")
             .c_str());
  sync();

  // 4. Verify binary mới.
  std::string finalBin = root + "/bin/brickdrop";
  if (stat(finalBin.c_str(), &st) != 0 || (uint64_t)st.st_size < 1000000) {
    setErr("Binary sau cập nhật không hợp lệ.");
    return false;
  }
  Logger::info("BrickDrop OTA: installed (" + std::to_string(st.st_size) +
               " bytes)");
  return true;
}

} // namespace BrickDrop
