#include "App.h"
#include "../core/Config.h"
#include "../core/Logger.h"
#include "../input/Input.h"
#include "../localsend/LocalSendManager.h"
#include <algorithm>
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

bool App::init(SDL_Window *win, SDL_Renderer *ren) {
  (void)win;
  m_ren = ren;
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
  m_fTitle = TTF_OpenFont(font.c_str(), 40);
  m_fMain = TTF_OpenFont(font.c_str(), 26);
  m_fSmall = TTF_OpenFont(font.c_str(), 20);
  if (!m_fTitle || !m_fMain || !m_fSmall)
    return false;

  m_sendLs.open(Config::instance().sdRoot());
  m_recvLs.open(Config::instance().saveDir());
  m_recvLs.refreshDirsOnly(true);
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
  LocalSendManager::instance().refreshDiscovery();
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

// ============================== INPUT ==============================
void App::frame() {
  pumpLsQueues();
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
  const int n = 5;
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
      m_sendLs.open(Config::instance().sdRoot());
      setScreen(Screen::SEND_PICK);
    } else if (m_sel == 1) {
      m_recvLs.open(Config::instance().saveDir());
      m_recvLs.refreshDirsOnly(true);
      setScreen(Screen::RECV_DIR);
    } else if (m_sel == 2) {
      setScreen(Screen::OFFLINE);
    } else if (m_sel == 3) {
      setScreen(Screen::PROGRESS);
    } else {
      m_exit = true;
    }
  }
}

void App::onSendPick() {
  auto &in = Input::instance();
  auto &ls = m_sendLs;
  if (in.justPressed(Button::UP))
    ls.moveSel(-1);
  if (in.justPressed(Button::DOWN))
    ls.moveSel(1);
  if (in.justPressed(Button::B)) {
    if (!ls.goUp())
      setScreen(Screen::HOME);
  }
  if (in.justPressed(Button::MENU))
    setScreen(Screen::HOME);
  if (in.justPressed(Button::A)) {
    const DirEntry *e = ls.current();
    if (!e) {
    } else if (e->isDir) {
      ls.enter();
    } else {
      m_pickFile = e->path;
      refreshDevices();
      setScreen(Screen::SEND_DEVICES);
    }
  }
}

void App::onSendDevices() {
  auto &in = Input::instance();
  if (in.justPressed(Button::UP))
    m_sel--;
  if (in.justPressed(Button::DOWN))
    m_sel++;
  if (m_sel < 0)
    m_sel = 0;
  if (!m_devices.empty() && m_sel >= (int)m_devices.size())
    m_sel = (int)m_devices.size() - 1;
  if (in.justPressed(Button::B) || in.justPressed(Button::MENU))
    setScreen(Screen::SEND_PICK);
  if (in.justPressed(Button::Y)) {
    refreshDevices();
    m_devices = LocalSendManager::instance().knownDevices();
    toast("Đang quét...");
  }
  if (in.justPressed(Button::A)) {
    if (m_devices.empty() || m_sel >= (int)m_devices.size()) {
      toast("Chưa thấy máy nào (Y để quét)");
      return;
    }
    LsFileMeta meta;
    meta.fileName = m_pickFile.substr(m_pickFile.find_last_of('/') + 1);
    LocalSendManager::instance().sendFileMetaAsync(meta, m_pickFile,
                                                   m_devices[(size_t)m_sel]);
    toast("Đang gửi...");
    setScreen(Screen::PROGRESS);
  }
}

