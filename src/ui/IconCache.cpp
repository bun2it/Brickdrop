#include "IconCache.h"
#include <SDL2/SDL_image.h>
#include <algorithm>
#include <cmath>

namespace BrickDrop {

bool IconCache::init(SDL_Renderer *ren, const std::string &assetsDir) {
  m_ren = ren;
  if (!m_ren)
    return false;
  IMG_Init(IMG_INIT_PNG);
  if (!assetsDir.empty()) {
    m_assetsDir = assetsDir;
    return true;
  }
  // Tự resolve giống cách App tìm font: ưu tiên CWD (dev), rồi deploy path.
  const char *cands[] = {"assets", "/mnt/SDCARD/Apps/BrickDrop/assets",
                         "../assets", nullptr};
  for (int i = 0; cands[i]; ++i) {
    std::string probe = std::string(cands[i]) + "/icons/FOLDER.png";
    SDL_RWops *r = SDL_RWFromFile(probe.c_str(), "rb");
    if (r) {
      SDL_RWclose(r);
      m_assetsDir = cands[i];
      break;
    }
  }
  if (m_assetsDir.empty())
    m_assetsDir = "assets"; // không tìm thấy: draw() fallback rect xám
  return true;
}

void IconCache::shutdown() {
  for (auto &kv : m_cache)
    if (kv.second)
      SDL_DestroyTexture(kv.second);
  m_cache.clear();
  m_order.clear();
  m_ren = nullptr;
  IMG_Quit();
}

void IconCache::evictOldest() {
  if (m_order.empty())
    return;
  std::string k = m_order.front();
  m_order.erase(m_order.begin());
  auto it = m_cache.find(k);
  if (it != m_cache.end()) {
    if (it->second)
      SDL_DestroyTexture(it->second);
    m_cache.erase(it);
  }
}

SDL_Texture *IconCache::get(const std::string &relPath) {
  auto it = m_cache.find(relPath);
  if (it != m_cache.end()) {
    // Refresh LRU: đưa về cuối.
    auto oi = std::find(m_order.begin(), m_order.end(), relPath);
    if (oi != m_order.end()) {
      m_order.erase(oi);
      m_order.push_back(relPath);
    }
    return it->second; // có thể nullptr (file thiếu) → caller fallback
  }
  SDL_Texture *tx = nullptr;
  std::string full = m_assetsDir + "/" + relPath;
  if (SDL_Surface *surf = IMG_Load(full.c_str())) {
    tx = SDL_CreateTextureFromSurface(m_ren, surf);
    SDL_FreeSurface(surf);
  }
  // Cache cả kết quả âm (nullptr) để không IMG_Load lại mỗi frame.
  if (m_cache.size() >= kMaxEntries)
    evictOldest();
  m_cache[relPath] = tx;
  m_order.push_back(relPath);
  return tx;
}

void IconCache::draw(const std::string &relPath, int x, int y, int w, int h) {
  if (!m_ren || w <= 0 || h <= 0)
    return;
  SDL_Texture *tx = get(relPath);
  if (!tx) {
    // Fallback: rect xám (giống RomCloud drawGridIcon khi thiếu file).
    SDL_SetRenderDrawColor(m_ren, 60, 70, 85, 255);
    SDL_Rect r{x, y, w, h};
    SDL_RenderFillRect(m_ren, &r);
    return;
  }
  int tw = 0, th = 0;
  SDL_QueryTexture(tx, nullptr, nullptr, &tw, &th);
  if (tw <= 0 || th <= 0)
    return;
  // Aspect-fit, căn giữa trong box (port blitFit của RomCloud).
  float s = std::min((float)w / (float)tw, (float)h / (float)th);
  int dw = (int)(tw * s), dh = (int)(th * s);
  SDL_Rect dst{x + (w - dw) / 2, y + (h - dh) / 2, dw, dh};
  SDL_RenderCopy(m_ren, tx, nullptr, &dst);
}

} // namespace BrickDrop
