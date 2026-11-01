#include "present_hook.h"
#include "crash_report.h"
#include "log.h"
#include "shared_block.h"
#include "xr_runtime.h"

#include <intrin.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdio>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace tmvr {
namespace {

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);

constexpr size_t kPresentSlot = 8;   // IUnknown(3) + IDXGIObject(4) + IDXGIDeviceSubObject(1)
constexpr size_t kPresent1Slot = 22; // IDXGISwapChain(18) + GetDesc1, GetFullscreenDesc, GetHwnd, GetCoreWindow

// Openplanet (and other overlays) patch the same shared vtable slot. Calling "whatever was in
// the slot before us" unconditionally loops forever once such a hook patches over ours and
// chains back to us, so each hook knows both the previous slot value and DXGI's real function.
struct Slot {
    void** address = nullptr;
    void* hook = nullptr;
    void* previous = nullptr; // slot value before we patched it (maybe another overlay's hook)
    void* real = nullptr;     // DXGI's own implementation
};

Slot g_present;
Slot g_present1;
bool g_patched = false;             // the vtable stays patched until the process exits
std::atomic<bool> g_active{false};  // false = hooks only pass calls through
std::atomic<int> g_handling{0};
std::atomic<int> g_callLogBudget{24};
std::atomic<bool> g_recursionLogged{false};
thread_local int t_depth = 0;

// Nested calls deeper than this can only be a loop between hooks; break it instead of crashing.
constexpr int kMaxDepth = 6;

struct DepthScope {
    DepthScope() { ++t_depth; }
    ~DepthScope() { --t_depth; }
};

size_t StackLeft()
{
    ULONG_PTR low = 0, high = 0;
    GetCurrentThreadStackLimits(&low, &high);
    volatile char marker = 0;
    return size_t(reinterpret_cast<uintptr_t>(&marker) - low);
}

// Logs the first few hooked calls: who called us, how deep, and where the call goes next.
void LogCall(const char* name, void* caller, const void* next)
{
    if (g_callLogBudget.load() <= 0 || g_callLogBudget.fetch_sub(1) <= 0) {
        return;
    }
    char from[96], to[96];
    DescribeCode(caller, from, sizeof(from));
    DescribeCode(next, to, sizeof(to));
    Log("hook %s: depth %d, thread %lu, caller %s, next %s, stack left %zu KB", name, t_depth,
        GetCurrentThreadId(), from, to, StackLeft() / 1024);
}

bool BreakRecursion(const char* name, void* caller)
{
    if (t_depth <= kMaxDepth) {
        return false;
    }
    if (!g_recursionLogged.exchange(true)) {
        char from[96];
        DescribeCode(caller, from, sizeof(from));
        LogError("Recursion through the %s hook (depth %d, caller %s); skipping the nested call",
            name, t_depth, from);
    }
    return true;
}

// Where a hooked call continues. Only an outermost call that came straight from the vtable
// goes to the previous hook; a call reached through another hook, or re-entering through the
// vtable, goes to DXGI directly.
void* NextFunction(const Slot& slot)
{
    if (t_depth == 1 && *slot.address == slot.hook) {
        return slot.previous;
    }
    return slot.real;
}

void HandlePresent(IDXGISwapChain* swapChain)
{
    __try {
        OnPresentCallback(swapChain);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Runtime().Disable();
        LogError("Crash in the Present hook (0x%08lX); VR is disabled until the game restarts",
            GetExceptionCode());
    }
}

// Each frame is handled once: by the vtable hook (before Openplanet's overlay is drawn) or by
// StubPresent (after it), depending on whether the UI should be visible in VR.
void MaybeHandlePresent(IDXGISwapChain* swapChain, UINT flags, bool afterOverlay)
{
    EnsureStackGuarantee();
    if (flags & DXGI_PRESENT_TEST) {
        return;
    }
    if (afterOverlay ? !CapturesAfterOverlay() : (t_depth != 1 || CapturesAfterOverlay())) {
        return;
    }
    ++g_handling;
    if (g_active) {
        HandlePresent(swapChain);
    }
    --g_handling;
}

HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)
{
    DepthScope depth;
    if (BreakRecursion("Present", _ReturnAddress())) {
        return S_OK;
    }
    MaybeHandlePresent(swapChain, flags, false);
    auto next = reinterpret_cast<PresentFn>(NextFunction(g_present));
    LogCall("Present", _ReturnAddress(), reinterpret_cast<const void*>(next));
    return next(swapChain, syncInterval, flags);
}

// Called by Openplanet's hook stub in place of DXGI's Present (see PatchOverlayStub), i.e. after
// Openplanet has run plugin scripts and drawn its overlay into the frame.
HRESULT STDMETHODCALLTYPE StubPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)
{
    DepthScope depth;
    MaybeHandlePresent(swapChain, flags, true);
    auto next = reinterpret_cast<PresentFn>(g_present.real);
    LogCall("Present (after overlay)", _ReturnAddress(), reinterpret_cast<const void*>(next));
    return next(swapChain, syncInterval, flags);
}

HRESULT STDMETHODCALLTYPE HookedPresent1(
    IDXGISwapChain1* swapChain, UINT syncInterval, UINT flags, const DXGI_PRESENT_PARAMETERS* params)
{
    DepthScope depth;
    if (BreakRecursion("Present1", _ReturnAddress())) {
        return S_OK;
    }
    MaybeHandlePresent(swapChain, flags, false);
    auto next = reinterpret_cast<Present1Fn>(NextFunction(g_present1));
    LogCall("Present1", _ReturnAddress(), reinterpret_cast<const void*>(next));
    return next(swapChain, syncInterval, flags, params);
}

struct ModuleRange {
    uintptr_t base = 0;
    uintptr_t end = 0;
    bool Contains(const void* p) const
    {
        const auto v = reinterpret_cast<uintptr_t>(p);
        return v >= base && v < end;
    }
};

ModuleRange GetModuleRange(HMODULE module)
{
    const auto base = reinterpret_cast<uintptr_t>(module);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    return {base, base + nt->OptionalHeader.SizeOfImage};
}

// Reads a vtable slot from the module's file on disk, i.e. before anyone in this process
// patched it, and relocates it to where the module is loaded.
void* ReadPristineSlot(HMODULE module, void** slot)
{
    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameW(module, path, MAX_PATH)) {
        return nullptr;
    }
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return nullptr;
    }
    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);
    std::vector<uint8_t> image(size_t(size.QuadPart));
    DWORD read = 0;
    const BOOL ok = ReadFile(file, image.data(), DWORD(image.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok || read != image.size() || image.size() < sizeof(IMAGE_DOS_HEADER)) {
        return nullptr;
    }

    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE ||
        size_t(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > image.size()) {
        return nullptr;
    }
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return nullptr;
    }

    const auto base = reinterpret_cast<uintptr_t>(module);
    const auto rva = uint32_t(reinterpret_cast<uintptr_t>(slot) - base);
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        const uint32_t start = section->VirtualAddress;
        const uint32_t length = std::max(section->Misc.VirtualSize, section->SizeOfRawData);
        if (rva < start || rva + sizeof(uint64_t) > uint64_t(start) + length) {
            continue;
        }
        const size_t offset = size_t(rva - start) + section->PointerToRawData;
        if (offset + sizeof(uint64_t) > image.size()) {
            return nullptr;
        }
        uint64_t value = 0;
        memcpy(&value, image.data() + offset, sizeof(value));
        return reinterpret_cast<void*>(value - nt->OptionalHeader.ImageBase + base);
    }
    return nullptr;
}

