#include "cockpit.h"
#include "crash_report.h"
#include "log.h"
#include "present_hook.h"
#include "shared_block.h"
#include "xr_runtime.h"

#include <cstdint>

#define TMVR_VERSION "0.1.1"

namespace tmvr {
namespace {

SharedBlock g_shared{kSharedVersion};
std::wstring g_moduleDirectory;
bool g_initialized = false;

} // namespace

SharedBlock& Shared() { return g_shared; }
const std::wstring& ModuleDirectory() { return g_moduleDirectory; }
void OnPresentCallback(IDXGISwapChain* swapChain) { Runtime().OnPresent(swapChain); }

} // namespace tmvr

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(module, path, MAX_PATH);
        std::wstring dir = path;
        tmvr::g_moduleDirectory = dir.substr(0, dir.find_last_of(L"\\/") + 1);
    }
    return TRUE;
}

// Exports called by the Openplanet plugin (plugin/Native.as). They take no arguments; all data
// goes through the SharedBlock so we never depend on how Import::Function marshals values.
extern "C" {

__declspec(dllexport) uint64_t TMVR_GetShared()
{
    return reinterpret_cast<uint64_t>(&tmvr::Shared());
}

// Only prepares logging; nothing in the game is touched until TMVR_Start().
__declspec(dllexport) int32_t TMVR_Init()
{
    using namespace tmvr;
    if (g_initialized) {
        return 1;
    }
    LogOpen(g_moduleDirectory + L"TrackmaniaVR.log");
    Log("TrackmaniaVR " TMVR_VERSION " loaded");
    InstallCrashReporter();
    g_initialized = true;
    return 1;
}

// Installs the Present hook (once per process). The plugin calls this from "Start VR".
__declspec(dllexport) int32_t TMVR_Start()
{
    using namespace tmvr;
    if (!g_initialized) {
        return 0;
    }
    if (!InstallPresentHook()) {
        return 0;
    }
    g_shared.status |= Status_HooksInstalled;
    return 1;
}

__declspec(dllexport) int32_t TMVR_ComposeModel()
{
    return tmvr::Runtime().ComposeModel(tmvr::Shared()) ? 1 : 0;
}

__declspec(dllexport) void TMVR_Measure()
{
    float requested[6];
    tmvr::Runtime().RequestedForCurrentFrame(tmvr::Shared(), requested);
    tmvr::MeasureCamera(tmvr::Shared(), requested);
}

__declspec(dllexport) void TMVR_CalibrationReset()
{
    tmvr::ResetCalibration(tmvr::Shared());
}

__declspec(dllexport) void TMVR_Recenter()
{
    tmvr::Runtime().RequestRecenter();
}

__declspec(dllexport) void TMVR_Shutdown()
{
    using namespace tmvr;
    if (!g_initialized) {
        return;
    }
    UninstallPresentHook();
    Runtime().Shutdown();
    g_shared.status = 0;
    g_initialized = false;
    Log("TrackmaniaVR stopped");
    LogClose();
}

} // extern "C"
