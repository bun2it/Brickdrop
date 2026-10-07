#include "DirLister.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace BrickDrop {

static bool isJunk(const std::string &n) {
  if (n == ".DS_Store" || n == "Thumbs.db")
    return true;
  return n.size() > 2 && n[0] == '.' && n[1] == '_';
}

bool DirLister::open(const std::string &path) {
  m_path = path.empty() ? "/" : path;
  while (m_path.size() > 1 && m_path.back() == '/')
    m_path.pop_back();
  m_sel = 0;
  return refresh();
}

bool DirLister::refresh() {
  m_entries.clear();
  DIR *d = opendir(m_path.c_str());
  if (!d)
    return false;
  struct dirent *de;
  while ((de = readdir(d))) {
    std::string n = de->d_name;
    if (n == "." || n == ".." || isJunk(n))
      continue;
    if (!n.empty() && n[0] == '.')
      continue; // ẩn file dot
    std::string full = m_path + "/" + n;
    struct stat st;
    if (stat(full.c_str(), &st) != 0)
      continue;
    bool isDir = S_ISDIR(st.st_mode);
    if (m_dirsOnly && !isDir)
      continue;
    DirEntry e;
    e.name = n;
    e.path = full;
    e.isDir = isDir;
    e.size = isDir ? 0 : (uint64_t)st.st_size;
    m_entries.push_back(std::move(e));
  }
  closedir(d);
  std::sort(m_entries.begin(), m_entries.end(), [](const DirEntry &a, const DirEntry &b) {
    if (a.isDir != b.isDir)
      return a.isDir > b.isDir;
    std::string al = a.name, bl = b.name;
    for (auto &c : al)
      c = tolower((unsigned char)c);
    for (auto &c : bl)
      c = tolower((unsigned char)c);
    return al < bl;
  });
  if (m_sel >= (int)m_entries.size())
    m_sel = (int)m_entries.size() - 1;
  if (m_sel < 0)
    m_sel = 0;
  return true;
}

void DirLister::moveSel(int d) {
  if (m_entries.empty()) {
    m_sel = 0;
    return;
  }
  m_sel += d;
  if (m_sel < 0)
    m_sel = 0;
  if (m_sel >= (int)m_entries.size())
    m_sel = (int)m_entries.size() - 1;
}

const DirEntry *DirLister::current() const {
  if (m_sel < 0 || m_sel >= (int)m_entries.size())
    return nullptr;
  return &m_entries[(size_t)m_sel];
}

bool DirLister::enter() {
  const DirEntry *e = current();
  if (!e || !e->isDir)
    return false;
  m_path = e->path;
  m_sel = 0;
  return refresh();
}

bool DirLister::goUp() {
  if (m_path.empty() || m_path == "/")
    return false;
  size_t p = m_path.find_last_of('/');
  m_path = (p == std::string::npos || p == 0) ? "/" : m_path.substr(0, p);
  m_sel = 0;
  return refresh();
}

bool DirLister::mkdir(const std::string &name) {
  if (name.empty() || name.find('/') != std::string::npos)
    return false;
  std::string dst = m_path + "/" + name;
  struct stat st;
  if (stat(dst.c_str(), &st) == 0)
    return false;
  if (::mkdir(dst.c_str(), 0755) != 0)
    return false;
  return refresh();
}

std::string DirLister::suggestFolderName(const std::string &base) const {
  if (access((m_path + "/" + base).c_str(), F_OK) != 0)
    return base;
  for (int i = 2;; ++i) {
    std::string n = base + " " + std::to_string(i);
    if (access((m_path + "/" + n).c_str(), F_OK) != 0)
      return n;
  }
}

std::string DirLister::humanSize(uint64_t bytes) {
  const char *u[] = {"B", "KB", "MB", "GB", "TB"};
  double v = (double)bytes;
  int i = 0;
  while (v >= 1024.0 && i < 4) {
    v /= 1024.0;
    ++i;
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "%.1f %s", v, u[i]);
  return buf;
}

} // namespace BrickDrop
