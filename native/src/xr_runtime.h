#pragma once
#include "camera_math.h"
#include "xr_includes.h"

#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace tmvr {

struct SharedBlock;

// Owns the OpenXR instance/session and turns the game's presented frames into headset frames.
//
// Frames alternate between eyes: the plugin calls ComposeModel() during a game frame to get the
// cockpit camera for one eye, and OnPresent() later copies that frame's back buffer into the
// eye's swapchain. Every eye image is submitted with the exact pose and FOV it was rendered with,
// so the runtime's reprojection hides the one-frame offset between the two eyes. Frames without
// a ComposeModel() call (menus, replays, loading screens) are shown as a flat virtual screen.
class XrRuntime {
public:
    // Render thread, from the Present hook.
    void OnPresent(IDXGISwapChain* swapChain);
    // Script thread: writes this frame's eye as cockpit model fields. Returns false when no
    // stereo camera should be applied this frame.
    bool ComposeModel(SharedBlock& sb);
    // Script thread: what ComposeModel asked for the frame whose camera the plugin sees right now.
    // Falls back to the latest request.
    void RequestedForCurrentFrame(const SharedBlock& sb, float out[6]);
    void RequestRecenter() { m_recenterRequested = true; }
    // Stops all XR work after a crash in the Present hook; nothing is torn down.
    void Disable() { m_disabled = true; }
    // Script thread, after the Present hook has been removed.
    void Shutdown();

private:
    struct Swapchain {
        XrSwapchain handle = XR_NULL_HANDLE;
        int32_t width = 0;
        int32_t height = 0;
        std::vector<XrSwapchainImageD3D11KHR> images;
    };

    struct ComposeRecord {
        uint64_t target = UINT64_MAX; // present index this camera was meant for
        int eye = 0;
        XrPosef pose{};
        XrFovf fov{};
    };

    // The eye a compose call is for, in the game camera's local frame.
    struct EyePose {
        uint64_t target = 0;
        int eye = 0;
        XrPosef xrPose{};
        XrFovf fov{};
        Mat3 rot{};
        Vec3 pos{};
    };

    enum class LayerKind { None, Projection, Quad };

    bool PrepareEye(SharedBlock& sb, EyePose& out); // m_mutex must be held
    void Commit(SharedBlock& sb, const EyePose& e);  // m_mutex must be held

    bool EnsureInitialized(IDXGISwapChain* swapChain);
    bool LoadLoader();
    bool CreateInstance();
    bool GetSystem();
    bool CreateSession(ID3D11Device* device);
    void DestroySession();
    void PollEvents();
    void PresentFrame(IDXGISwapChain* swapChain, uint64_t presentIndex);
    bool BeginFrame();
    void EndFrame(LayerKind kind);
    void LocateViews(XrTime time);
    bool Recenter(XrTime time, bool fullOrientation);
    bool EnsureSwapchains(const D3D11_TEXTURE2D_DESC& desc);
    int64_t PickSwapchainFormat(DXGI_FORMAT source);
    bool CreateSwapchain(Swapchain& sc, int64_t format, int32_t width, int32_t height);
    void DestroySwapchains();
    bool CopyToSwapchain(Swapchain& sc, ID3D11Texture2D* source, const D3D11_TEXTURE2D_DESC& desc);
    void LogStats();
    void Trace(const char* step);
    bool Check(XrResult res, const char* what);

    HMODULE m_loader = nullptr;
    XrInstance m_instance = XR_NULL_HANDLE;
    XrSystemId m_system = XR_NULL_SYSTEM_ID;
    XrSession m_session = XR_NULL_HANDLE;
    XrSpace m_localSpace = XR_NULL_HANDLE;
    XrSpace m_viewSpace = XR_NULL_HANDLE;
    XrSpace m_space = XR_NULL_HANDLE; // recentered LOCAL space; views and layers live here
    std::atomic<bool> m_sessionRunning{false};
    std::atomic<bool> m_recenterRequested{false};
    std::atomic<bool> m_disabled{false};
    bool m_needsRecenter = true;
    XrResult m_lastSystemResult = XR_SUCCESS;
    uint64_t m_nextInitTick = 0;
    bool m_wasEnabled = false;
    int m_traceFrames = 0;

    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_resolveTexture;
    Swapchain m_eyes[2];
    Swapchain m_quad;
    DXGI_FORMAT m_sourceFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT m_failedFormat = DXGI_FORMAT_UNKNOWN;

    bool m_frameActive = false;
    XrFrameState m_frameState{XR_TYPE_FRAME_STATE};
    std::array<XrCompositionLayerProjectionView, 2> m_layerViews{};
    bool m_haveEye[2] = {};

    // Shared between the render thread and the script thread.
    std::mutex m_mutex;
    std::array<XrView, 2> m_views{};
    bool m_viewsValid = false;
    std::array<ComposeRecord, 8> m_records{};
    struct RequestedRecord {
        uint64_t target = UINT64_MAX;
        float values[6] = {};
    };
    std::array<RequestedRecord, 8> m_requested{};
    std::atomic<uint64_t> m_presentIndex{0};
    int32_t m_backbufferWidth = 0;
    int32_t m_backbufferHeight = 0;

    uint64_t m_statsTick = 0;
    uint32_t m_statsPresents = 0;
    uint32_t m_statsComposes = 0;
    uint32_t m_statsXrFrames = 0;
};

XrRuntime& Runtime();
const std::wstring& ModuleDirectory();

} // namespace tmvr
