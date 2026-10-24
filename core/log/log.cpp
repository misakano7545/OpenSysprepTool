#include "core/log.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace osp::log {

namespace {

std::mutex g_mutex;
std::ofstream g_file;
std::string g_lastError;

const char* levelTag(Level level)
{
    switch (level)
    {
    case Level::Debug:
        return "DEBUG";
    case Level::Info:
        return "INFO ";
    case Level::Warn:
        return "WARN ";
    case Level::Error:
        return "ERROR";
    }
    return "INFO ";
}

std::string timestamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);

    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch())
                        .count() %
                    1000;

    char buffer[64]{};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                  local.tm_year + 1900,
                  local.tm_mon + 1,
                  local.tm_mday,
                  local.tm_hour,
                  local.tm_min,
                  local.tm_sec,
                  static_cast<int>(ms));
    return std::string(buffer);
}

std::filesystem::path toPath(const std::string& utf8Path)
{
    const std::u8string u8(reinterpret_cast<const char8_t*>(utf8Path.data()), utf8Path.size());
    return std::filesystem::path(u8);
}

} // namespace

bool openFile(const std::string& utf8Path)
{
    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_file.is_open())
    {
        g_file.close();
    }

    const std::filesystem::path path = toPath(utf8Path);

    std::error_code ec;
    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
        {
            g_lastError = "create_directories failed: " + ec.message();
            return false;
        }
    }

    g_file.open(path, std::ios::out | std::ios::app | std::ios::binary);
    if (!g_file.is_open())
    {
        g_lastError = "cannot open log file for append";
        return false;
    }

    g_lastError.clear();
    return true;
}

void close()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file.is_open())
    {
        g_file.flush();
        g_file.close();
    }
}

bool isOpen()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_file.is_open();
}

void write(Level level, const std::string& message)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_file.is_open())
    {
        return;
    }

    g_file << timestamp() << " [" << levelTag(level) << "] " << message << '\n';
    g_file.flush();
}

void debug(const std::string& message)
{
    write(Level::Debug, message);
}

void info(const std::string& message)
{
    write(Level::Info, message);
}

void warn(const std::string& message)
{
    write(Level::Warn, message);
}

void error(const std::string& message)
{
    write(Level::Error, message);
}

std::string lastError()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_lastError;
}

} // namespace osp::log
