#pragma once
#include <string>

namespace tmvr {

void LogOpen(const std::wstring& path);
void LogClose();
void Log(const char* fmt, ...);
// Logs and also copies the message into SharedBlock::lastError for the plugin's status window.
void LogError(const char* fmt, ...);

} // namespace tmvr
