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

  const std::string &path() const { return m_path; }
  const std::vector<DirEntry> &entries() const { return m_entries; }
  int selected() const { return m_sel; }
  void setSelected(int i) { m_sel = i; }

  static std::string humanSize(uint64_t bytes);

private:
  std::string m_path = "/";
  std::vector<DirEntry> m_entries;
  int m_sel = 0;
  bool m_dirsOnly = false;
};

} // namespace BrickDrop
