#pragma once
// BrickDrop App — SDL UI 1024x768 cho TrimUI Brick Pro.
// Không nhập liệu: mọi chọn lựa qua D-pad + A/B/X/Y/START.
#include "../explorer/DirLister.h"
#include "../localsend/LocalSendProtocol.h"
#include "../net/WifiDirectManager.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace BrickDrop {

enum class Screen { HOME, SEND_PICK, SEND_DEVICES, RECV_DIR, PROGRESS, OFFLINE };

struct Toast {
  std::string msg;
  uint32_t until = 0;
};

class App {
public:
  bool init(SDL_Window *win, SDL_Renderer *ren);
  void shutdown();
  void frame(); // 1 frame: input + render
  bool shouldExit() const { return m_exit; }

private:
  // --- helpers ---
  void toast(const std::string &msg, uint32_t ms = 2500);
  void drawText(const std::string &t, int x, int y, SDL_Color c, TTF_Font *f,
                bool centered = false);
  int textW(const std::string &t, TTF_Font *f);
  std::string trunc(const std::string &t, TTF_Font *f, int maxPx);
  void rect(int x, int y, int w, int h, SDL_Color c, bool fill = true);
  void header(const std::string &title, const std::string &sub = "");
  void footer(const std::vector<std::pair<std::string, std::string>> &hints);
  void row(int x, int y, int w, int h, bool sel);
  void progressBar(int x, int y, int w, int h, double frac);
  void modal(const std::string &title, const std::vector<std::string> &lines,
             const std::vector<std::pair<std::string, std::string>> &hints);

  void setScreen(Screen s);
  void refreshDevices();
  void restartService();
  void pumpLsQueues();

  void onHome();
  void onSendPick();
  void onSendDevices();
  void onRecvDir();
  void onProgress();
  void onOffline();
  void onIncoming(); // modal A/B, gọi cuối frame nếu có prompt

  void renderHome();
  void renderSendPick();
  void renderSendDevices();
  void renderRecvDir();
  void renderProgress();
  void renderOffline();
  void renderIncoming();

  SDL_Renderer *m_ren = nullptr;
  TTF_Font *m_fTitle = nullptr;
  TTF_Font *m_fMain = nullptr;
  TTF_Font *m_fSmall = nullptr;
  bool m_exit = false;
  Screen m_screen = Screen::HOME;
  int m_sel = 0;
  int m_scroll = 0;

  DirLister m_sendLs;
  DirLister m_recvLs;
  bool m_recvDirsOnly = true;
  std::string m_pickFile; // file đã chọn để gửi
  std::vector<LsDeviceInfo> m_devices;

  std::vector<WifiGroup> m_groups;
  bool m_scanning = false;
  bool m_working = false; // op mạng nền đang chạy
  std::string m_workMsg;

  // Hàng đợi từ LocalSend callbacks (network threads)
  std::mutex m_qmtx;
  std::deque<LsUploadRequest> m_prompts;
  std::string m_activePrompt; // sessionId đang hiện modal
  LsUploadRequest m_activeReq;
  uint32_t m_lastPromptMs = 0;

  Toast m_toast;
};

} // namespace BrickDrop