void App::onRecvDir() {
  auto &in = Input::instance();
  auto &ls = m_recvLs;
  if (in.justPressed(Button::UP))
    ls.moveSel(-1);
  if (in.justPressed(Button::DOWN))
    ls.moveSel(1);
  if (in.justPressed(Button::B)) {
    if (!ls.goUp())
      setScreen(Screen::HOME);
  }
  if (in.justPressed(Button::MENU))
    setScreen(Screen::HOME);
  if (in.justPressed(Button::A))
    ls.enter();
  if (in.justPressed(Button::Y)) {
    // Chốt folder hiện tại làm nơi nhận
    Config::instance().setSaveDir(ls.path());
    // setTargetFolder nhận relative; normalizeRel tự strip sdRoot.
    std::string rel = ls.path();
    std::string root = Config::instance().sdRoot();
    if (rel.compare(0, root.size(), root) == 0)
      rel = rel.substr(root.size());
    LocalSendManager::instance().setTargetFolder(rel);
    toast("Nơi nhận: " + ls.path());
  }
  if (in.justPressed(Button::X)) {
    std::string n = ls.suggestFolderName("BrickDrop");
    if (ls.mkdir(n))
      toast("Đã tạo " + n);
    else
      toast("Tạo thư mục thất bại");
  }
}

void App::onProgress() {
  auto &in = Input::instance();
  if (in.justPressed(Button::B) || in.justPressed(Button::MENU) ||
      in.justPressed(Button::A))
    setScreen(Screen::HOME);
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
        m_working = false;
        m_scanning = false;
        toast(m_groups.empty() ? "Không thấy nhóm nào" : "Thấy " + std::to_string(m_groups.size()) + " nhóm");
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
  header("BrickDrop", Config::instance().alias());
  const char *items[] = {"Gửi file", "Thư mục nhận file", "Nhóm offline (gặp ngoài đường)",
                         "Truyền đang chạy", "Thoát"};
  int y = 120;
  for (int i = 0; i < 5; ++i) {
    row(60, y, W - 120, 64, i == m_sel);
    drawText(items[i], 90, y + 16, i == m_sel ? SDL_Color{0, 0, 0, 255} : C_TEXT,
             m_fMain);
    y += 78;
  }
  std::string st = "IP " + wd.ownIp() + "  |  Nơi nhận: " + Config::instance().saveDir();
  drawText(trunc(st, m_fSmall, W - 120), 60, 560, C_DIM, m_fSmall);
  std::string dev = "Thấy " + std::to_string(LocalSendManager::instance().knownDevices().size()) + " máy quanh đây";
  drawText(dev, 60, 592, C_DIM, m_fSmall);
  footer({{"A", "Chọn"}, {"Y", "Tên mới"}, {"B", "Thoát"}});
}

void App::renderSendPick() {
  header("Chọn file gửi", trunc(m_sendLs.path(), m_fSmall, 500));
  const auto &es = m_sendLs.entries();
  int y0 = 90, rh = 56, vis = 9;
  int total = (int)es.size();
  if (m_sendLs.selected() < m_scroll)
    m_scroll = m_sendLs.selected();
  if (m_sendLs.selected() >= m_scroll + vis)
    m_scroll = m_sendLs.selected() - vis + 1;
  for (int i = 0; i < vis; ++i) {
    int idx = m_scroll + i;
    if (idx >= total)
      break;
    int y = y0 + i * 64;
    row(40, y, W - 80, rh, idx == m_sendLs.selected());
    const auto &e = es[(size_t)idx];
    std::string label = (e.isDir ? "[+] " : "") + e.name;
    drawText(trunc(label, m_fMain, W - 300),
             60, y + 12, idx == m_sendLs.selected() ? SDL_Color{0, 0, 0, 255} : C_TEXT,
             m_fMain);
    if (!e.isDir)
      drawText(DirLister::humanSize(e.size), W - 220, y + 14, C_DIM, m_fSmall);
  }
  if (total == 0)
    drawText("Thư mục trống", W / 2, 300, C_DIM, m_fMain, true);
  footer({{"A", "Vào/Chọn"}, {"B", "Lên/Thoát"}});
}

