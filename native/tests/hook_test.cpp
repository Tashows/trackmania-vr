// Checks that TrackmaniaVR's Present hook coexists with another overlay patching the same
// vtable slot (the Openplanet crash): every Present must reach both hooks exactly once and
// DXGI exactly once, without recursion.
//
// Usage: hook_test.exe <path to TrackmaniaVR.dll>
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

using Microsoft::WRL::ComPtr;
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);

constexpr size_t kPresentSlot = 8;
constexpr uint32_t kOffPresentCount = 144; // SharedBlock::presentCount

enum class OverlayStyle {
    PatchedBeforeUs,    // overlay hooked first and calls its saved original
    RepatchesOverUs,    // overlay notices it was replaced, re-patches and chains to us
    CallsThroughVtable, // overlay calls the vtable again with a reentrancy guard
    OpenplanetStub,     // a heap stub like Openplanet's: call handler, then mov rax, <DXGI>; call rax
    OpenplanetStubHiddenUi, // same, but capturing before the overlay (UI hidden in VR)
};

constexpr uint32_t kOffCaptureAfterOverlay = 84; // SharedBlock::captureAfterOverlay

void** g_slot = nullptr;
PresentFn g_overlayOriginal = nullptr;
OverlayStyle g_style;
int g_overlayCalls = 0;
int g_depth = 0;
int g_maxDepth = 0;
bool g_inOverlay = false;

void Write(void** slot, void* value)
{
    DWORD old;
    VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old);
    *slot = value;
    VirtualProtect(slot, sizeof(void*), old, &old);
}

HRESULT STDMETHODCALLTYPE OverlayPresent(IDXGISwapChain* sc, UINT sync, UINT flags)
{
    if (++g_depth > g_maxDepth) {
        g_maxDepth = g_depth;
    }
    if (g_depth > 32) {
        printf("FAIL: recursion\n");
        ExitProcess(2);
    }
    HRESULT hr;
    if (g_style == OverlayStyle::CallsThroughVtable) {
        if (g_inOverlay) {
            hr = g_overlayOriginal(sc, sync, flags);
        } else {
            g_inOverlay = true;
            g_overlayCalls++;
            hr = sc->Present(sync, flags);
            g_inOverlay = false;
        }
    } else {
        g_overlayCalls++;
        hr = g_overlayOriginal(sc, sync, flags);
    }
    --g_depth;
    return hr;
}

// For OpenplanetStub: the handler records how many frames TrackmaniaVR had seen when the overlay
// ran, so we can check TrackmaniaVR handles each frame after the overlay.
uint64_t (*g_getShared)() = nullptr;
uint32_t g_seenByOverlay[64];

void OverlayHandler()
{
    if (g_getShared && g_overlayCalls < 64) {
        g_seenByOverlay[g_overlayCalls] = *reinterpret_cast<uint32_t*>(g_getShared() + kOffPresentCount);
    }
    g_overlayCalls++;
}

void* BuildOpenplanetStub(void* realPresent)
{
    auto* code = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    uint8_t* p = code;
    auto emit = [&](std::initializer_list<uint8_t> bytes) { for (uint8_t b : bytes) *p++ = b; };
    auto emit64 = [&](const void* v) { memcpy(p, &v, 8); p += 8; };
    emit({0x51, 0x52, 0x41, 0x50});           // push rcx; push rdx; push r8
    emit({0x48, 0x83, 0xEC, 0x20});           // sub rsp, 20h
    emit({0x48, 0xB8}); emit64(reinterpret_cast<void*>(&OverlayHandler));
    emit({0xFF, 0xD0});                       // call rax
    emit({0x48, 0x83, 0xC4, 0x20});           // add rsp, 20h
    emit({0x41, 0x58, 0x5A, 0x59});           // pop r8; pop rdx; pop rcx
    emit({0x48, 0x83, 0xEC, 0x28});           // sub rsp, 28h
    emit({0x48, 0xB8}); emit64(realPresent);  // mov rax, <DXGI Present>
    emit({0xFF, 0xD0});                       // call rax
    emit({0x48, 0x83, 0xC4, 0x28});           // add rsp, 28h
    emit({0xC3});                             // ret
    return code;
}

#pragma warning(push)
#pragma warning(disable : 4717) // the overflow is the point
__declspec(noinline) int Recurse(int depth)
{
    volatile char buffer[512];
    buffer[0] = char(depth);
    return Recurse(depth + 1) + buffer[0];
}
#pragma warning(pop)

