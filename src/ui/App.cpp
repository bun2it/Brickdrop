#include "App.h"
#include "../core/Config.h"
#include "../core/DeviceIdentity.h"
#include "../core/Logger.h"
#include "../input/Input.h"
#include "../localsend/LocalSendManager.h"
#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>
#include <thread>

namespace BrickDrop {

static const SDL_Color C_BG = {16, 20, 28, 255};
static const SDL_Color C_PANEL = {26, 33, 46, 255};
static const SDL_Color C_FOCUS = {0, 180, 216, 255};
static const SDL_Color C_TEXT = {230, 237, 245, 255};
static const SDL_Color C_DIM = {148, 163, 184, 255};
static const SDL_Color C_OK = {46, 204, 113, 255};
static const SDL_Color C_WARN = {241, 196, 15, 255};

static const int W = 1024, H = 768, HDR = 64, FTR = 53;

namespace {
// Tên chế độ hiển thị kiểu AirDrop (0=Mọi người, 1=Danh bạ, 2=Tắt).
const char *visName(int v) {
  if (v == 1)
    return "Danh bạ";
  if (v == 2)
    return "Tắt";
  return "Mọi người";
}
// Hint khi danh sách thiết bị trống — phải đúng flow từng chế độ hiển thị.
std::string devEmptyHint() {
  int v = Config::instance().visibility();
  if (v == 2)
    return "Chế độ hiển thị đang Tắt — bật Mọi người/Danh bạ ở HOME để tìm "
           "thiết bị";
  if (v == 1)
    return "Danh bạ trống — chuyển Hiển thị sang Mọi người ở HOME, tìm máy "
           "rồi nhấn X để thêm";
  return "Chưa phát hiện thiết bị nào — máy gần bạn sẽ tự hiện qua "
         "Bluetooth, hoặc vào cùng mạng WiFi rồi nhấn Y để quét";
}

// Đệ quy liệt kê file trong thư mục để gửi cả folder.
// out: (absPath, relDir) với relDir dạng "TenFolder/sub" (receiver lồng
// dưới thư mục nhận để dựng lại cấu trúc). Bỏ symlink (tránh vòng lặp).
// Trả false nếu vượt giới hạn (sâu >16 hoặc >2000 file).
bool collectFolderTree(const std::string &absDir, const std::string &relDir,
                       std::vector<std::pair<std::string, std::string>> &out,
                       uint64_t &totalBytes, int depth = 0) {
  if (depth > 16 || out.size() > 2000)
    return false;
  DIR *d = opendir(absDir.c_str());
  if (!d)
    return true; // thư mục không đọc được: bỏ qua
  struct dirent *de;
  while ((de = readdir(d)) != nullptr) {
    std::string n = de->d_name;
    if (n == "." || n == "..")
      continue;
    std::string abs = absDir + "/" + n;
    struct stat st{};
    if (lstat(abs.c_str(), &st) != 0)
      continue;
    if (S_ISLNK(st.st_mode))
      continue;
    if (S_ISDIR(st.st_mode)) {
      if (!collectFolderTree(abs, relDir + "/" + n, out, totalBytes,
                             depth + 1)) {
        closedir(d);
        return false;
      }
    } else if (S_ISREG(st.st_mode)) {
      out.push_back({abs, relDir});
      totalBytes += (uint64_t)st.st_size;
      if (out.size() > 2000) {
        closedir(d);
        return false;
      }
    }
  }
  closedir(d);
  return true;
}

// batchId cho 1 lần gửi thư mục: 32 hex random (duy nhất mỗi lần gửi).
std::string makeBatchId() {
  static const char hex[] = "0123456789abcdef";
  unsigned char buf[16] = {0};
  std::ifstream ur("/dev/urandom", std::ios::binary);
  if (ur)
    ur.read(reinterpret_cast<char *>(buf), sizeof(buf));
  std::string out;
  for (int i = 0; i < 16; ++i) {
    out += hex[(buf[i] >> 4) & 0xF];
    out += hex[buf[i] & 0xF];
  }
  return out;
}
} // namespace

bool App::init(SDL_Window *win, SDL_Renderer *ren) {
  (void)win;
  m_ren = ren;
  m_icons.init(m_ren); // PNG icons (fallback rect xám nếu thiếu assets)
  const char *cands[] = {"assets/fonts/NotoSans-Regular.ttf",
                         "/mnt/SDCARD/Apps/BrickDrop/assets/fonts/NotoSans-Regular.ttf",
                         "../assets/fonts/NotoSans-Regular.ttf", nullptr};
  std::string font;
  for (int i = 0; cands[i]; ++i) {
    SDL_RWops *r = SDL_RWFromFile(cands[i], "rb");
    if (r) {
      SDL_RWclose(r);
      font = cands[i];
      break;
    }
  }
  if (font.empty()) {
    Logger::error("BrickDrop: no font found");
    return false;
  }
  m_fTitle = TTF_OpenFont(font.c_str(), 44);
  m_fMain = TTF_OpenFont(font.c_str(), 28);
  m_fSmall = TTF_OpenFont(font.c_str(), 22);
  if (!m_fTitle || !m_fMain || !m_fSmall)
    return false;

  m_sendLs.open(Config::instance().sdRoot());
  m_recvLs.open(Config::instance().saveDir());
  m_recvLs.refreshDirsOnly(true);
  // Điều kiện của Tai: thoát app phải có internet lại ngay → lưu WiFi gốc
  // trước khi đụng vào hotspot.
  WifiDirectManager::instance().saveOriginalWifi();
  // Phase D: BLE handshake — tự bring-up hci0 dưới nền (thất bại thì app
  // vẫn chạy LAN-only bình thường).
  BleLinkManager::instance().initAsync();
  m_bleFlow.init({
      [this](const std::string &m) { toast(m); },
      [this]() { restartService(); },
      [this](const LsDeviceInfo &d) { startSendTo(d); },
      [this]() { return m_devices; },
      [](const std::string &sid8) {
        for (auto &c : Config::instance().contacts())
          if (c.fp.size() >= 8 && c.fp.substr(0, 8) == sid8)
            return true;
        return false;
      },
      []() { return Config::instance().visibility(); },
  });
  // OTA: kiểm tra bản mới dưới nền, im lặng khi offline/không có mạng.
  UpdateManager::instance().init();
  UpdateManager::instance().checkForUpdatesAsync(
      [this](bool has, const OtaInfo &info) {
        if (!has)
          return;
        std::lock_guard<std::mutex> l(m_otaMtx);
        m_otaAvailable = true;
        m_otaInfo = info;
      });
  // Khôi phục preset gửi (file/folder đã chọn từ lần mở app trước).
  {
    struct stat st{};
    std::string pf = Config::instance().sendPresetFile();
    if (!pf.empty() && stat(pf.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
      m_pickFile = pf;
    } else if (!pf.empty()) {
      Config::instance().setSendPresetFile(""); // file không còn → xóa preset
    }
    std::string pd = Config::instance().sendPresetFolder();
    if (!pd.empty() && stat(pd.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
      m_pickFolder = pd;
      std::string base = pd.substr(pd.find_last_of('/') + 1);
      collectFolderTree(pd, base, m_pickFolderFiles, m_pickFolderTotal);
    } else if (!pd.empty()) {
      Config::instance().setSendPresetFolder("");
    }
  }
  refreshDevices();

  auto &ls = LocalSendManager::instance();
  ls.setOnUserPrompt([this](const LsUploadRequest &r) {
    std::lock_guard<std::mutex> l(m_qmtx);
    if (m_prompts.size() < 8)
      m_prompts.push_back(r);
  });
  return true;
}

void App::shutdown() {
  UpdateManager::instance().shutdown();
  m_bleFlow.shutdown();
  BleLinkManager::instance().shutdown();
  m_icons.shutdown();
  if (m_fTitle)
    TTF_CloseFont(m_fTitle);
  if (m_fMain)
    TTF_CloseFont(m_fMain);
  if (m_fSmall)
    TTF_CloseFont(m_fSmall);
}

void App::toast(const std::string &msg, uint32_t ms) {
  m_toast.msg = msg;
  m_toast.until = SDL_GetTicks() + ms;
}

void App::drawText(const std::string &t, int x, int y, SDL_Color c, TTF_Font *f,
                   bool centered) {
  if (t.empty())
    return;
  SDL_Surface *s = TTF_RenderUTF8_Blended(f, t.c_str(), c);
  if (!s)
    return;
  SDL_Texture *tx = SDL_CreateTextureFromSurface(m_ren, s);
  SDL_Rect r = {x, y, s->w, s->h};
  if (centered) {
    r.x = x - s->w / 2;
  }
  SDL_FreeSurface(s);
  if (!tx)
    return;
  SDL_RenderCopy(m_ren, tx, nullptr, &r);
  SDL_DestroyTexture(tx);
}

int App::textW(const std::string &t, TTF_Font *f) {
  int w = 0;
  TTF_SizeUTF8(f, t.c_str(), &w, nullptr);
  return w;
}

std::string App::trunc(const std::string &t, TTF_Font *f, int maxPx) {
  if (textW(t, f) <= maxPx)
    return t;
  // Cắt theo UTF-8 char boundary, binary search số bytes.
  size_t lo = 0, hi = t.size();
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    // lùi về đầu char
    while (mid > 0 && (t[mid] & 0xC0) == 0x80)
      --mid;
    if (textW(t.substr(0, mid) + "...", f) <= maxPx)
      lo = mid;
    else if (mid == 0)
      break;
    else
      hi = mid - 1;
  }
  size_t end = lo;
  while (end > 0 && (t[end] & 0xC0) == 0x80)
    --end;
  return t.substr(0, end) + "...";
}

void App::rect(int x, int y, int w, int h, SDL_Color c, bool fill) {
  SDL_SetRenderDrawColor(m_ren, c.r, c.g, c.b, c.a);
  SDL_Rect r = {x, y, w, h};
  if (fill)
    SDL_RenderFillRect(m_ren, &r);
  else
    SDL_RenderDrawRect(m_ren, &r);
}

void App::header(const std::string &title, const std::string &sub) {
  rect(0, 0, W, HDR, C_PANEL);
  rect(0, HDR - 2, W, 2, C_FOCUS);
  drawText(title, 24, 10, C_TEXT, m_fTitle);
  if (!sub.empty())
    drawText(sub, W - 24 - textW(sub, m_fSmall), 22, C_DIM, m_fSmall);
}

void App::footer(const std::vector<std::pair<std::string, std::string>> &hints) {
  int y = H - FTR;
  rect(0, y, W, FTR, C_PANEL);
  rect(0, y, W, 2, C_FOCUS);
  int total = 0;
  for (auto &h : hints)
    total += 34 + 12 + textW(h.second, m_fSmall) + 30;
  int x = (W - total) / 2;
  int cy = y + FTR / 2;
  for (auto &h : hints) {
    // icon nút: rounded box + chữ
    rect(x, cy - 14, 34, 28, C_FOCUS);
    drawText(h.first, x + 17, cy - 12, {0, 0, 0, 255}, m_fSmall, true);
    x += 34 + 12;
    drawText(h.second, x, cy - 12, C_TEXT, m_fSmall);
    x += textW(h.second, m_fSmall) + 30;
  }
}

void App::row(int x, int y, int w, int h, bool sel) {
  rect(x, y, w, h, sel ? C_FOCUS : C_PANEL);
}

void App::progressBar(int x, int y, int w, int h, double frac) {
  if (frac < 0)
    frac = 0;
  if (frac > 1)
    frac = 1;
  rect(x, y, w, h, C_PANEL);
  rect(x, y, (int)(w * frac), h, C_FOCUS);
}

void App::setScreen(Screen s) {
  m_screen = s;
  m_sel = 0;
  m_scroll = 0;
}

void App::refreshDevices() {
  m_devices = LocalSendManager::instance().knownDevices();
  // IP của contact có thể đổi khi qua mạng khác → cập nhật lại để
  // announceToTrusted (unicast) không gõ nhầm địa chỉ cũ.
  bool changed = false;
  for (auto &d : m_devices) {
    if (!d.fingerprint.empty() && !d.ip.empty() &&
        Config::instance().isContact(d.fingerprint)) {
      if (Config::instance().updateContactNet(d.fingerprint, d.ip, d.port))
        changed = true;
    }
  }
  if (changed)
    syncTrustedPeers();
  // Tiện: tự chọn lại thiết bị vừa gửi (gửi nhiều file chỉ cần A, A).
  if (!m_lastDeviceFp.empty()) {
    for (size_t i = 0; i < m_devices.size(); ++i) {
      if (m_devices[i].fingerprint == m_lastDeviceFp) {
        m_sel = (int)i;
        break;
      }
    }
  }
  LocalSendManager::instance().refreshDiscovery();
}

void App::openSendPicker() {
  // Giữ nguyên thư mục đang duyệt: refresh thay vì open lại từ sdRoot
  // (App::init đã open 1 lần). Gửi nhiều file cùng folder không phải đi lại.
  m_sendLs.refresh();
  setScreen(Screen::SEND_PICK);
}

void App::openFiles() {
  // Lần đầu mở từ gốc thẻ nhớ, các lần sau giữ nguyên chỗ đang duyệt.
  if (m_fileLs.path().empty())
    m_fileLs.open(Config::instance().sdRoot());
  else
    m_fileLs.refresh();
  setScreen(Screen::FILES);
}

void App::syncTrustedPeers() {
  std::vector<LsTrusted> t;
  for (auto &c : Config::instance().contacts()) {
    if (c.fp.empty() || c.ip.empty())
      continue;
    LsTrusted lt;
    lt.fp = c.fp;
    lt.ip = c.ip;
    lt.port = c.port > 0 ? c.port : LocalSendProto::kPort;
    t.push_back(lt);
  }
  LocalSendManager::instance().setTrustedPeers(t);
}

void App::restartService() {
  auto &ls = LocalSendManager::instance();
  ls.stop();
  ls.setAlias(Config::instance().alias());
  ls.start();
  refreshDevices();
}

void App::pumpLsQueues() {
  std::lock_guard<std::mutex> l(m_qmtx);
  if (m_activePrompt.empty() && !m_prompts.empty()) {
    m_activeReq = m_prompts.front();
    m_prompts.pop_front();
    m_activePrompt = m_activeReq.sessionId;
    m_lastPromptMs = SDL_GetTicks();
  }
  // Test hook: BRICKDROP_AUTOAPPROVE=1 tự duyệt sau 2s (test E2E qua adb).
  std::string autoSid;
  if (!m_activePrompt.empty() && getenv("BRICKDROP_AUTOAPPROVE")) {
    if (SDL_GetTicks() - m_lastPromptMs > 2000) {
      autoSid = m_activePrompt;
      m_activePrompt.clear();
    }
  }
  // Timeout 60s không duyệt -> coi như bỏ qua modal (service tự timeout 408).
  if (!m_activePrompt.empty() && SDL_GetTicks() - m_lastPromptMs > 60000) {
    LocalSendManager::instance().rejectUpload(m_activePrompt);
    m_activePrompt.clear();
  }
  if (!autoSid.empty()) {
    LocalSendManager::instance().approveUploadWithPath(
        autoSid, Config::instance().saveDir());
    toast("Tự duyệt (autotest)");
    setScreen(Screen::PROGRESS);
  }
}

// ============================== OTA ==============================
// Auto-check lúc mở app: có bản mới → hiện modal ở HOME (A tải/B để sau).
// Đang tải/cài → modal progress. Xong → thoát code 42 để launch.sh
// chạy lại bản mới.
void App::pumpOta() {
  {
    std::lock_guard<std::mutex> l(m_otaMtx);
    if (m_otaAvailable && m_otaUi == OtaUi::NONE &&
        m_screen == Screen::HOME && m_activePrompt.empty()) {
      m_otaAvailable = false;
      m_otaUi = OtaUi::PROMPT;
    }
  }
  if (m_otaUi == OtaUi::BUSY) {
    auto p = UpdateManager::instance().getProgress();
    if (p.state == OtaState::COMPLETED) {
      toast("Cập nhật xong, đang khởi động lại...");
      m_exitCode = OTA_RESTART_EXIT_CODE;
      m_exit = true;
    } else if (p.state == OtaState::FAILED) {
      toast(p.errorMessage.empty() ? "Cập nhật thất bại"
                                   : p.errorMessage);
      m_otaUi = OtaUi::NONE;
    }
  }
}

void App::renderOta() {
  if (m_otaUi == OtaUi::PROMPT) {
    std::vector<std::string> lines;
    lines.push_back("Bản mới: v" + m_otaInfo.remoteVersion + " (đang dùng v" +
                    UpdateManager::instance().getCurrentVersion() + ")");
    if (!m_otaInfo.changelog.empty())
      lines.push_back(trunc(m_otaInfo.changelog, m_fMain, 640));
    modal("Có bản cập nhật", lines,
          {{"A", "Tải & cài đặt"}, {"B", "Để sau"}});
  } else if (m_otaUi == OtaUi::BUSY) {
    auto p = UpdateManager::instance().getProgress();
    int w = 700, h = 220;
    int x = (W - w) / 2, y = (H - h) / 2;
    rect(0, 0, W, H, {0, 0, 0, 160});
    rect(x, y, w, h, C_PANEL);
    rect(x, y, w, h, C_FOCUS, false);
    rect(x, y, w, 56, C_FOCUS);
    drawText("Đang cập nhật", x + 24, y + 8, {0, 0, 0, 255}, m_fMain);
    drawText(trunc(p.currentStep, m_fMain, w - 60), x + 30, y + 70, C_TEXT,
             m_fMain);
    progressBar(x + 30, y + 120, w - 60, 24, p.progressPct / 100.0);
  }
}

// ============================== INPUT ==============================
void App::frame() {
  pumpLsQueues();
  // Phase D: BLE handshake chạy nền mọi màn hình (máy kia có thể gọi tới
  // khi mình đang ở HOME); scan mạnh khi ở màn "Gửi đi".
  m_bleFlow.frame(m_screen == Screen::SEND_DEVICES);
  pumpOta();
  switch (m_screen) {
  case Screen::HOME:
    onHome();
    break;
  case Screen::SEND_PICK:
    onSendPick();
    break;
  case Screen::SEND_DEVICES:
    onSendDevices();
    break;
  case Screen::FILES:
    onFiles();
    break;
  case Screen::RECV_DIR:
    onRecvDir();
    break;
  case Screen::PROGRESS:
    onProgress();
    break;
  case Screen::OFFLINE:
    onOffline();
    break;
  }
  if (!m_activePrompt.empty())
    onIncoming();

  // Render
  rect(0, 0, W, H, C_BG);
  switch (m_screen) {
  case Screen::HOME:
    renderHome();
    break;
  case Screen::SEND_PICK:
    renderSendPick();
    break;
  case Screen::SEND_DEVICES:
    renderSendDevices();
    break;
  case Screen::FILES:
    renderFiles();
    break;
  case Screen::RECV_DIR:
    renderRecvDir();
    break;
  case Screen::PROGRESS:
    renderProgress();
    break;
  case Screen::OFFLINE:
    renderOffline();
    break;
  }
  if (!m_activePrompt.empty())
    renderIncoming();
  renderOta();
  // Popup menu thao tác file (SELECT) vẽ trên cùng, dưới toast.
  if (m_opsOpen)
    renderOpsMenu();
  // Toast
  if (!m_toast.msg.empty()) {
    if (SDL_GetTicks() < m_toast.until) {
      int w = textW(m_toast.msg, m_fMain) + 48;
      rect((W - w) / 2, H - FTR - 70, w, 52, C_PANEL);
      rect((W - w) / 2, H - FTR - 70, w, 52, C_FOCUS, false);
      drawText(m_toast.msg, W / 2, H - FTR - 56, C_TEXT, m_fMain, true);
    } else {
      m_toast.msg.clear();
    }
  }
  SDL_RenderPresent(m_ren);
}

void App::onHome() {
  auto &in = Input::instance();
  // Modal OTA: A = tải & cài đặt, B/MENU = để sau.
  if (m_otaUi == OtaUi::PROMPT) {
    if (in.justPressed(Button::A)) {
      UpdateManager::instance().startUpdate(m_otaInfo);
      m_otaUi = OtaUi::BUSY;
    } else if (in.justPressed(Button::B) || in.justPressed(Button::MENU)) {
      m_otaUi = OtaUi::NONE;
    }
    return;
  }
  const int n = 8;
  if (in.justPressed(Button::UP))
    m_sel = (m_sel + n - 1) % n;
  if (in.justPressed(Button::DOWN))
    m_sel = (m_sel + 1) % n;
  if (in.justPressed(Button::MENU))
    m_exit = true;
  if (in.justPressed(Button::B))
    m_exit = true;
  if (in.justPressed(Button::Y)) {
    Config::instance().regenerateAlias();
    LocalSendManager::instance().setAlias(Config::instance().alias());
    toast("Tên mới: " + Config::instance().alias());
  }
  if (in.justPressed(Button::A)) {
    if (m_sel == 0) {
      openSendPicker(); // chọn file/folder để gửi (đặt preset)
    } else if (m_sel == 1) {
      m_recvLs.open(Config::instance().saveDir());
      m_recvLs.refreshDirsOnly(true);
      setScreen(Screen::RECV_DIR);
    } else if (m_sel == 2) {
      // Gửi đi: chỉ chọn máy nhận rồi gửi. File/thư mục đã chọn sẵn ở hàng 1
      // (không tự mở picker chọn folder ở đây nữa).
      bool hasFile = !m_pickFile.empty();
      bool hasFolder = !m_pickFolder.empty() && !m_pickFolderFiles.empty();
      if (hasFile || hasFolder) {
        setScreen(Screen::SEND_DEVICES);
        refreshDevices();
      } else {
        // Preset hỏng (folder trống/không đọc được) → dọn sạch, báo bằng pill.
        m_pickFile.clear();
        m_pickFolder.clear();
        m_pickFolderFiles.clear();
        m_pickFolderTotal = 0;
        Config::instance().setSendPresetFile("");
        Config::instance().setSendPresetFolder("");
        toast("Chưa chọn file/thư mục để gửi");
      }
    } else if (m_sel == 3) {
      openFiles(); // quản lý file (duyệt + SELECT để thao tác)
    } else if (m_sel == 4) {
      setScreen(Screen::OFFLINE);
    } else if (m_sel == 5) {
      // Chế độ hiển thị kiểu AirDrop: Mọi người → Danh bạ → Tắt.
      int v = (Config::instance().visibility() + 1) % 3;
      Config::instance().setVisibility(v);
      LocalSendManager::instance().setVisibility((LsVisibility)v);
      toast(std::string("Hiển thị: ") + visName(v));
    } else if (m_sel == 6) {
      setScreen(Screen::PROGRESS);
    } else {
      m_exit = true;
    }
  }
}

void App::onSendPick() {
  if (pickerInput(m_sendLs, PickMode::SEND_FILE))
    setScreen(Screen::HOME);
}

void App::onFiles() {
  if (pickerInput(m_fileLs, PickMode::MANAGE))
    setScreen(Screen::HOME);
}

void App::onSendDevices() {
  auto &in = Input::instance();
  if (in.justPressed(Button::UP))
    m_sel--;
  if (in.justPressed(Button::DOWN))
    m_sel++;
  if (m_sel < 0)
    m_sel = 0;
  // Danh sách gộp: thiết bị LAN + máy chỉ thấy qua BLE ("Gần bạn").
  int total = (int)m_devices.size() + (int)m_bleFlow.bleOnlyPeers().size();
  if (total > 0 && m_sel >= total)
    m_sel = total - 1;
  if (in.justPressed(Button::B) || in.justPressed(Button::MENU))
    setScreen(Screen::SEND_PICK);
  if (in.justPressed(Button::Y)) {
    // Chế độ Tắt: refreshDiscovery là no-op → báo đúng flow thay vì "Đang quét".
    if (Config::instance().visibility() == 2) {
      toast("Chế độ hiển thị đang Tắt");
    } else {
      refreshDevices();
      m_devices = LocalSendManager::instance().knownDevices();
      toast("Đang quét thiết bị...");
    }
  }
  if (in.justPressed(Button::X)) {
    // Thêm/xóa danh bạ cho thiết bị đang chọn (ID đã lưu từ trước).
    // Hàng BLE chưa có fingerprint đầy đủ → kết nối trước (bấm A).
    if (m_sel < (int)m_devices.size() && !m_devices.empty()) {
      const auto &d = m_devices[(size_t)m_sel];
      if (d.fingerprint.empty()) {
        toast("Thiết bị chưa có ID");
      } else if (Config::instance().isContact(d.fingerprint)) {
        Config::instance().removeContact(d.fingerprint);
        toast("Đã xóa khỏi danh bạ");
      } else {
        Config::instance().addContact(d.fingerprint,
                                      d.alias.empty() ? d.ip : d.alias, d.ip,
                                      d.port);
        toast("Đã thêm vào danh bạ");
      }
      syncTrustedPeers();
    } else if (m_sel >= (int)m_devices.size() &&
               !m_bleFlow.bleOnlyPeers().empty()) {
      toast("Máy BLE: bấm A để kết nối");
    }
  }
  if (in.justPressed(Button::A)) {
    auto bleRows = m_bleFlow.bleOnlyPeers();
    int tot = (int)m_devices.size() + (int)bleRows.size();
    if (tot == 0) {
      if (Config::instance().visibility() == 2)
        toast("Chế độ hiển thị đang Tắt");
      else
        toast("Chưa phát hiện thiết bị nào (nhấn Y để quét)");
      return;
    }
    if (m_bleFlow.busy()) {
      toast("Đang kết nối, chờ chút...");
      return;
    }
    if (m_sel < (int)m_devices.size()) {
      startSendTo(m_devices[(size_t)m_sel]);
      return;
    }
    // Hàng BLE: handshake connectionless → dựng Wi-Fi → tự gửi.
    size_t bi = (size_t)(m_sel - (int)m_devices.size());
    if (bi < bleRows.size())
      m_bleFlow.userSelectBlePeer(bleRows[bi].shortId);
    return;
  }
}

// Bắt đầu gửi file/thư mục đã stage tới thiết bị LAN.
// (BleLinkFlow gọi lại sau khi link Wi-Fi qua BLE handshake dựng xong.)
void App::startSendTo(const LsDeviceInfo &target) {
  m_lastDeviceFp = target.fingerprint; // nhớ để tự chọn lại lần sau
  if (!m_pickFolder.empty() && !m_pickFolderFiles.empty()) {
    // Gửi cả thư mục: tuần tự từng file (giữ relativePath để receiver
    // dựng lại cấu trúc), chung 1 batchId → bên nhận duyệt 1 lần.
    // Dừng cả batch nếu 1 file bị từ chối/timeout/lỗi.
    auto files = m_pickFolderFiles;
    std::string batchId = makeBatchId();
    std::string batchName =
        m_pickFolder.substr(m_pickFolder.find_last_of('/') + 1);
    uint64_t batchSize = m_pickFolderTotal;
    std::thread([files, target, batchId, batchName, batchSize]() {
      auto &ls = LocalSendManager::instance();
      for (size_t i = 0; i < files.size(); ++i) {
        LsFileMeta meta;
        meta.fileName =
            files[i].first.substr(files[i].first.find_last_of('/') + 1);
        meta.relativePath = files[i].second;
        meta.batchId = batchId;
        meta.batchIndex = (int)i + 1;
        meta.batchTotal = (int)files.size();
        meta.batchSize = batchSize;
        meta.batchName = batchName;
        // Bản sync: block tới khi file này xong mới gửi file tiếp.
        std::string sid = ls.sendFileMeta(meta, files[i].first, target);
        if (sid.empty() || !ls.isRunning()) {
          Logger::warn("BrickDrop: batch " + batchName + " dừng ở file " +
                       std::to_string(i + 1) + "/" +
                       std::to_string(files.size()));
          break;
        }
      }
    }).detach();
    toast("Đang gửi thư mục (" + std::to_string(files.size()) + " file)...");
    setScreen(Screen::PROGRESS);
    return;
  }
  LsFileMeta meta;
  meta.fileName = m_pickFile.substr(m_pickFile.find_last_of('/') + 1);
  LocalSendManager::instance().sendFileMetaAsync(meta, m_pickFile, target);
  toast("Đang gửi...");
  setScreen(Screen::PROGRESS);
}

void App::onRecvDir() {
  if (pickerInput(m_recvLs, PickMode::RECV_DIR))
    setScreen(Screen::HOME);
}

// ================= FILE PICKER 1-PANE =================
// Component dùng chung cho "chọn file gửi" và "chọn thư mục nhận".
// Layout port từ RomCloud renderLocalSendFolderPicker (1024x768):
// pane 24,72,976x636, path bar, row 54px, icon PNG 34px, footer theo mode.
// pickerInput trả true → caller về HOME (B ở root / MENU).
bool App::pickerInput(DirLister &ls, PickMode mode) {
  auto &in = Input::instance();
  // Popup menu thao tác file (SELECT): khi mở thì chặn mọi phím khác.
  if (m_opsOpen) {
    opsMenuInput(ls);
    return false;
  }
  if (in.justPressed(Button::SELECT)) {
    openOpsMenu(ls);
    return false;
  }
  if (in.justPressed(Button::UP)) {
    ls.moveSel(-1);
    return false;
  }
  if (in.justPressed(Button::DOWN)) {
    ls.moveSel(1);
    return false;
  }
  // L1/R1/LEFT/RIGHT: nhảy 10 dòng (port từ RomCloud handleFolderPickerInput).
  if (in.justPressed(Button::LEFT) || in.justPressed(Button::L1)) {
    ls.moveSel(-10);
    return false;
  }
  if (in.justPressed(Button::RIGHT) || in.justPressed(Button::R1)) {
    ls.moveSel(10);
    return false;
  }
  if (in.justPressed(Button::MENU))
    return true;
  if (in.justPressed(Button::B)) {
    if (!ls.goUp())
      return true;
    return false;
  }
  if (in.justPressed(Button::A)) {
    const DirEntry *e = ls.current();
    if (e && e->isDir) {
      ls.enter();
    } else if (mode == PickMode::SEND_FILE && e) {
      // Preset file để gửi: lưu lại rồi về HOME (nút "Gửi đi" mới là action).
      m_pickFile = e->path;
      m_pickFolder.clear();
      m_pickFolderFiles.clear();
      m_pickFolderTotal = 0;
      Config::instance().setSendPresetFile(e->path);
      Config::instance().setSendPresetFolder("");
      toast("Đã chọn: " + e->name);
      return true; // về HOME
    } else if (mode == PickMode::MANAGE && e) {
      // Quản lý file: A vào file chỉ xem thông tin, không đặt preset gửi.
      toast(e->name + " (" + DirLister::humanSize(e->size) + ")");
    }
    return false;
  }
  // X: chọn cả thư mục để gửi (đặt preset, chỉ ở màn chọn file gửi).
  if (mode == PickMode::SEND_FILE && in.justPressed(Button::X)) {
    const DirEntry *e = ls.current();
    if (e && e->isDir) {
      std::vector<std::pair<std::string, std::string>> files;
      uint64_t total = 0;
      bool ok = collectFolderTree(e->path, e->name, files, total);
      if (ok && !files.empty()) {
        m_pickFolder = e->path;
        m_pickFolderFiles = std::move(files);
        m_pickFolderTotal = total;
        m_pickFile.clear();
        Config::instance().setSendPresetFolder(e->path);
        Config::instance().setSendPresetFile("");
        toast("Đã chọn thư mục: " + e->name + " (" +
              std::to_string(m_pickFolderFiles.size()) + " file)");
        return true; // về HOME
      } else if (!ok) {
        toast("Thư mục quá lớn (>2000 file hoặc sâu >16)");
      } else {
        toast("Thư mục trống");
      }
    } else {
      toast("Chọn 1 thư mục rồi bấm X để đặt preset");
    }
    return false;
  }
  if (mode == PickMode::RECV_DIR) {
    if (in.justPressed(Button::Y)) {
      confirmRecvDir(ls); // chốt, ở lại picker
      return false;
    }
    // START: chốt thư mục hiện tại + thoát thẳng ra HOME (không cần B B...).
    if (in.justPressed(Button::START)) {
      confirmRecvDir(ls);
      return true;
    }
  }
  // X: tạo thư mục mới (màn thư mục nhận + quản lý file).
  if ((mode == PickMode::RECV_DIR || mode == PickMode::MANAGE)) {
    if (in.justPressed(Button::X)) {
      std::string n = ls.suggestFolderName("BrickDrop");
      if (ls.mkdir(n))
        toast("Đã tạo " + n);
      else
        toast("Tạo thư mục thất bại");
      return false;
    }
  }
  return false;
}

void App::confirmRecvDir(DirLister &ls) {
  Config::instance().setSaveDir(ls.path());
  // setTargetFolder nhận relative; strip sdRoot prefix.
  std::string rel = ls.path();
  std::string root = Config::instance().sdRoot();
  if (rel.compare(0, root.size(), root) == 0)
    rel = rel.substr(root.size());
  LocalSendManager::instance().setTargetFolder(rel);
  // Hiện rõ path vừa chốt (dạng gọn, bỏ prefix /mnt/SDCARD).
  std::string disp = rel.empty() ? "/ (gốc thẻ nhớ)" : rel;
  toast("Thư mục nhận: " + disp);
}

// ================== Popup menu thao tác file (SELECT) ==================
// Mở bằng SELECT trong explorer. Điều hướng UP/DOWN, A chọn, B đóng.
void App::openOpsMenu(DirLister &ls) {
  const DirEntry *e = ls.current();
  m_opsActs.clear();
  m_opsTarget.clear();
  if (m_clip.has)
    m_opsActs.push_back(OpsAct::PASTE);
  if (e) {
    m_opsTarget = e->path;
    m_opsTargetIsDir = e->isDir;
    m_opsActs.push_back(OpsAct::COPY);
    m_opsActs.push_back(OpsAct::CUT);
    m_opsActs.push_back(OpsAct::DELETE);
    if (!e->isDir) {
      std::string low = e->name;
      for (auto &c : low)
        c = (char)tolower((unsigned char)c);
      if (low.size() >= 4 && low.compare(low.size() - 4, 4, ".zip") == 0)
        m_opsActs.push_back(OpsAct::UNZIP);
    }
  }
  if (m_opsActs.empty()) {
    toast("Không có gì để thao tác");
    return;
  }
  m_opsSel = 0;
  m_opsConfirmDelete = false;
  m_opsOpen = true;
}

void App::opsMenuInput(DirLister &ls) {
  auto &in = Input::instance();
  // Modal xác nhận xoá: A xoá, B huỷ.
  if (m_opsConfirmDelete) {
    if (in.justPressed(Button::A)) {
      std::string name = DirLister::baseName(m_opsTarget);
      if (DirLister::removeRec(m_opsTarget)) {
        toast("Đã xoá " + name);
        if (m_clip.has && m_clip.src == m_opsTarget)
          m_clip.has = false;
        // Preset gửi đang trỏ vào file vừa xoá -> dọn luôn.
        if (m_pickFile == m_opsTarget) {
          m_pickFile.clear();
          Config::instance().setSendPresetFile("");
        }
        if (m_pickFolder == m_opsTarget) {
          m_pickFolder.clear();
          m_pickFolderFiles.clear();
          m_pickFolderTotal = 0;
          Config::instance().setSendPresetFolder("");
        }
      } else {
        toast("Xoá thất bại");
      }
      m_opsConfirmDelete = false;
      m_opsOpen = false;
      ls.refresh();
    } else if (in.justPressed(Button::B)) {
      m_opsConfirmDelete = false;
    }
    return;
  }
  int n = (int)m_opsActs.size();
  if (in.justPressed(Button::UP))
    m_opsSel = (m_opsSel - 1 + n) % n;
  else if (in.justPressed(Button::DOWN))
    m_opsSel = (m_opsSel + 1) % n;
  else if (in.justPressed(Button::B))
    m_opsOpen = false;
  else if (in.justPressed(Button::A))
    doOpsAction(ls, m_opsActs[(size_t)m_opsSel]);
}

void App::doOpsAction(DirLister &ls, OpsAct act) {
  std::string name = DirLister::baseName(m_opsTarget);
  switch (act) {
  case OpsAct::COPY:
    m_clip = {true, false, m_opsTarget, m_opsTargetIsDir};
    m_opsOpen = false;
    toast("Đã sao chép '" + name + "' — tới thư mục cần, SELECT > Dán");
    break;
  case OpsAct::CUT:
    m_clip = {true, true, m_opsTarget, m_opsTargetIsDir};
    m_opsOpen = false;
    toast("Đã cắt '" + name + "' — tới thư mục cần, SELECT > Dán");
    break;
  case OpsAct::PASTE:
    pasteClip(ls);
    break;
  case OpsAct::DELETE:
    m_opsConfirmDelete = true;
    break;
  case OpsAct::UNZIP:
    m_opsOpen = false;
    if (DirLister::unzipToDir(m_opsTarget, ls.path()))
      toast("Đã bung '" + name + "'");
    else
      toast("Bung zip thất bại");
    ls.refresh();
    break;
  }
}

void App::pasteClip(DirLister &ls) {
  if (!m_clip.has) {
    m_opsOpen = false;
    return;
  }
  std::string src = m_clip.src;
  struct stat st;
  if (stat(src.c_str(), &st) != 0) {
    toast("File gốc không còn nữa");
    m_clip.has = false;
    m_opsOpen = false;
    return;
  }
  std::string name = DirLister::baseName(src);
  // Dời vào chính thư mục chứa nó -> vô nghĩa.
  if (m_clip.cut && DirLister::dirName(src) == ls.path()) {
    toast("Đã ở đây rồi");
    m_opsOpen = false;
    return;
  }
  // Không dán thư mục vào trong chính nó.
  if (m_clip.srcIsDir &&
      (ls.path() == src || ls.path().compare(0, src.size() + 1, src + "/") == 0)) {
    toast("Không thể dán vào chính nó");
    m_opsOpen = false;
    return;
  }
  std::string dst = ls.path() + "/" + ls.suggestName(name);
  // File lẻ: kiểm tra dung lượng trước cho rõ ràng.
  if (!m_clip.srcIsDir && (uint64_t)st.st_size > DirLister::diskFree(ls.path())) {
    toast("Không đủ dung lượng");
    m_opsOpen = false;
    return;
  }
  bool ok = m_clip.cut ? DirLister::movePath(src, dst) : DirLister::copyRec(src, dst);
  if (ok) {
    toast(m_clip.cut ? "Đã dời '" + name + "'" : "Đã dán '" + name + "'");
    // Preset gửi đi theo file/thư mục vừa dời.
    if (m_clip.cut) {
      if (m_pickFile == src) {
        m_pickFile = dst;
        Config::instance().setSendPresetFile(dst);
      }
      if (m_pickFolder == src) {
        m_pickFolder = dst;
        Config::instance().setSendPresetFolder(dst);
        for (auto &pr : m_pickFolderFiles)
          if (pr.first.compare(0, src.size(), src) == 0)
            pr.first = dst + pr.first.substr(src.size());
      }
    }
  } else {
    toast("Thất bại");
  }
  m_clip.has = false;
  m_opsOpen = false;
  ls.refresh();
}

void App::renderOpsMenu() {
  // Nền mờ phủ lên picker.
  rect(0, 0, W, H, {0, 0, 0, 160});
  if (m_opsConfirmDelete) {
    std::string name = DirLister::baseName(m_opsTarget);
    modal("Xoá?",
          {"Xoá '" + name + "'" + (m_opsTargetIsDir ? " (cả thư mục!)" : ""),
           "Không thể hoàn tác."},
          {{"A", "Xoá"}, {"B", "Huỷ"}});
    return;
  }
  int n = (int)m_opsActs.size();
  int w = 480, rowH = 52;
  int h = 150 + n * rowH;
  int x = (W - w) / 2, y = (H - h) / 2;
  rect(x, y, w, h, C_PANEL);
  rect(x, y, w, h, C_FOCUS, false);
  std::string title = m_opsTarget.empty()
                          ? "Thao tác"
                          : "Thao tác: " + DirLister::baseName(m_opsTarget);
  drawText(trunc(title, m_fMain, w - 48), x + 24, y + 16, C_TEXT, m_fMain);
  rect(x + 24, y + 54, w - 48, 2, C_DIM);
  for (int i = 0; i < n; ++i) {
    int iy = y + 66 + i * rowH;
    bool sel = (i == m_opsSel);
    if (sel)
      rect(x + 12, iy, w - 24, rowH - 8, C_FOCUS);
    std::string label;
    switch (m_opsActs[i]) {
    case OpsAct::PASTE:
      label = "Dán '" + DirLister::baseName(m_clip.src) + "'" +
              (m_clip.cut ? " (dời)" : "");
      break;
    case OpsAct::COPY:
      label = "Sao chép";
      break;
    case OpsAct::CUT:
      label = "Dời";
      break;
    case OpsAct::DELETE:
      label = "Xoá";
      break;
    case OpsAct::UNZIP:
      label = "Bung zip";
      break;
    }
    drawText(trunc(label, m_fMain, w - 80), x + 32, iy + 8, C_TEXT, m_fMain);
  }
  drawText("A Chọn · B Đóng", x + w / 2, y + h - 38, C_DIM, m_fSmall, true);
}

void App::renderPicker(DirLister &ls, const std::string &title, PickMode mode) {
  // Header: title + dung lượng trống (như RomCloud).
  header(title,
         "Trống: " + DirLister::humanSize(DirLister::diskFree(ls.path())));
  const int px = 24, py = 72, pw = W - 48, ph = 636;
  rect(px, py, pw, ph, C_PANEL);
  rect(px, py, pw, ph, C_FOCUS, false);
  // Path bar: strip sdRoot prefix cho gọn.
  std::string sp = ls.path();
  std::string root = Config::instance().sdRoot();
  if (sp.compare(0, root.size(), root) == 0)
    sp = sp.substr(root.size());
  if (sp.empty())
    sp = "/";
  drawText(trunc("Thư mục: " + sp, m_fMain, pw - 32), px + 16, py + 12, C_TEXT,
           m_fMain);
  rect(px + 16, py + 44, pw - 32, 2, C_DIM);
  // Rows: rowH 54, icon PNG 34px, tên + size right-align (port ExplorerRender).
  const auto &es = ls.entries();
  int total = (int)es.size();
  const int rowH = 54, listTop = py + 48;
  int vis = (ph - 52) / rowH;
  if (vis < 1)
    vis = 1;
  // Scroll: giữ selected luôn visible (port FileListView::calcScroll).
  if (ls.selected() < m_scroll)
    m_scroll = ls.selected();
  if (ls.selected() >= m_scroll + vis)
    m_scroll = ls.selected() - vis + 1;
  if (m_scroll > total - vis)
    m_scroll = total - vis;
  if (m_scroll < 0)
    m_scroll = 0;
  for (int i = 0; i < vis; ++i) {
    int idx = m_scroll + i;
    if (idx >= total)
      break;
    int y = listTop + i * rowH;
    bool sel = (idx == ls.selected());
    const auto &e = es[(size_t)idx];
    if (sel) {
      rect(px + 8, y, pw - 16, rowH - 6, {37, 99, 235, 255});
      rect(px + 8, y, 4, rowH - 6, C_FOCUS);
    }
    m_icons.draw(e.isDir ? "icons/FOLDER.png" : "icons/FILES.png", px + 16,
                 y + 8, 34, 34);
    int rowRight = px + pw - 16;
    std::string sub;
    if ((mode == PickMode::SEND_FILE || mode == PickMode::MANAGE) && !e.isDir)
      sub = DirLister::humanSize(e.size);
    int sizeW = sub.empty() ? 0 : textW(sub, m_fSmall);
    int nameMaxW = rowRight - (px + 60) - (sizeW > 0 ? sizeW + 16 : 0);
    if (nameMaxW < 60)
      nameMaxW = 60;
    drawText(trunc(e.name, m_fMain, nameMaxW), px + 60, y + 10, C_TEXT, m_fMain);
    if (!sub.empty())
      drawText(sub, rowRight - sizeW, y + 14, C_DIM, m_fSmall);
  }
  if (total == 0)
    drawText(mode == PickMode::RECV_DIR
                 ? "(trống — Y để chốt chính thư mục này)"
                 : "Thư mục trống",
             px + pw / 2, listTop + 60, C_DIM, m_fMain, true);
  // RECV_DIR: dòng nơi nhận đã chốt (giữ tính năng cũ, không mất khi hết toast).
  if (mode == PickMode::RECV_DIR) {
    std::string saved = Config::instance().saveDir();
    bool here = (ls.path() == saved);
    drawText(trunc("Thư mục nhận file: " + saved +
                        (here ? "  (đang ở đây)" : ""),
                   m_fSmall, W - 120),
             60, H - FTR - 36, here ? C_OK : C_DIM, m_fSmall);
  }
  if (mode == PickMode::SEND_FILE)
    footer({{"A", "Vào/Chọn"},
            {"X", "Chọn thư mục"},
            {"SELECT", "Thao tác"},
            {"B", "Lên/Thoát"}});
  else if (mode == PickMode::MANAGE)
    footer({{"A", "Vào/Xem"},
            {"X", "Tạo thư mục"},
            {"SELECT", "Thao tác"},
            {"B", "Về"}});
  else
    footer({{"A", "Vào"},
            {"Y", "Chốt"},
            {"START", "Chốt + Về"},
            {"X", "Tạo thư mục"},
            {"SELECT", "Thao tác"},
            {"B", "Về"}});
}

void App::onProgress() {
  auto &in = Input::instance();
  if (in.justPressed(Button::B) || in.justPressed(Button::MENU) ||
      in.justPressed(Button::A))
    setScreen(Screen::HOME);
  // X: gửi tiếp file khác — thẳng về picker, giữ nguyên thư mục đang duyệt.
  if (in.justPressed(Button::X))
    openSendPicker();
  if (in.justPressed(Button::Y)) {
    LocalSendManager::instance().clearFinishedTasks();
    toast("Đã dọn danh sách");
  }
}

void App::onOffline() {
  auto &in = Input::instance();
  auto &wd = WifiDirectManager::instance();
  // Hàng 0=Phát, 1=Quét, 2=Rời; sau đó là list nhóm quét được.
  int total = 3 + (int)m_groups.size();
  if (in.justPressed(Button::UP))
    m_sel = (m_sel + total - 1) % total;
  if (in.justPressed(Button::DOWN))
    m_sel = (m_sel + 1) % total;
  if (in.justPressed(Button::B) || in.justPressed(Button::MENU))
    setScreen(Screen::HOME);
  if (m_working)
    return;
  if (in.justPressed(Button::A)) {
    if (m_sel == 0) {
      m_working = true;
      std::thread([this]() {
        std::string msg;
        std::string ssid = WifiDirectManager::instance().startGroup(msg);
        m_workMsg = msg;
        m_working = false;
        if (!ssid.empty())
          restartService();
        toast(m_workMsg);
      }).detach();
    } else if (m_sel == 1) {
      m_working = true;
      m_scanning = true;
      std::thread([this]() {
        m_groups = WifiDirectManager::instance().scanGroups();
        // Tiện: quét xong nhảy sẵn tới nhóm đầu tiên (khỏi DOWN 2 lần).
        if (!m_groups.empty())
          m_sel = 3;
        m_working = false;
        m_scanning = false;
        toast(m_groups.empty() ? "Không tìm thấy hotspot nào" : "Tìm thấy " + std::to_string(m_groups.size()) + " hotspot");
      }).detach();
    } else if (m_sel == 2) {
      m_working = true;
      std::thread([this]() {
        std::string msg;
        WifiDirectManager::instance().stopGroup(msg);
        m_workMsg = msg;
        m_working = false;
        restartService();
        toast(m_workMsg);
      }).detach();
    } else {
      size_t gi = (size_t)(m_sel - 3);
      if (gi < m_groups.size()) {
        std::string ssid = m_groups[gi].ssid;
        m_working = true;
        std::thread([this, ssid]() {
          std::string msg;
          bool ok = WifiDirectManager::instance().joinGroup(ssid, msg);
          m_workMsg = msg;
          m_working = false;
          if (ok)
            restartService();
          toast(m_workMsg);
        }).detach();
      }
    }
  }
}

void App::onIncoming() {
  auto &in = Input::instance();
  if (in.justPressed(Button::A)) {
    // Duyệt: lưu vào saveDir đã chốt
    LocalSendManager::instance().approveUploadWithPath(
        m_activePrompt, Config::instance().saveDir());
    toast("Đang nhận...");
    m_activePrompt.clear();
    setScreen(Screen::PROGRESS);
  }
  if (in.justPressed(Button::B)) {
    LocalSendManager::instance().rejectUpload(m_activePrompt);
    m_activePrompt.clear();
    toast("Đã từ chối");
  }
}

// ============================== RENDER ==============================
void App::renderHome() {
  auto &wd = WifiDirectManager::instance();
  header("BrickDrop", DeviceIdentity::instance().shortId());
  // Mô hình preset: File/thư mục gửi đi (chọn trước ở hàng 1) /
  // Thư mục nhận (preset) / Gửi đi (chỉ chọn máy, không chọn folder) / ...
  const char *items[] = {"File/thư mục gửi đi", "Thư mục nhận", "Gửi đi",
                         "Quản lý file", "WiFi Hotspot", "Hiển thị",
                         "Tiến trình truyền", "Thoát"};
  int y = 76;
  for (int i = 0; i < 8; ++i) {
    bool sel = (i == m_sel);
    row(60, y, W - 120, 54, sel);
    SDL_Color tc = sel ? SDL_Color{0, 0, 0, 255} : C_TEXT;
    if (i == 0) {
      // Preset: path thư mục hoặc tên file sẽ gửi.
      std::string sub;
      if (!m_pickFolder.empty()) {
        std::string p = m_pickFolder;
        std::string root = Config::instance().sdRoot();
        if (p.compare(0, root.size(), root) == 0)
          p = p.substr(root.size());
        if (p.empty())
          p = "/";
        sub = "Thư mục: " + p + " (" +
              std::to_string(m_pickFolderFiles.size()) + " file)";
      } else if (!m_pickFile.empty()) {
        sub = m_pickFile.substr(m_pickFile.find_last_of('/') + 1);
      } else {
        sub = "Chưa chọn";
      }
      drawText(items[i], 90, y + 2, tc, m_fMain);
      drawText(trunc(sub, m_fSmall, W - 260), 90, y + 30, sel ? tc : C_DIM,
               m_fSmall);
    } else if (i == 1) {
      // Nút "Thư mục nhận" hiện luôn path đã chốt (2 dòng).
      std::string sd = Config::instance().saveDir();
      std::string root = Config::instance().sdRoot();
      std::string rel = sd;
      if (rel.compare(0, root.size(), root) == 0)
        rel = rel.substr(root.size());
      if (rel.empty())
        rel = "/";
      drawText(items[i], 90, y + 2, tc, m_fMain);
      drawText(trunc(rel, m_fSmall, W - 260), 90, y + 30, sel ? tc : C_DIM,
               m_fSmall);
    } else if (i == 2) {
      // Nút action chính: xanh lá để nổi bật.
      drawText(items[i], 90, y + 14, sel ? tc : C_OK, m_fMain);
      // Pill cảnh báo khi hàng 1 chưa chọn file/thư mục nào.
      bool hasFile = !m_pickFile.empty();
      bool hasFolder = !m_pickFolder.empty() && !m_pickFolderFiles.empty();
      if (!hasFile && !hasFolder) {
        const char *warn = "Chưa chọn file/thư mục để gửi";
        int tw = textW(warn, m_fSmall);
        int pw = tw + 28, ph = 34;
        int px = 60 + (W - 120) - 16 - pw;
        int py = y + (54 - ph) / 2;
        SDL_Color orange{255, 140, 0, 255};
        rect(px, py, pw, ph, orange);
        rect(px, py, pw, ph, {0, 0, 0, 255}, false);
        drawText(warn, px + pw / 2, py + 4, {0, 0, 0, 255}, m_fSmall, true);
      }
    } else if (i == 5) {
      // Nút "Hiển thị" hiện chế độ hiện tại (2 dòng).
      drawText(items[i], 90, y + 2, tc, m_fMain);
      drawText(visName(Config::instance().visibility()), 90, y + 30,
               sel ? tc : C_DIM, m_fSmall);
    } else {
      drawText(items[i], 90, y + 14, tc, m_fMain);
    }
    y += 60;
  }
  std::string st = "IP " + wd.ownIp() + "  |  Nơi nhận: " +
                   Config::instance().saveDir() + "  |  v" +
                   UpdateManager::instance().getCurrentVersion();
  drawText(trunc(st, m_fSmall, W - 120), 60, 560, C_DIM, m_fSmall);
  std::string dev = "Phát hiện " + std::to_string(LocalSendManager::instance().knownDevices().size()) + " thiết bị xung quanh";
  drawText(dev, 60, 592, C_DIM, m_fSmall);
  footer({{"A", "Chọn"}, {"Y", "Tên mới"}, {"B", "Thoát"}});
}

void App::renderSendPick() {
  renderPicker(m_sendLs, "Chọn file gửi", PickMode::SEND_FILE);
}

void App::renderFiles() {
  renderPicker(m_fileLs, "Quản lý file", PickMode::MANAGE);
}

void App::renderSendDevices() {
  // Subtitle: file lẻ hoặc thư mục (số file + dung lượng để user quyết).
  std::string sub;
  if (!m_pickFolder.empty()) {
    std::string fn = m_pickFolder.substr(m_pickFolder.find_last_of('/') + 1);
    sub = "Thư mục: " + fn + " (" +
          std::to_string(m_pickFolderFiles.size()) + " file, " +
          DirLister::humanSize(m_pickFolderTotal) + ")";
  } else {
    sub = m_pickFile;
  }
  header("Chọn thiết bị nhận", trunc(sub, m_fSmall, 500));
  // Danh sách gộp: thiết bị LAN trước, máy chỉ thấy qua BLE ("Gần bạn") sau.
  auto bleRows = m_bleFlow.bleOnlyPeers();
  int y0 = 110;
  int rowIdx = 0;
  for (size_t i = 0; i < m_devices.size() && rowIdx < 8; ++i, ++rowIdx) {
    int y = y0 + rowIdx * 68;
    row(60, y, W - 120, 58, rowIdx == m_sel);
    const auto &d = m_devices[i];
    SDL_Color c = rowIdx == m_sel ? SDL_Color{0, 0, 0, 255} : C_TEXT;
    // ★ đánh dấu thiết bị đã lưu trong danh bạ.
    std::string name =
        (Config::instance().isContact(d.fingerprint) ? "★ " : "") +
        (d.alias.empty() ? d.ip : d.alias);
    drawText(trunc(name, m_fMain, 500), 90, y + 4, c,
             m_fMain);
    // Dòng 2: pill ID máy (BD-XXXXXXXX từ fingerprint, trùng ID máy kia
    // tự hiện ở góc phải) + IP + model, xếp hàng ngang.
    int lx = 90;
    if (d.fingerprint.size() >= 8) {
      std::string sid = "BD-" + d.fingerprint.substr(0, 8);
      int tw = textW(sid, m_fSmall);
      int pw = tw + 20, ph = 28, py = y + 28;
      rect(lx, py, pw, ph, C_BG);
      rect(lx, py, pw, ph, C_DIM, false);
      drawText(sid, lx + pw / 2, py + 3, C_TEXT, m_fSmall, true);
      lx += pw + 12;
    }
    drawText(trunc(d.ip + "  " + d.deviceModel, m_fSmall, 964 - 16 - lx), lx,
             y + 31, C_DIM, m_fSmall);
  }
  for (size_t j = 0; j < bleRows.size() && rowIdx < 8; ++j, ++rowIdx) {
    int y = y0 + rowIdx * 68;
    int idx = (int)m_devices.size() + (int)j;
    row(60, y, W - 120, 58, idx == m_sel);
    const auto &p = bleRows[j];
    SDL_Color c = idx == m_sel ? SDL_Color{0, 0, 0, 255} : C_TEXT;
    std::string name = p.alias.empty() ? "BD-" + p.shortId : p.alias;
    drawText(trunc(name, m_fMain, 500), 90, y + 4, c, m_fMain);
    // Dòng 2: pill xanh "Gần bạn" + ID + cường độ sóng BLE.
    int lx = 90;
    const char *tag = "Gần bạn";
    int tw = textW(tag, m_fSmall);
    int pw = tw + 20, ph = 28, py = y + 28;
    rect(lx, py, pw, ph, C_OK);
    drawText(tag, lx + pw / 2, py + 3, {0, 0, 0, 255}, m_fSmall, true);
    lx += pw + 12;
    std::string info = "BD-" + p.shortId + "  BLE " +
                       std::to_string(p.rssi) + " dBm";
    drawText(trunc(info, m_fSmall, 964 - 16 - lx), lx, y + 31, C_DIM, m_fSmall);
  }
  if (m_devices.empty() && bleRows.empty())
    drawText(devEmptyHint(), W / 2, 300, C_DIM, m_fMain, true);
  // Trạng thái handshake BLE (nếu đang nối).
  if (m_bleFlow.busy())
    drawText(m_bleFlow.status(), W / 2, H - FTR - 70, C_WARN, m_fMain, true);
  // Chế độ Tắt: không quét/không thấy ai → ẩn Y/X cho đúng flow.
  if (Config::instance().visibility() == 2)
    footer({{"B", "Về"}});
  else
    footer({{"A", "Gửi"}, {"Y", "Quét"}, {"X", "Danh bạ"}, {"B", "Về"}});
}

void App::renderRecvDir() {
  renderPicker(m_recvLs, "Thư mục nhận file", PickMode::RECV_DIR);
}

void App::renderProgress() {
  header("Đang truyền", "");
  auto sends = LocalSendManager::instance().sendProgresses();
  auto recvs = LocalSendManager::instance().receiveProgresses();
  int y = 100;
  drawText("GỬI (" + std::to_string(sends.size()) + ")", 60, y, C_DIM, m_fSmall);
  y += 34;
  for (size_t i = 0; i < sends.size() && i < 4; ++i) {
    const auto &s = sends[i];
    drawText(trunc(s.fileName + " → " + s.toAlias, m_fMain, W - 300), 60, y,
             C_TEXT, m_fMain);
    y += 34;
    double f = s.totalBytes ? (double)s.sentBytes / (double)s.totalBytes : 0;
    progressBar(60, y, W - 120, 22, f);
    std::string st = s.state == LsSendProgress::DONE
                         ? "Hoàn tất"
                         : (s.state == LsSendProgress::FAILED ? "Lỗi: " + s.errorMessage
                                                              : DirLister::humanSize(s.sentBytes) + " / " +
                                                                    DirLister::humanSize(s.totalBytes));
    drawText(st, 60, y + 26, C_DIM, m_fSmall);
    y += 58;
  }
  drawText("NHẬN (" + std::to_string(recvs.size()) + ")", 60, y, C_DIM, m_fSmall);
  y += 34;
  for (size_t i = 0; i < recvs.size() && i < 4; ++i) {
    const auto &r = recvs[i];
    drawText(trunc(r.file.fileName + " ← " + r.fromAlias, m_fMain, W - 300), 60,
             y, C_TEXT, m_fMain);
    y += 34;
    uint64_t tot = r.file.size ? r.file.size : 1;
    progressBar(60, y, W - 120, 22, (double)r.receivedBytes / (double)tot);
    y += 30;
  }
  footer({{"X", "Gửi tiếp"}, {"Y", "Dọn danh sách"}, {"B", "Về"}});
}

void App::renderOffline() {
  auto &wd = WifiDirectManager::instance();
  header("WiFi Hotspot", wd.mode() == LinkMode::HOST ? "AP đang phát: " + wd.groupSsid()
                  : wd.mode() == LinkMode::JOINED ? "Đã kết nối: " + wd.groupSsid()
                                                  : "Chưa kết nối");
  const char *ops[] = {"Phát Hotspot (Host)", "Quét Hotspot lân cận",
                       "Ngắt kết nối"};
  int y = 110;
  for (int i = 0; i < 3; ++i) {
    row(60, y, W - 120, 58, i == m_sel);
    drawText(ops[i], 90, y + 12,
             i == m_sel ? SDL_Color{0, 0, 0, 255} : C_TEXT, m_fMain);
    y += 68;
  }
  if (m_scanning || m_working)
    drawText("Đang xử lý...", 90, y + 6, C_WARN, m_fMain);
  else {
    for (size_t i = 0; i < m_groups.size() && i < 5; ++i) {
      int idx = 3 + (int)i;
      row(60, y, W - 120, 58, idx == m_sel);
      drawText(m_groups[i].ssid + "  (" + std::to_string(m_groups[i].signalDbm) +
                   " dBm)",
               90, y + 12, idx == m_sel ? SDL_Color{0, 0, 0, 255} : C_TEXT,
               m_fMain);
      y += 68;
    }
  }
  std::string ip = "IP: " + wd.ownIp();
  drawText(ip, 90, H - FTR - 40, C_DIM, m_fSmall);
  footer({{"A", "Chọn"}, {"B", "Về"}});
}

void App::renderIncoming() {
  const auto &f = m_activeReq.file;
  // Batch (gửi cả thư mục): chỉ hiện modal 1 lần cho file đầu — hiện tên
  // thư mục + tổng số file/dung lượng để user quyết.
  bool isBatch = f.batchTotal > 0;
  std::vector<std::string> lines;
  lines.push_back("Từ: " + (m_activeReq.fromAlias.empty() ? m_activeReq.fromIp
                                                          : m_activeReq.fromAlias));
  if (isBatch) {
    std::string bn = f.batchName.empty() ? f.fileName : f.batchName;
    lines.push_back("Thư mục: " + bn + " (" +
                    std::to_string(f.batchTotal) + " file)");
    std::string sz = f.batchSize ? DirLister::humanSize(f.batchSize) : "?";
    lines.push_back("Dung lượng: " + sz);
  } else {
    lines.push_back("File: " + f.fileName);
    std::string sz = f.size ? DirLister::humanSize(f.size) : "?";
    lines.push_back("Dung lượng: " + sz);
  }
  lines.push_back("Lưu vào: " + Config::instance().saveDir());
  modal(isBatch ? "Nhận thư mục?" : "Nhận file?", lines,
        {{"A", "Nhận"}, {"B", "Từ chối"}});
}

void App::modal(const std::string &title,
                const std::vector<std::string> &lines,
                const std::vector<std::pair<std::string, std::string>> &hints) {
  int w = 700, h = 140 + (int)lines.size() * 40;
  int x = (W - w) / 2, y = (H - h) / 2;
  rect(0, 0, W, H, {0, 0, 0, 160});
  rect(x, y, w, h, C_PANEL);
  rect(x, y, w, h, C_FOCUS, false);
  rect(x, y, w, 56, C_FOCUS);
  drawText(title, x + 24, y + 8, {0, 0, 0, 255}, m_fMain);
  for (size_t i = 0; i < lines.size(); ++i)
    drawText(trunc(lines[i], m_fMain, w - 60), x + 30, y + 70 + (int)i * 40,
             C_TEXT, m_fMain);
  int hx = x + 30;
  for (auto &hh : hints) {
    rect(hx, y + h - 48, 34, 28, C_FOCUS);
    drawText(hh.first, hx + 17, y + h - 46, {0, 0, 0, 255}, m_fSmall, true);
    drawText(hh.second, hx + 46, y + h - 46, C_TEXT, m_fSmall);
    hx += 46 + textW(hh.second, m_fSmall) + 30;
  }
}

} // namespace BrickDrop
