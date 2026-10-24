#pragma once

#include <string>

namespace osp::log {

enum class Level
{
    Debug,
    Info,
    Warn,
    Error
};

// 打开日志文件（UTF-8 路径，追加模式）。重复调用会先关闭旧文件。
bool openFile(const std::string& utf8Path);

// 关闭日志文件；未打开时无副作用
void close();

bool isOpen();

// 写入一行，自动加时间戳与级别前缀
void write(Level level, const std::string& message);

void debug(const std::string& message);
void info(const std::string& message);
void warn(const std::string& message);
void error(const std::string& message);

// 最近一次失败的原因（openFile 返回 false 时可用）
std::string lastError();

} // namespace osp::log
