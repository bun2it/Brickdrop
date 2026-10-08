// BrickDrop main — app chia sẻ file offline cho TrimUI Brick Pro.
// SDL 1024x768, điều khiển tay cầm. B = thoát (giữ MENU cũng thoát).
#include "core/Config.h"
#include "core/Logger.h"
#include "input/Input.h"
#include "localsend/LocalSendManager.h"
#include "net/BleLinkManager.h"
#include "ui/App.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <cstdio>
#include <string>
#include <unistd.h>

using namespace BrickDrop;

static int selftest() {
  // Kiểm tra logic không cần màn hình: Config, DirLister, LocalSend, WiFi.
  Config::instance().init();
  Logger::instance().init(Config::instance().stateDir() + "/selftest.log");
  int fails = 0;
  auto check = [&](bool ok, const std::string &name) {
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", name.c_str());
    if (!ok)
      ++fails;
  };
  check(!Config::instance().sdRoot().empty(), "sdRoot=" + Config::instance().sdRoot());
  check(!Config::instance().alias().empty(), "alias=" + Config::instance().alias());
  check(Config::instance().fingerprint().size() == 64, "fingerprint 64hex (DeviceIdentity)");
  {
    DirLister d;
    check(d.open(Config::instance().sdRoot()), "DirLister open sdRoot");
    printf("  entries: %d (vd %s)\n", (int)d.entries().size(),
           d.entries().empty() ? "-" : d.entries()[0].name.c_str());
  }
  auto &ls = LocalSendManager::instance();
  ls.setAlias(Config::instance().alias());
  check(ls.start(), "LocalSend start");
  sleep(1);
  {
    // BrickDrop dùng kServicePort (53318) để tránh đụng RomCloud (53317).
    std::string url = "curl -m 5 -s http://127.0.0.1:" +
                      std::to_string(LocalSendProto::kServicePort) +
                      "/api/localsend/v2/info";
    FILE *p = popen(url.c_str(), "r");
    char buf[512] = {0};
    if (p) {
      fread(buf, 1, sizeof(buf) - 1, p);
      pclose(p);
    }
    check(std::string(buf).find("\"alias\"") != std::string::npos, "HTTP /info");
  }
  {
    bool sup = WifiDirectManager::instance().supported();
    check(sup, "WiFi AP/P2P support (iw list)");
    printf("  ownIp=%s\n", WifiDirectManager::instance().ownIp().c_str());
  }
  // BLE là optional (thiếu thì app vẫn chạy LAN-only) → chỉ in, không check.
  printf("  BLE hci0: %s\n",
         BleLinkManager::hciPresent() ? "có" : "không (LAN-only)");
  ls.stop();
  printf(fails ? "SELFTEST FAIL (%d)\n" : "SELFTEST PASS\n", fails);
  return fails ? 1 : 0;
}

int main(int argc, char **argv) {
  if (argc > 1 && std::string(argv[1]) == "--selftest")
    return selftest();
  Config::instance().init();
  Logger::instance().init(Config::instance().stateDir() + "/brickdrop.log");
  Logger::info("BrickDrop starting");

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
    Logger::error(std::string("SDL_Init: ") + SDL_GetError());
    return 1;
  }
  if (TTF_Init() != 0) {
    Logger::error("TTF_Init failed");
    SDL_Quit();
    return 1;
  }

  SDL_Window *win = SDL_CreateWindow("BrickDrop", SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED, 1024, 768, 0);
  if (!win) {
    Logger::error(std::string("CreateWindow: ") + SDL_GetError());
    TTF_Quit();
    SDL_Quit();
    return 1;
  }
  SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
  if (!ren)
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
  if (!ren) {
    SDL_DestroyWindow(win);
    TTF_Quit();
    SDL_Quit();
    return 1;
  }

  Input::instance().init();

  auto &ls = LocalSendManager::instance();
  ls.setAlias(Config::instance().alias());
  // Chế độ hiển thị kiểu AirDrop + danh bạ đã lưu (áp trước khi start).
  ls.setVisibility((LsVisibility)Config::instance().visibility());
  App::syncTrustedPeers();
  // Nơi nhận mặc định = saveDir đã chốt (qua setTargetFolder ở màn RECV_DIR).
  {
    std::string rel = Config::instance().saveDir();
    std::string root = Config::instance().sdRoot();
    if (rel.compare(0, root.size(), root) == 0)
      rel = rel.substr(root.size());
    ls.setTargetFolder(rel);
  }
  if (!ls.start())
    Logger::error("LocalSend failed to start (continuing without P2P)");

  App app;
  if (!app.init(win, ren)) {
    Logger::error("App init failed (font?)");
    ls.stop();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    TTF_Quit();
    SDL_Quit();
    return 1;
  }

  while (!app.shouldExit()) {
    if (!Input::instance().poll())
      break;
    app.frame();
    SDL_Delay(16);
  }

  // Điều kiện của Tai: thoát app → khôi phục WiFi gốc NGAY để có internet
  // (xóa profile BrickDrop-*, về lại SSID đã lưu lúc mở app).
  {
    std::string wmsg;
    WifiDirectManager::instance().restoreOriginalWifi(wmsg);
    Logger::info("BrickDrop exit wifi: " + wmsg);
  }

  app.shutdown();
  ls.stop();
  Input::instance().shutdown();
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  TTF_Quit();
  SDL_Quit();
  Logger::info("BrickDrop exit");
  // 42 = OTA cập nhật xong → launch.sh chạy lại bản mới.
  return app.exitCode();
}
