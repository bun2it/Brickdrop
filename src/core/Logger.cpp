#include "Logger.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace BrickDrop {

Logger &Logger::instance() {
  static Logger inst;
  return inst;
}

void Logger::init(const std::string &path) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_file.open(path, std::ios::app);
  m_init = m_file.is_open();
}

void Logger::write(const char *level, const std::string &msg) {
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::ostringstream ss;
  ss << std::put_time(std::gmtime(&t), "%FT%TZ") << " [" << level << "] " << msg
     << "\n";
  std::string line = ss.str();
  std::lock_guard<std::mutex> lock(m_mutex);
  std::cerr << line;
  if (m_init)
    m_file << line;
}

void Logger::debug(const std::string &msg) { instance().write("DEBUG", msg); }
void Logger::info(const std::string &msg) { instance().write("INFO", msg); }
void Logger::warn(const std::string &msg) { instance().write("WARN", msg); }
void Logger::error(const std::string &msg) { instance().write("ERROR", msg); }
void Logger::flush() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_init)
    m_file.flush();
}

} // namespace BrickDrop