void App::renderSendDevices() {
  header("Chọn máy nhận", trunc(m_pickFile, m_fSmall, 500));
  int y0 = 110;
  for (size_t i = 0; i < m_devices.size() && i < 8; ++i) {
    int y = y0 + (int)i * 68;
    row(60, y, W - 120, 58, (int)i == m_sel);
    const auto &d = m_devices[i];
    SDL_Color c = ((int)i == m_sel) ? SDL_Color{0, 0, 0, 255} : C_TEXT;
    drawText(trunc(d.alias.empty() ? d.ip : d.alias, m_fMain, 500), 90, y + 4, c,
             m_fMain);
    drawText(d.ip + "  " + d.deviceModel, 90, y + 32, C_DIM, m_fSmall);
  }
  if (m_devices.empty())
    drawText("Chưa thấy máy nào — cùng WiFi rồi bấm Y để quét", W / 2, 300,
             C_DIM, m_fMain, true);
  footer({{"A", "Gửi"}, {"Y", "Quét"}, {"B", "Về"}});
}

void App::renderRecvDir() {
  header("Thư mục nhận file", trunc(m_recvLs.path(), m_fSmall, 500));
  const auto &es = m_recvLs.entries();
  int y0 = 90, rh = 56, vis = 8;
  if (m_recvLs.selected() < m_scroll)
    m_scroll = m_recvLs.selected();
  if (m_recvLs.selected() >= m_scroll + vis)
    m_scroll = m_recvLs.selected() - vis + 1;
  for (int i = 0; i < vis; ++i) {
    int idx = m_scroll + i;
    if (idx >= (int)es.size())
      break;
    int y = y0 + i * 64;
    row(40, y, W - 80, rh, idx == m_recvLs.selected());
    drawText(trunc("[+] " + es[(size_t)idx].name, m_fMain, W - 200), 60, y + 12,
             idx == m_recvLs.selected() ? SDL_Color{0, 0, 0, 255} : C_TEXT,
             m_fMain);
  }
  if (es.empty())
    drawText("(trống — Y để chốt chính thư mục này)", W / 2, 300, C_DIM,
             m_fMain, true);
  // Dòng path cố định: nơi nhận đã chốt (không mất khi hết toast).
  std::string saved = Config::instance().saveDir();
  bool here = (m_recvLs.path() == saved);
  std::string line = "Thư mục nhận file: " + saved + (here ? "  (đang ở đây)" : "");
  drawText(trunc(line, m_fSmall, W - 120), 60, H - FTR - 36, here ? C_OK : C_DIM,
           m_fSmall);
  footer({{"A", "Vào"}, {"Y", "Chốt nơi nhận"}, {"X", "Tạo thư mục"}, {"B", "Về"}});
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
                         ? "Xong"
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
  footer({{"Y", "Dọn xong"}, {"B", "Về"}});
}

void App::renderOffline() {
  auto &wd = WifiDirectManager::instance();
  header("Nhóm offline", wd.mode() == LinkMode::HOST ? "Đang phát: " + wd.groupSsid()
                  : wd.mode() == LinkMode::JOINED ? "Đã vào: " + wd.groupSsid()
                                                  : "Chưa vào nhóm");
  const char *ops[] = {"Phát nhóm (máy này làm chủ)", "Quét nhóm gần đây",
                       "Rời nhóm"};
  int y = 110;
  for (int i = 0; i < 3; ++i) {
    row(60, y, W - 120, 58, i == m_sel);
    drawText(ops[i], 90, y + 12,
             i == m_sel ? SDL_Color{0, 0, 0, 255} : C_TEXT, m_fMain);
    y += 68;
  }
  if (m_scanning || m_working)
    drawText("Đang chạy...", 90, y + 6, C_WARN, m_fMain);
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
  std::vector<std::string> lines;
  lines.push_back("Từ: " + (m_activeReq.fromAlias.empty() ? m_activeReq.fromIp
                                                          : m_activeReq.fromAlias));
  lines.push_back("File: " + f.fileName);
  std::string sz = f.size ? DirLister::humanSize(f.size) : "?";
  lines.push_back("Size: " + sz);
  lines.push_back("Lưu vào: " + Config::instance().saveDir());
  modal("Nhận file?", lines, {{"A", "Nhận"}, {"B", "Từ chối"}});
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