void DumpBytes(const char* label, const void* address, size_t count)
{
    char hex[3 * 128 + 1] = {};
    count = std::min(count, size_t(128));
    __try {
        const auto* p = static_cast<const uint8_t*>(address);
        for (size_t i = 0; i < count; ++i) {
            snprintf(hex + i * 3, 4, "%02X ", p[i]);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        strcat_s(hex, "<unreadable>");
    }
    Log("bytes at %s (%p): %s", label, address, hex);
}

bool ResolveSlot(Slot& slot, void** vtable, size_t index, void* hook, HMODULE dxgi,
    const ModuleRange& range, const char* name)
{
    slot.address = &vtable[index];
    slot.hook = hook;
    slot.previous = *slot.address;
    if (range.Contains(slot.previous)) {
        slot.real = slot.previous;
        DumpBytes(name, slot.real, 32); // an inline hook here would show up as a jmp
        return true;
    }
    slot.real = ReadPristineSlot(dxgi, slot.address);
    char owner[96];
    DescribeCode(slot.previous, owner, sizeof(owner));
    Log("%s is already hooked by another overlay (%s); DXGI's function is %p", name, owner, slot.real);
    HMODULE ownerModule = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            static_cast<LPCSTR>(slot.previous), &ownerModule)) {
        // A stub outside any module: name it for crash reports and record what it does.
        char stubName[48];
        snprintf(stubName, sizeof(stubName), "%s-hook-stub", name);
        RegisterCodeRange(slot.previous, 0x100, stubName);
        DumpBytes(stubName, slot.previous, 96);
    }
    if (range.Contains(slot.real)) {
        DumpBytes(name, slot.real, 32);
    }
    if (!range.Contains(slot.real)) {
        LogError("Could not find DXGI's own %s function", name);
        return false;
    }
    return true;
}

bool WriteSlot(void** address, void* value)
{
    DWORD oldProtect = 0;
    if (!VirtualProtect(address, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
        return false;
    }
    *address = value;
    VirtualProtect(address, sizeof(void*), oldProtect, &oldProtect);
    return true;
}

// Runs dxgi_probe.exe (next to this DLL), which creates a swapchain in its own process and
// prints the swapchain vtable's offset inside dxgi.dll. Creating a swapchain inside the game
// would be seen by Openplanet, which tracks swapchain creation.
bool ProbeVtableRva(uint64_t& rva)
{
    const std::wstring exe = ModuleDirectory() + L"dxgi_probe.exe";
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        LogError("Could not create a pipe for dxgi_probe.exe (error %lu)", GetLastError());
        return false;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    PROCESS_INFORMATION pi{};
    std::wstring commandLine = L"\"" + exe + L"\"";
    const BOOL started = CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, ModuleDirectory().c_str(), &si, &pi);
    CloseHandle(writePipe);
    if (!started) {
        LogError("Could not run dxgi_probe.exe (error %lu)", GetLastError());
        CloseHandle(readPipe);
        return false;
    }
    if (WaitForSingleObject(pi.hProcess, 10000) != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 1);
        LogError("dxgi_probe.exe did not finish");
    }
    std::string output;
    char buffer[256];
    DWORD read = 0;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        output.append(buffer, read);
    }
    CloseHandle(readPipe);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    for (char& c : output) {
        if (c == '\r' || c == '\n') c = ' ';
    }
    Log("dxgi_probe.exe: %s", output.c_str());
    const size_t at = output.find("vtable_rva=");
    unsigned long long value = 0;
    if (at == std::string::npos || sscanf_s(output.c_str() + at, "vtable_rva=%llx", &value) != 1) {
        LogError("dxgi_probe.exe did not report the swapchain vtable");
        return false;
    }
    rva = value;
    return true;
}

// Openplanet's Present hook is a small stub that calls Openplanet's handler (plugin scripts,
// overlay drawing) and then calls DXGI's Present with `mov rax, imm64; call rax`. Pointing that
// imm64 at StubPresent makes us copy the frame after the overlay is drawn, so the overlay (and
// the plugin's VR panel) shows up in the headset. Only patched when the exact instruction
// with DXGI's address is found.
uint64_t* g_stubTarget = nullptr;

