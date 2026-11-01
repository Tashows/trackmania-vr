#include "crash_report.h"
#include "log.h"

#include <windows.h>
#include <dbghelp.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace tmvr {
namespace {

constexpr size_t kTopFrames = 64;
constexpr size_t kBottomFrames = 24;
constexpr size_t kScanSequence = 160;

struct CodeRange {
    uintptr_t start;
    size_t length;
    char name[48];
};

HANDLE g_requestEvent = nullptr;
HANDLE g_doneEvent = nullptr;
EXCEPTION_POINTERS* g_exception = nullptr;
DWORD g_faultingThread = 0;
ULONG_PTR g_stackLow = 0;
ULONG_PTR g_stackHigh = 0;
std::atomic<bool> g_reported{false};
bool g_installed = false;
thread_local bool t_guaranteed = false;
CodeRange g_ranges[8];
std::atomic<int> g_rangeCount{0};

const CodeRange* FindRange(uintptr_t address)
{
    const int count = g_rangeCount.load();
    for (int i = 0; i < count; ++i) {
        if (address >= g_ranges[i].start && address < g_ranges[i].start + g_ranges[i].length) {
            return &g_ranges[i];
        }
    }
    return nullptr;
}

// A value on the stack looks like a return address if the bytes before it are a call.
bool LooksLikeReturnAddress(uintptr_t value)
{
    __try {
        const auto* p = reinterpret_cast<const uint8_t*>(value);
        if (p[-5] == 0xE8) return true;                                  // call rel32
        if (p[-6] == 0xFF && p[-5] == 0x15) return true;                 // call [rip+disp32]
        if (p[-2] == 0xFF && (p[-1] & 0xF8) == 0xD0) return true;        // call reg
        if (p[-3] == 0x41 && p[-2] == 0xFF && (p[-1] & 0xF8) == 0xD0) return true; // call r8-r15
        if (p[-3] == 0xFF && (p[-2] & 0xF8) == 0x50) return true;        // call [reg+disp8]
        if (p[-6] == 0xFF && (p[-5] & 0xF8) == 0x90) return true;        // call [reg+disp32]
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
}

bool IsCode(uintptr_t value)
{
    if (value < 0x10000) {
        return false;
    }
    if (FindRange(value)) {
        return true;
    }
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(value), &module)) {
        return false;
    }
    return LooksLikeReturnAddress(value);
}

uintptr_t ReadStackSlot(uintptr_t address, bool& ok)
{
    __try {
        ok = true;
        return *reinterpret_cast<const uintptr_t*>(address);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
        return 0;
    }
}

std::string ModuleOf(uintptr_t address)
{
    char text[128];
    DescribeCode(reinterpret_cast<const void*>(address), text, sizeof(text));
    std::string s = text;
    const size_t plus = s.find('+');
    return plus == std::string::npos ? s : s.substr(0, plus);
}

// Walks every 8-byte slot between the faulting stack pointer and the top of the stack and
// reports the ones that look like return addresses. Unlike StackWalk64 this keeps going
// through code without unwind data (such as another overlay's hook stub).
void ScanStack(uintptr_t rsp)
{
    std::vector<uintptr_t> hits;
    for (uintptr_t a = rsp & ~uintptr_t(7); a + 8 <= g_stackHigh; a += 8) {
        bool ok = false;
        const uintptr_t value = ReadStackSlot(a, ok);
        if (ok && IsCode(value)) {
            hits.push_back(value);
        }
    }

    Log("Stack scan: %zu KB used of %zu KB, %zu return addresses",
        size_t((g_stackHigh - rsp) / 1024), size_t((g_stackHigh - g_stackLow) / 1024), hits.size());

    std::map<std::string, size_t> perModule;
    std::map<uintptr_t, size_t> perAddress;
    for (uintptr_t h : hits) {
        perModule[ModuleOf(h)]++;
        perAddress[h]++;
    }
    for (const auto& [module, count] : perModule) {
        Log("  %6zu  %s", count, module.c_str());
    }

    std::vector<std::pair<size_t, uintptr_t>> top;
    for (const auto& [address, count] : perAddress) {
        top.emplace_back(count, address);
    }
    std::sort(top.rbegin(), top.rend());
    Log("Most repeated return addresses:");
    char text[160];
    for (size_t i = 0; i < top.size() && i < 16; ++i) {
        DescribeCode(reinterpret_cast<const void*>(top[i].second), text, sizeof(text));
        Log("  %6zu x %s", top[i].first, text);
    }

    Log("Return addresses from the innermost frame outward (repeats collapsed):");
    for (size_t i = 0, shown = 0; i < hits.size() && shown < kScanSequence; ++shown) {
        size_t run = 1;
        while (i + run < hits.size() && hits[i + run] == hits[i]) {
            ++run;
        }
        DescribeCode(reinterpret_cast<const void*>(hits[i]), text, sizeof(text));
        if (run > 1) {
            Log("  [%zu] %s (x%zu)", i, text, run);
        } else {
            Log("  [%zu] %s", i, text);
        }
        i += run;
    }
    if (hits.size() > kScanSequence) {
        Log("  ... outermost:");
        for (size_t i = hits.size() > 16 ? hits.size() - 16 : 0; i < hits.size(); ++i) {
            DescribeCode(reinterpret_cast<const void*>(hits[i]), text, sizeof(text));
            Log("  [%zu] %s", i, text);
        }
    }
}