int main(int argc, char** argv)
{
    if (argc < 2) {
        printf("usage: hook_test <TrackmaniaVR.dll> [style 0-2 | crash]\n");
        return 1;
    }
    if (argc > 2 && strcmp(argv[2], "crash") == 0) {
        // Crash reporter check: overflow the stack and let TrackmaniaVR.log record it.
        SetErrorMode(SEM_NOGPFAULTERRORBOX);
        HMODULE dll = LoadLibraryA(argv[1]);
        auto init = dll ? reinterpret_cast<int32_t (*)()>(GetProcAddress(dll, "TMVR_Init")) : nullptr;
        if (!init) {
            printf("FAIL: could not load %s\n", argv[1]);
            return 1;
        }
        init();
        return Recurse(0);
    }
    g_style = OverlayStyle(argc > 2 ? atoi(argv[2]) : 0);
    // Keep the DLL's OpenXR side from finding (and starting) a real runtime.
    SetEnvironmentVariableW(L"XR_RUNTIME_JSON", L"C:\\does-not-exist\\runtime.json");

    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"hook_test";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"hook_test", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64,
        nullptr, nullptr, wc.hInstance, nullptr);

    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2;
    desc.BufferDesc.Width = 64;
    desc.BufferDesc.Height = 64;
    desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = hwnd;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain> sc;
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &desc, &sc, &dev, nullptr, &ctx))) {
        printf("FAIL: could not create swapchain\n");
        return 1;
    }

    // The "Openplanet" overlay hooks first, like in the game.
    g_slot = &(*reinterpret_cast<void***>(sc.Get()))[kPresentSlot];
    g_overlayOriginal = reinterpret_cast<PresentFn>(*g_slot);
    const bool stubStyle = g_style == OverlayStyle::OpenplanetStub || g_style == OverlayStyle::OpenplanetStubHiddenUi;
    Write(g_slot, stubStyle ? BuildOpenplanetStub(*g_slot) : reinterpret_cast<void*>(&OverlayPresent));

    HMODULE dll = LoadLibraryA(argv[1]);
    if (!dll) {
        printf("FAIL: could not load %s\n", argv[1]);
        return 1;
    }
    auto getShared = reinterpret_cast<uint64_t (*)()>(GetProcAddress(dll, "TMVR_GetShared"));
    auto init = reinterpret_cast<int32_t (*)()>(GetProcAddress(dll, "TMVR_Init"));
    auto start = reinterpret_cast<int32_t (*)()>(GetProcAddress(dll, "TMVR_Start"));
    auto shutdown = reinterpret_cast<void (*)()>(GetProcAddress(dll, "TMVR_Shutdown"));
    if (!getShared || !init || !start || !shutdown || init() != 1 || start() != 1) {
        printf("FAIL: TMVR_Init/TMVR_Start\n");
        return 1;
    }
    auto presentCount = [&] { return *reinterpret_cast<uint32_t*>(getShared() + kOffPresentCount); };
    g_getShared = getShared;
    *reinterpret_cast<int32_t*>(getShared() + kOffCaptureAfterOverlay) = g_style == OverlayStyle::OpenplanetStub ? 1 : 0;

    if (g_style == OverlayStyle::RepatchesOverUs) {
        // Overlay sees our hook in its slot, takes it as its "original" and re-patches.
        g_overlayOriginal = reinterpret_cast<PresentFn>(*g_slot);
        Write(g_slot, reinterpret_cast<void*>(&OverlayPresent));
    }

    constexpr int kFrames = 20;
    const uint32_t before = presentCount();
    for (int i = 0; i < kFrames; ++i) {
        if (FAILED(sc->Present(0, 0))) {
            printf("FAIL: Present returned an error\n");
            return 1;
        }
    }
    const uint32_t ours = presentCount() - before;
    shutdown();
    // After shutdown the hook must still pass calls through.
    sc->Present(0, 0);

    printf("style %d: overlay calls %d, TrackmaniaVR calls %u, max overlay depth %d (expected %d, %d, 1)\n",
        int(g_style), g_overlayCalls, ours, g_maxDepth, kFrames + 1, kFrames);
    bool ok = g_overlayCalls == kFrames + 1 && ours == uint32_t(kFrames) && g_maxDepth <= 2;
    if (stubStyle) {
        // UI shown: the overlay runs before TrackmaniaVR handles each frame. UI hidden: after.
        const uint32_t offset = g_style == OverlayStyle::OpenplanetStub ? 0 : 1;
        for (int i = 0; i < kFrames; ++i) {
            ok = ok && g_seenByOverlay[i] == before + uint32_t(i) + offset;
        }
        printf("overlay ran %s TrackmaniaVR on every frame: %s\n", offset == 0 ? "before" : "after", ok ? "yes" : "no");
    }
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