bool PatchOverlayStub()
{
    auto* stub = static_cast<uint8_t*>(g_present.previous);
    const auto real = reinterpret_cast<uint64_t>(g_present.real);
    for (size_t i = 0; i + 12 <= 0x100; ++i) {
        uint8_t code[12];
        __try {
            memcpy(code, stub + i, sizeof(code));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        uint64_t imm = 0;
        memcpy(&imm, code + 2, sizeof(imm));
        if (code[0] != 0x48 || code[1] != 0xB8 || imm != real || code[10] != 0xFF || code[11] != 0xD0) {
            continue;
        }
        auto* target = reinterpret_cast<uint64_t*>(stub + i + 2);
        DWORD oldProtect = 0;
        if (!VirtualProtect(target, sizeof(uint64_t), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LogError("Could not unprotect the overlay stub (error %lu)", GetLastError());
            return false;
        }
        *target = reinterpret_cast<uint64_t>(&StubPresent);
        VirtualProtect(target, sizeof(uint64_t), oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), stub, 0x100);
        g_stubTarget = target;
        Log("Capturing frames after the overlay: patched Openplanet's stub at +0x%zx", i + 2);
        return true;
    }
    Log("Openplanet's stub has an unexpected layout; capturing frames before the overlay");
    return false;
}

bool PatchVtable()
{
    HMODULE dxgi = GetModuleHandleW(L"dxgi.dll");
    if (!dxgi) {
        LogError("dxgi.dll is not loaded");
        return false;
    }
    uint64_t rva = 0;
    if (!ProbeVtableRva(rva)) {
        return false;
    }
    const ModuleRange range = GetModuleRange(dxgi);
    void** vtable = reinterpret_cast<void**>(range.base + rva);
    if (!range.Contains(vtable)) {
        LogError("The swapchain vtable offset 0x%llx is outside dxgi.dll", static_cast<unsigned long long>(rva));
        return false;
    }
    if (!ResolveSlot(g_present, vtable, kPresentSlot, reinterpret_cast<void*>(&HookedPresent), dxgi,
            range, "Present") ||
        !ResolveSlot(g_present1, vtable, kPresent1Slot, reinterpret_cast<void*>(&HookedPresent1), dxgi,
            range, "Present1")) {
        return false;
    }
    // Hook both before the overlay (vtable) and after it (the overlay's stub, when it has one),
    // so the UI can be shown or hidden in VR at any time.
    if (g_present.previous != g_present.real) {
        PatchOverlayStub();
    }
    bool ok = WriteSlot(g_present.address, g_present.hook);
    if (ok && !WriteSlot(g_present1.address, g_present1.hook)) {
        WriteSlot(g_present.address, g_present.previous);
        ok = false;
    }
    if (!ok) {
        LogError("Could not patch the swapchain vtable (error %lu)", GetLastError());
    }
    return ok;
}

} // namespace

bool InstallPresentHook()
{
    if (!g_patched) {
        // Other hooks may chain to ours, so this DLL must never be unloaded once patched.
        HMODULE self = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&HookedPresent), &self)) {
            LogError("Could not pin TrackmaniaVR.dll in memory (error %lu)", GetLastError());
            return false;
        }
        if (!PatchVtable()) {
            return false;
        }
        g_patched = true;
        Log("Present hook installed (previous %p, DXGI %p; Present1 previous %p, DXGI %p)",
            g_present.previous, g_present.real, g_present1.previous, g_present1.real);
    }
    g_active = true;
    return true;
}

bool CapturesAfterOverlay()
{
    return g_stubTarget != nullptr && Shared().captureAfterOverlay != 0;
}

void UninstallPresentHook()
{
    if (!g_active.exchange(false)) {
        return;
    }
    for (int i = 0; i < 2000 && g_handling.load() > 0; ++i) {
        Sleep(1);
    }
    Log("Present hook paused (the DLL stays loaded until the game exits)");
}

} // namespace tmvr
