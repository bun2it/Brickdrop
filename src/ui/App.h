#pragma once
// BrickDrop App — SDL UI 1024x768 cho TrimUI Brick Pro.
// Không nhập liệu: mọi chọn lựa qua D-pad + A/B/X/Y/START.
#include "../explorer/DirLister.h"
#include "../localsend/LocalSendProtocol.h"
#include "../net/BleLinkFlow.h"
#include "../net/WifiDirectManager.h"
#include "../ota/UpdateManager.h"
#include "IconCache.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace BrickDrop {

enum class Screen { HOME, SEND_PICK, SEND_DEVICES, FILES, RECV_DIR, PROGRESS, OFFLINE };

// Chế độ của file picker 1-pane (port từ RomCloud renderLocalSendFolderPicker).
enum class PickMode { SEND_FILE, RECV_DIR, MANAGE };

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
  // 0 = thoát thường; 42 = OTA xong → launch.sh chạy lại bản mới.
  int exitCode() const { return m_exitCode; }
  // Đồng bộ danh bạ đã lưu → trusted peers của LocalSendManager.
  // Gọi khi khởi động và mỗi khi danh bạ đổi (static: main() gọi trước khi
  // App init).
  static void syncTrustedPeers();

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
  // Vào màn chọn file gửi: giữ nguyên thư mục đang duyệt (refresh thay vì
  // open lại từ root) — gửi nhiều file cùng folder không phải đi lại.
  void openSendPicker();
  // Vào màn quản lý file: giữ nguyên thư mục đang duyệt (như openSendPicker).
  void openFiles();
  void restartService();
  void pumpLsQueues();

  void onHome();
  void onSendPick();
  void onSendDevices();
  void onFiles();
  void onRecvDir();
  void onProgress();
  void onOffline();
  void onIncoming(); // modal A/B, gọi cuối frame nếu có prompt

  // File picker 1-pane dùng chung cho SEND_PICK (chọn file gửi) và
  // RECV_DIR (chọn thư mục nhận). pickerInput trả true → về HOME.
  bool pickerInput(DirLister &ls, PickMode mode);
  void renderPicker(DirLister &ls, const std::string &title, PickMode mode);
  void confirmRecvDir(DirLister &ls);

  // Popup menu thao tác file trong explorer (mở bằng SELECT).
  // Điều hướng UP/DOWN, A chọn, B đóng — đúng như Tai yêu cầu.
  enum class OpsAct { PASTE, COPY, CUT, DELETE, UNZIP };
  struct FileClip {
    bool has = false;
    bool cut = false; // true = dời, false = sao chép
    std::string src;
    bool srcIsDir = false;
  };
  bool m_opsOpen = false;
  int m_opsSel = 0;
  std::vector<OpsAct> m_opsActs;
  FileClip m_clip;
  bool m_opsConfirmDelete = false;
  std::string m_opsTarget; // path của entry đang thao tác (copy lúc mở menu)
  bool m_opsTargetIsDir = false;
  void openOpsMenu(DirLister &ls);
  void opsMenuInput(DirLister &ls);
  void renderOpsMenu();
  void doOpsAction(DirLister &ls, OpsAct act);
  void pasteClip(DirLister &ls);

  void renderHome();
  void renderSendPick();
  void renderSendDevices();
  void renderFiles();
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
  DirLister m_fileLs; // explorer "Quản lý file" (giữ vị trí duyệt giữa các lần mở)
  bool m_recvDirsOnly = true;
  IconCache m_icons; // PNG icons (port từ RomCloud drawGridIcon)
  std::string m_pickFile; // file đã chọn để gửi
  std::string m_lastDeviceFp; // fingerprint thiết bị vừa gửi (tự chọn lại)
  // Gửi cả thư mục: folder đã chọn + danh sách file đệ quy (abs, relDir).
  std::string m_pickFolder;
  std::vector<std::pair<std::string, std::string>> m_pickFolderFiles;
  uint64_t m_pickFolderTotal = 0;
  std::vector<LsDeviceInfo> m_devices;

  std::vector<WifiGroup> m_groups;
  bool m_scanning = false;
  bool m_working = false; // op mạng nền đang chạy
  std::string m_workMsg;
  // Phase D: BLE handshake connectionless → Wi-Fi link tự động.
  BleLinkFlow m_bleFlow;
  // Bắt đầu gửi file/thư mục đã stage tới thiết bị LAN (tách từ onSendDevices
  // để BleLinkFlow gọi lại sau khi link Wi-Fi dựng xong).
  void startSendTo(const LsDeviceInfo &target);
  // OTA cập nhật: auto-check lúc mở app (nền, im lặng khi offline).
  std::mutex m_otaMtx;
  bool m_otaAvailable = false;
  OtaInfo m_otaInfo;
  enum class OtaUi { NONE, PROMPT, BUSY } m_otaUi = OtaUi::NONE;
  int m_exitCode = 0; // 42 = OTA xong → launch.sh chạy lại bản mới
  void pumpOta();
  void renderOta();

  // Hàng đợi từ LocalSend callbacks (network threads)
  std::mutex m_qmtx;
  std::deque<LsUploadRequest> m_prompts;
  std::string m_activePrompt; // sessionId đang hiện modal
  LsUploadRequest m_activeReq;
  uint32_t m_lastPromptMs = 0;

  Toast m_toast;
};

} // namespace BrickDrop
