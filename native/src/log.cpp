#include "log.h"
#include "shared_block.h"

#include <windows.h>
#include <share.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace tmvr {
namespace {

std::mutex g_mutex;
FILE* g_file = nullptr;

void Write(const char* level, const char* fmt, va_list args, bool isError)
{
    char msg[1024];
    vsnprintf(msg, sizeof(msg), fmt, args);
    if (isError) {
        strncpy_s(Shared().lastError, msg, _TRUNCATE);
    }

    SYSTEMTIME t;
    GetLocalTime(&t);
    std::lock_guard lock(g_mutex);
    if (g_file) {
        fprintf(g_file, "[%02d:%02d:%02d.%03d] [%s] %s\n", t.wHour, t.wMinute, t.wSecond,
            t.wMilliseconds, level, msg);
        fflush(g_file);
    }
    OutputDebugStringA("[TrackmaniaVR] ");
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
}

} // namespace

void LogOpen(const std::wstring& path)
{
    std::lock_guard lock(g_mutex);
    if (!g_file) {
        g_file = _wfsopen(path.c_str(), L"w", _SH_DENYWR);
    }
}

void LogClose()
{
    std::lock_guard lock(g_mutex);
    if (g_file) {
        fclose(g_file);
        g_file = nullptr;
    }
}

void Log(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    Write("info", fmt, args, false);
    va_end(args);
}

void LogError(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    Write("ERROR", fmt, args, true);
    va_end(args);
}

} // namespace tmvr