void WriteReport()
{
    HANDLE process = GetCurrentProcess();
    HANDLE thread = OpenThread(THREAD_ALL_ACCESS, FALSE, g_faultingThread);
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);

    CONTEXT context = *g_exception->ContextRecord;
    const uintptr_t rsp = context.Rsp;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    std::vector<uint64_t> frames;
    while (frames.size() < 200000 &&
        StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr,
            SymFunctionTableAccess64, SymGetModuleBase64, nullptr) &&
        frame.AddrPC.Offset != 0) {
        frames.push_back(frame.AddrPC.Offset);
    }

    LogError("Stack overflow on thread %lu (%zu walkable frames). Call stack, innermost first:",
        g_faultingThread, frames.size());
    char text[512];
    for (size_t i = 0; i < frames.size(); ++i) {
        if (i == kTopFrames && frames.size() > kTopFrames + kBottomFrames) {
            Log("  ... %zu frames skipped ...", frames.size() - kTopFrames - kBottomFrames);
            i = frames.size() - kBottomFrames;
        }
        DescribeCode(reinterpret_cast<const void*>(frames[i]), text, sizeof(text));
        Log("  #%zu %s", i, text);
    }

    ScanStack(rsp);

    SymCleanup(process);
    if (thread) {
        CloseHandle(thread);
    }
}

LONG CALLBACK OnException(EXCEPTION_POINTERS* info)
{
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_STACK_OVERFLOW || g_reported.exchange(true)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    // Keep this frame tiny: hand off to the reporter thread and wait for it.
    g_exception = info;
    g_faultingThread = GetCurrentThreadId();
    GetCurrentThreadStackLimits(&g_stackLow, &g_stackHigh);
    SetEvent(g_requestEvent);
    WaitForSingleObject(g_doneEvent, 30000);
    return EXCEPTION_CONTINUE_SEARCH;
}

DWORD WINAPI ReporterThread(void*)
{
    WaitForSingleObject(g_requestEvent, INFINITE);
    WriteReport();
    SetEvent(g_doneEvent);
    return 0;
}

} // namespace

void InstallCrashReporter()
{
    if (g_installed) {
        return;
    }
    g_requestEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_doneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE thread = CreateThread(nullptr, 1 << 20, ReporterThread, nullptr, 0, nullptr);
    if (!g_requestEvent || !g_doneEvent || !thread) {
        LogError("Could not start the crash reporter");
        return;
    }
    CloseHandle(thread);
    AddVectoredExceptionHandler(1, OnException);
    g_installed = true;
}

void EnsureStackGuarantee()
{
    if (t_guaranteed) {
        return;
    }
    ULONG size = 32 * 1024;
    SetThreadStackGuarantee(&size);
    t_guaranteed = true;
}

void RegisterCodeRange(const void* start, size_t length, const char* name)
{
    const int index = g_rangeCount.load();
    if (index >= int(std::size(g_ranges))) {
        return;
    }
    g_ranges[index].start = reinterpret_cast<uintptr_t>(start);
    g_ranges[index].length = length;
    strncpy_s(g_ranges[index].name, name, _TRUNCATE);
    g_rangeCount = index + 1;
}

void DescribeCode(const void* address, char* out, size_t size)
{
    const auto value = reinterpret_cast<uintptr_t>(address);
    if (const CodeRange* range = FindRange(value)) {
        snprintf(out, size, "%s+0x%llx", range->name, static_cast<unsigned long long>(value - range->start));
        return;
    }
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(address), &module)) {
        snprintf(out, size, "?%p", address);
        return;
    }
    char path[MAX_PATH] = {};
    GetModuleFileNameA(module, path, MAX_PATH);
    const char* slash = strrchr(path, '\\');
    snprintf(out, size, "%s+0x%llx", slash ? slash + 1 : path,
        static_cast<unsigned long long>(value - reinterpret_cast<uintptr_t>(module)));
}

} // namespace tmvr
