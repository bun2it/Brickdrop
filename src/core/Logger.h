#pragma once
// BrickDrop Logger — minimal, file + stderr. API tương thích Logger RomCloud
// (static debug/info/warn/error) để tái dùng LocalSendManager không sửa nhiều.
#include <fstream>
#include <mutex>
#include <string>

namespace BrickDrop {

class Logger {
public:
  static Logger &instance();
  void init(const std::string &path);
  static void debug(const std::string &msg);
  static void info(const std::string &msg);
  static void warn(const std::string &msg);
  static void error(const std::string &msg);
  void flush();

private:
  Logger() = default;
  void write(const char *level, const std::string &msg);
  std::ofstream m_file;
  std::mutex m_mutex;
  bool m_init = false;
};

} // namespace BrickDrop
