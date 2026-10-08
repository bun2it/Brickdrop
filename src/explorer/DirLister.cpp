#include "DirLister.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
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

uint64_t DirLister::diskFree(const std::string &path) {
  struct statvfs sv;
  if (statvfs(path.c_str(), &sv) != 0)
    return 0;
  return (uint64_t)sv.f_bavail * (uint64_t)sv.f_frsize;
}

// =============================================================
// Thao tác file (popup menu trong explorer)
// =============================================================
std::string DirLister::baseName(const std::string &p) {
  size_t i = p.find_last_of('/');
  return (i == std::string::npos) ? p : p.substr(i + 1);
}

std::string DirLister::dirName(const std::string &p) {
  size_t i = p.find_last_of('/');
  if (i == std::string::npos)
    return ".";
  if (i == 0)
    return "/";
  return p.substr(0, i);
}

bool DirLister::removeRec(const std::string &path) {
  struct stat st;
  if (lstat(path.c_str(), &st) != 0)
    return false;
  // Symlink: chỉ unlink, không đi theo.
  if (S_ISLNK(st.st_mode))
    return ::unlink(path.c_str()) == 0;
  if (!S_ISDIR(st.st_mode))
    return ::unlink(path.c_str()) == 0;
  DIR *d = opendir(path.c_str());
  if (!d)
    return false;
  bool ok = true;
  struct dirent *de;
  while ((de = readdir(d))) {
    std::string n = de->d_name;
    if (n == "." || n == "..")
      continue;
    if (!removeRec(path + "/" + n))
      ok = false;
  }
  closedir(d);
  if (::rmdir(path.c_str()) != 0)
    ok = false;
  return ok;
}

namespace {
bool copyFile(const std::string &src, const std::string &dst) {
  FILE *in = fopen(src.c_str(), "rb");
  if (!in)
    return false;
  FILE *out = fopen(dst.c_str(), "wb");
  if (!out) {
    fclose(in);
    return false;
  }
  char buf[65536];
  bool ok = true;
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
    if (fwrite(buf, 1, n, out) != n) {
      ok = false;
      break;
    }
  }
  if (ferror(in))
    ok = false;
  fclose(in);
  if (fclose(out) != 0)
    ok = false;
  // Giữ nguyên quyền của file gốc.
  if (ok) {
    struct stat st;
    if (stat(src.c_str(), &st) == 0)
      chmod(dst.c_str(), st.st_mode & 0777);
  }
  return ok;
}
} // namespace

bool DirLister::copyRec(const std::string &src, const std::string &dst) {
  struct stat st;
  if (lstat(src.c_str(), &st) != 0)
    return false;
  if (S_ISLNK(st.st_mode))
    return true; // bỏ qua symlink
  if (!S_ISDIR(st.st_mode))
    return copyFile(src, dst);
  if (::mkdir(dst.c_str(), 0755) != 0)
    return false;
  DIR *d = opendir(src.c_str());
  if (!d) {
    removeRec(dst);
    return false;
  }
  bool ok = true;
  struct dirent *de;
  while ((de = readdir(d))) {
    std::string n = de->d_name;
    if (n == "." || n == "..")
      continue;
    if (!copyRec(src + "/" + n, dst + "/" + n)) {
      ok = false;
      break;
    }
  }
  closedir(d);
  if (!ok)
    removeRec(dst); // dọn bản copy dở dang
  return ok;
}

bool DirLister::movePath(const std::string &src, const std::string &dst) {
  if (::rename(src.c_str(), dst.c_str()) == 0)
    return true;
  if (errno != EXDEV)
    return false;
  // Khác filesystem (vd thẻ nhớ -> bộ nhớ trong): copy rồi xoá gốc.
  if (!copyRec(src, dst))
    return false;
  return removeRec(src);
}

bool DirLister::unzipToDir(const std::string &zipPath, const std::string &dir) {
  // Như postProcessUpload: fork+exec trực tiếp, không qua shell (chống RCE
  // vì tên file do sender/user kiểm soát).
  pid_t pid = fork();
  if (pid < 0)
    return false;
  if (pid == 0) {
    execlp("unzip", "unzip", "-o", "-qq", zipPath.c_str(), "-d",
           dir.c_str(), (char *)nullptr);
    _exit(127); // không có binary unzip
  }
  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

std::string DirLister::suggestName(const std::string &name) const {
  if (access((m_path + "/" + name).c_str(), F_OK) != 0)
    return name;
  std::string stem = name, ext;
  size_t dot = name.find_last_of('.');
  if (dot != std::string::npos && dot != 0) {
    stem = name.substr(0, dot);
    ext = name.substr(dot);
  }
  for (int i = 2;; ++i) {
    std::string n = stem + " " + std::to_string(i) + ext;
    if (access((m_path + "/" + n).c_str(), F_OK) != 0)
      return n;
  }
}

} // namespace BrickDrop
