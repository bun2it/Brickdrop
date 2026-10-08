#pragma once
// BrickDrop DirLister — duyệt thư mục POSIX thuần, không SDL.
// Logic port từ RomCloud FileExplorer/FileSystemManager (dirs-first, alpha),
// bỏ clipboard/archive/keyboard (BrickDrop không nhập liệu: tạo folder auto-tên).
#include <cstdint>
#include <string>
#include <vector>

namespace BrickDrop {

struct DirEntry {
  std::string name;
  std::string path;
  bool isDir = false;
  uint64_t size = 0;
};

class DirLister {
public:
  bool open(const std::string &path); // normalize + refresh
  bool refresh();
  bool refreshDirsOnly(bool v) {
    m_dirsOnly = v;
    return refresh();
  }
  void moveSel(int d);
  const DirEntry *current() const;
  bool enter(); // vào folder đang chọn
  bool goUp();
  bool mkdir(const std::string &name);
  // Tên gợi ý không trùng: "BrickDrop", "BrickDrop 2", ...
  std::string suggestFolderName(const std::string &base) const;
  // Tên file/folder không trùng trong thư mục hiện tại: "a.ext" ->
  // "a 2.ext", "Games" -> "Games 2".
  std::string suggestName(const std::string &name) const;
  // --- Thao tác file (popup menu trong explorer) ---
  // Xoá file/thư mục đệ quy (không đi theo symlink). false nếu lỗi.
  static bool removeRec(const std::string &path);
  // Copy file/thư mục đệ quy vào dst (dst chưa tồn tại). Bỏ qua symlink,
  // tự dọn dst dở dang nếu lỗi.
  static bool copyRec(const std::string &src, const std::string &dst);
  // Dời/đổi tên: rename, fallback copy+xoá khi khác filesystem.
  static bool movePath(const std::string &src, const std::string &dst);
  // Bung file zip ra thư mục dir (gọi binary unzip của hệ thống).
  static bool unzipToDir(const std::string &zipPath, const std::string &dir);
  static std::string baseName(const std::string &p);
  static std::string dirName(const std::string &p);

  const std::string &path() const { return m_path; }
  const std::vector<DirEntry> &entries() const { return m_entries; }
  int selected() const { return m_sel; }
  void setSelected(int i) { m_sel = i; }

  static std::string humanSize(uint64_t bytes);
  // Dung lượng trống (bytes) của filesystem chứa path. 0 nếu lỗi.
  static uint64_t diskFree(const std::string &path);

private:
  std::string m_path = "/";
  std::vector<DirEntry> m_entries;
  int m_sel = 0;
  bool m_dirsOnly = false;
};

} // namespace BrickDrop
