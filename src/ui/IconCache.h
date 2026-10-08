#pragma once
// BrickDrop IconCache — load PNG icon (SDL2_image), cache LRU, vẽ aspect-fit.
// Port gọn từ RomCloud drawGridIcon: bỏ ImageCache share + PlatformInfo scale
// (BrickDrop cố định 1024x768).
#include <SDL2/SDL.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace BrickDrop {

class IconCache {
public:
  // assetsDir rỗng → tự resolve (thử "assets", deploy path, "../assets").
  bool init(SDL_Renderer *ren, const std::string &assetsDir = "");
  void shutdown();
  // Vẽ icon (vd "icons/FOLDER.png") vừa khít trong box (x,y,w,h), giữ tỉ lệ,
  // căn giữa. Thiếu file → vẽ rect xám fallback, không crash.
  void draw(const std::string &relPath, int x, int y, int w, int h);

private:
  SDL_Texture *get(const std::string &relPath);
  void evictOldest();

  SDL_Renderer *m_ren = nullptr;
  std::string m_assetsDir;
  std::unordered_map<std::string, SDL_Texture *> m_cache;
  std::vector<std::string> m_order; // LRU: cũ nhất ở đầu
  static constexpr size_t kMaxEntries = 64;
};

} // namespace BrickDrop
