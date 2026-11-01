#include "xr_runtime.h"
#include "cockpit.h"
#include "log.h"
#include "present_hook.h"
#include "shared_block.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace tmvr {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kQuadDistance = 3.0f; // meters in front of the recentered head, by default
constexpr float kQuadWidth = 3.6f;    // meters, by default
constexpr uint64_t kInitRetryMs = 3000;
constexpr uint64_t kStatsIntervalMs = 5000;
constexpr int kTraceFrames = 8; // frames logged step by step after the session starts

#define TMVR_XR_FUNCTIONS(X)                                                                   \
    X(xrDestroyInstance) X(xrGetInstanceProperties) X(xrPollEvent) X(xrResultToString)          \
    X(xrGetSystem) X(xrGetSystemProperties) X(xrEnumerateViewConfigurationViews)                \
    X(xrCreateSession) X(xrDestroySession) X(xrBeginSession) X(xrEndSession)                    \
    X(xrCreateReferenceSpace) X(xrDestroySpace) X(xrLocateSpace)                                \
    X(xrEnumerateSwapchainFormats) X(xrCreateSwapchain) X(xrDestroySwapchain)                   \
    X(xrEnumerateSwapchainImages) X(xrAcquireSwapchainImage) X(xrWaitSwapchainImage)            \
    X(xrReleaseSwapchainImage) X(xrWaitFrame) X(xrBeginFrame) X(xrEndFrame) X(xrLocateViews)    \
    X(xrGetD3D11GraphicsRequirementsKHR)

struct XrFunctions {
    PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr = nullptr;
    PFN_xrEnumerateInstanceExtensionProperties xrEnumerateInstanceExtensionProperties = nullptr;
    PFN_xrCreateInstance xrCreateInstance = nullptr;
#define TMVR_DECLARE(name) PFN_##name name = nullptr;
    TMVR_XR_FUNCTIONS(TMVR_DECLARE)
#undef TMVR_DECLARE
};

XrFunctions xr;

bool IsSrgb8(DXGI_FORMAT f)
{
    return f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
        f == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
}

double MillisecondsSince(const LARGE_INTEGER& start)
{
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    return double(now.QuadPart - start.QuadPart) * 1000.0 / double(freq.QuadPart);
}

const char* SessionStateName(XrSessionState s)
{
    switch (s) {
    case XR_SESSION_STATE_IDLE: return "IDLE";
    case XR_SESSION_STATE_READY: return "READY";
    case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
    case XR_SESSION_STATE_VISIBLE: return "VISIBLE";
    case XR_SESSION_STATE_FOCUSED: return "FOCUSED";
    case XR_SESSION_STATE_STOPPING: return "STOPPING";
    case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
    case XR_SESSION_STATE_EXITING: return "EXITING";
    default: return "UNKNOWN";
    }
}

} // namespace

XrRuntime& Runtime()
{
    static XrRuntime runtime;
    return runtime;
}

bool XrRuntime::Check(XrResult res, const char* what)
{
    if (XR_SUCCEEDED(res)) {
        return true;
    }
    // Per-frame calls can fail every frame; don't flood the log with the same failure.
    static const char* lastWhat = nullptr;
    static XrResult lastRes = XR_SUCCESS;
    static uint64_t lastTick = 0;
    const uint64_t now = GetTickCount64();
    if (what == lastWhat && res == lastRes && now - lastTick < 2000) {
        return false;
    }
    lastWhat = what;
    lastRes = res;
    lastTick = now;

    char name[XR_MAX_RESULT_STRING_SIZE] = {};
    if (m_instance != XR_NULL_HANDLE && xr.xrResultToString) {
        xr.xrResultToString(m_instance, res, name);
    } else {
        snprintf(name, sizeof(name), "%d", int(res));
    }
    LogError("%s failed: %s", what, name);
    return false;
}

void XrRuntime::Trace(const char* step)
{
    if (m_traceFrames > 0) {
        Log("trace present %llu: %s", static_cast<unsigned long long>(m_presentIndex.load()), step);
    }
}

void XrRuntime::OnPresent(IDXGISwapChain* swapChain)
{
    const uint64_t presentIndex = m_presentIndex.load();
    if (presentIndex == 0) {
        Log("First Present received (thread %lu)", GetCurrentThreadId());
    }

    const bool wanted = Shared().xrEnabled != 0;
    if (wanted != m_wasEnabled) {
        Log(wanted ? "VR start requested" : "VR stop requested");
        m_wasEnabled = wanted;
        m_nextInitTick = 0;
        if (!wanted) {
            DestroySession();
        }
    }

    if (wanted && !m_disabled && EnsureInitialized(swapChain)) {
        PollEvents();
        if (m_sessionRunning && m_session != XR_NULL_HANDLE) {
            PresentFrame(swapChain, presentIndex);
            if (m_traceFrames > 0) {
                Trace("done, returning to the game's Present chain");
                --m_traceFrames;
            }
        }
    }
    m_presentIndex.store(presentIndex + 1);
    Shared().presentCount = uint32_t(presentIndex + 1);
    LogStats();
}

bool XrRuntime::EnsureInitialized(IDXGISwapChain* swapChain)
{
    ComPtr<ID3D11Device> device;
    if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&device)))) {
        return false; // not a D3D11 swapchain
    }
    if (m_session != XR_NULL_HANDLE) {
        if (device.Get() == m_device.Get()) {
            return true;
        }
        Log("The game's D3D11 device changed; restarting the OpenXR session");
        DestroySession();
    }

    const uint64_t now = GetTickCount64();
    if (now < m_nextInitTick) {
        return false;
    }
    m_nextInitTick = now + kInitRetryMs;

    if (m_instance == XR_NULL_HANDLE && !CreateInstance()) {
        return false;
    }
    if (m_system == XR_NULL_SYSTEM_ID && !GetSystem()) {
        return false;
    }
    return CreateSession(device.Get());
}

bool XrRuntime::LoadLoader()
{
    if (m_loader) {
        return true;
    }
    const std::wstring path = ModuleDirectory() + L"openxr_loader.dll";
    m_loader = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!m_loader) {
        LogError("Could not load openxr_loader.dll next to TrackmaniaVR.dll (error %lu)", GetLastError());
        return false;
    }
    xr.xrGetInstanceProcAddr =
        reinterpret_cast<PFN_xrGetInstanceProcAddr>(GetProcAddress(m_loader, "xrGetInstanceProcAddr"));
    if (!xr.xrGetInstanceProcAddr) {
        LogError("openxr_loader.dll does not export xrGetInstanceProcAddr");
        return false;
    }
    xr.xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties",
        reinterpret_cast<PFN_xrVoidFunction*>(&xr.xrEnumerateInstanceExtensionProperties));
    xr.xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrCreateInstance",
        reinterpret_cast<PFN_xrVoidFunction*>(&xr.xrCreateInstance));
    return xr.xrEnumerateInstanceExtensionProperties && xr.xrCreateInstance;
}

bool XrRuntime::CreateInstance()
{
    if (!LoadLoader()) {
        m_disabled = true;
        return false;
    }

    uint32_t count = 0;
    if (!Check(xr.xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr),
            "xrEnumerateInstanceExtensionProperties")) {
        return false;
    }
    std::vector<XrExtensionProperties> extensions(count, {XR_TYPE_EXTENSION_PROPERTIES});
    xr.xrEnumerateInstanceExtensionProperties(nullptr, count, &count, extensions.data());
    const bool hasD3D11 = std::any_of(extensions.begin(), extensions.end(), [](const auto& e) {
        return strcmp(e.extensionName, XR_KHR_D3D11_ENABLE_EXTENSION_NAME) == 0;
    });
    if (!hasD3D11) {
        LogError("The active OpenXR runtime does not support D3D11");
        m_disabled = true;
        return false;
    }

    const char* enabled[] = {XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    strcpy_s(ci.applicationInfo.applicationName, "TrackmaniaVR");
    ci.applicationInfo.applicationVersion = 1;
    strcpy_s(ci.applicationInfo.engineName, "Openplanet");
    ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    ci.enabledExtensionCount = 1;
    ci.enabledExtensionNames = enabled;
    if (!Check(xr.xrCreateInstance(&ci, &m_instance), "xrCreateInstance")) {
        return false;
    }

#define TMVR_LOAD(name)                                                                        \
    if (XR_FAILED(xr.xrGetInstanceProcAddr(                                                    \
            m_instance, #name, reinterpret_cast<PFN_xrVoidFunction*>(&xr.name)))) {            \
        LogError("Runtime is missing " #name);                                                 \
        m_disabled = true;                                                                     \
        return false;                                                                          \
    }
    TMVR_XR_FUNCTIONS(TMVR_LOAD)
#undef TMVR_LOAD

    XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};
    if (XR_SUCCEEDED(xr.xrGetInstanceProperties(m_instance, &props))) {
        Log("OpenXR runtime: %s %u.%u.%u", props.runtimeName,
            unsigned(XR_VERSION_MAJOR(props.runtimeVersion)),
            unsigned(XR_VERSION_MINOR(props.runtimeVersion)),
            unsigned(XR_VERSION_PATCH(props.runtimeVersion)));
    }
    Shared().status |= Status_XrInstance;
    return true;
}

bool XrRuntime::GetSystem()
{
    XrSystemGetInfo info{XR_TYPE_SYSTEM_GET_INFO};
    info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    const XrResult res = xr.xrGetSystem(m_instance, &info, &m_system);
    if (XR_FAILED(res)) {
        if (res != m_lastSystemResult) {
            if (res == XR_ERROR_FORM_FACTOR_UNAVAILABLE) {
                LogError("Headset not available. Is SteamVR running and the headset connected?");
            } else {
                Check(res, "xrGetSystem");
            }
        }
        m_lastSystemResult = res;
        m_system = XR_NULL_SYSTEM_ID;
        return false;
    }
    m_lastSystemResult = res;

    XrSystemProperties props{XR_TYPE_SYSTEM_PROPERTIES};
    if (XR_SUCCEEDED(xr.xrGetSystemProperties(m_instance, m_system, &props))) {
        Log("Headset: %s", props.systemName);
    }

    uint32_t count = 0;
    xr.xrEnumerateViewConfigurationViews(m_instance, m_system,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &count, nullptr);
    std::vector<XrViewConfigurationView> views(count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    xr.xrEnumerateViewConfigurationViews(m_instance, m_system,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, count, &count, views.data());
    if (!views.empty()) {
        Shared().recommendedWidth = int32_t(views[0].recommendedImageRectWidth);
        Shared().recommendedHeight = int32_t(views[0].recommendedImageRectHeight);
        Log("Recommended per-eye resolution: %ux%u", views[0].recommendedImageRectWidth,
            views[0].recommendedImageRectHeight);
    }
    return true;
}

bool XrRuntime::CreateSession(ID3D11Device* device)
{
    XrGraphicsRequirementsD3D11KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    if (!Check(xr.xrGetD3D11GraphicsRequirementsKHR(m_instance, m_system, &req),
            "xrGetD3D11GraphicsRequirementsKHR")) {
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC adapterDesc{};
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) &&
        SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) && SUCCEEDED(adapter->GetDesc(&adapterDesc))) {
        char gpu[128] = {};
        WideCharToMultiByte(CP_UTF8, 0, adapterDesc.Description, -1, gpu, sizeof(gpu), nullptr, nullptr);
        Log("Game GPU: %s", gpu);
        if (memcmp(&adapterDesc.AdapterLuid, &req.adapterLuid, sizeof(LUID)) != 0) {
            LogError("The game renders on a different GPU than the headset is connected to");
        }
    }
    if (device->GetFeatureLevel() < req.minFeatureLevel) {
        LogError("The game's D3D11 feature level is below what the runtime requires");
        return false;
    }

    XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
    binding.device = device;
    XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
    sci.next = &binding;
    sci.systemId = m_system;
    if (!Check(xr.xrCreateSession(m_instance, &sci, &m_session), "xrCreateSession")) {
        m_session = XR_NULL_HANDLE;
        return false;
    }
    m_device = device;
    m_device->GetImmediateContext(&m_context);

    XrReferenceSpaceCreateInfo rs{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rs.poseInReferenceSpace.orientation.w = 1.0f;
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    bool ok = Check(xr.xrCreateReferenceSpace(m_session, &rs, &m_localSpace), "Create LOCAL space");
    ok = ok && Check(xr.xrCreateReferenceSpace(m_session, &rs, &m_space), "Create play space");
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    ok = ok && Check(xr.xrCreateReferenceSpace(m_session, &rs, &m_viewSpace), "Create VIEW space");
    if (!ok) {
        DestroySession();
        return false;
    }
    m_needsRecenter = true;
    Log("OpenXR session created");
    return true;
}

void XrRuntime::DestroySession()
{
    DestroySwapchains();
    for (XrSpace* space : {&m_space, &m_viewSpace, &m_localSpace}) {
        if (*space != XR_NULL_HANDLE) {
            xr.xrDestroySpace(*space);
            *space = XR_NULL_HANDLE;
        }
    }
    if (m_session != XR_NULL_HANDLE) {
        xr.xrDestroySession(m_session);
        m_session = XR_NULL_HANDLE;
    }
    m_sessionRunning = false;
    m_frameActive = false;
    m_haveEye[0] = m_haveEye[1] = false;
    m_resolveTexture.Reset();
    m_context.Reset();
    m_device.Reset();
    m_sourceFormat = DXGI_FORMAT_UNKNOWN;
    m_failedFormat = DXGI_FORMAT_UNKNOWN;
    {
        std::lock_guard lock(m_mutex);
        m_viewsValid = false;
        m_records.fill({});
    }
    Shared().status &= ~(Status_SessionRunning | Status_Stereo);
}

void XrRuntime::Shutdown()
{
    if (m_session != XR_NULL_HANDLE) {
        DestroySession();
        Log("OpenXR session destroyed");
    }
    if (m_instance != XR_NULL_HANDLE) {
        xr.xrDestroyInstance(m_instance);
        m_instance = XR_NULL_HANDLE;
    }
    m_system = XR_NULL_SYSTEM_ID;
    if (m_loader) {
        FreeLibrary(m_loader);
        m_loader = nullptr;
    }
    xr = {};
    Shared().status &= ~Status_XrInstance;
}

void XrRuntime::PollEvents()
{
    XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
    while (m_instance != XR_NULL_HANDLE && xr.xrPollEvent(m_instance, &ev) == XR_SUCCESS) {
        switch (ev.type) {
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
            const auto& e = *reinterpret_cast<const XrEventDataSessionStateChanged*>(&ev);
            Log("Session state: %s", SessionStateName(e.state));
            if (e.state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
                bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                if (Check(xr.xrBeginSession(m_session, &bi), "xrBeginSession")) {
                    m_sessionRunning = true;
                    m_traceFrames = kTraceFrames;
                    Shared().status |= Status_SessionRunning;
                }
            } else if (e.state == XR_SESSION_STATE_STOPPING) {
                m_sessionRunning = false;
                m_frameActive = false;
                Shared().status &= ~(Status_SessionRunning | Status_Stereo);
                Check(xr.xrEndSession(m_session), "xrEndSession");
            } else if (e.state == XR_SESSION_STATE_EXITING || e.state == XR_SESSION_STATE_LOSS_PENDING) {
                // The runtime went away (e.g. SteamVR closed). Try again later.
                DestroySession();
                return;
            }
            break;
        }
        case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
            m_needsRecenter = true;
            break;
        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
            LogError("OpenXR instance lost");
            DestroySession();
            Shutdown();
            return;
        default:
            break;
        }
        ev = {XR_TYPE_EVENT_DATA_BUFFER};
    }
}

void XrRuntime::PresentFrame(IDXGISwapChain* swapChain, uint64_t presentIndex)
{
    ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
        return;
    }
    D3D11_TEXTURE2D_DESC desc;
    backBuffer->GetDesc(&desc);
    Trace("got back buffer");
    if (!EnsureSwapchains(desc)) {
        return;
    }
    Trace("swapchains ready");
    if (!m_frameActive && !BeginFrame()) {
        return;
    }

    ComposeRecord record;
    bool stereo = false;
    {
        std::lock_guard lock(m_mutex);
        const ComposeRecord& r = m_records[presentIndex % m_records.size()];
        if (r.target == presentIndex) {
            record = r;
            stereo = true;
        }
    }

    if (!m_frameState.shouldRender) {
        EndFrame(LayerKind::None);
    } else if (stereo) {
        if (m_haveEye[record.eye]) {
            Shared().eyeMismatchCount++;
        }
        CopyToSwapchain(m_eyes[record.eye], backBuffer.Get(), desc);
        XrCompositionLayerProjectionView& view = m_layerViews[record.eye];
        view = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        view.pose = record.pose;
        view.fov = record.fov;
        view.subImage.swapchain = m_eyes[record.eye].handle;
        view.subImage.imageRect = {{0, 0}, {m_eyes[record.eye].width, m_eyes[record.eye].height}};
        m_haveEye[record.eye] = true;
        if (record.eye == 1) {
            if (m_haveEye[0]) {
                EndFrame(LayerKind::Projection);
            } else {
                Shared().eyeMismatchCount++;
                EndFrame(LayerKind::None);
            }
        }
        Shared().status |= Status_Stereo;
    } else {
        if (m_haveEye[0]) {
            Shared().eyeMismatchCount++;
        }
        CopyToSwapchain(m_quad, backBuffer.Get(), desc);
        EndFrame(LayerKind::Quad);
        Shared().status &= ~Status_Stereo;
    }

    // Begin the next frame right away so Compose() has fresh views during the next game frame.
    if (!m_frameActive) {
        BeginFrame();
    }
}

bool XrRuntime::BeginFrame()
{
    XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
    m_frameState = {XR_TYPE_FRAME_STATE};
    LARGE_INTEGER start;
    QueryPerformanceCounter(&start);
    Trace("xrWaitFrame");
    if (!Check(xr.xrWaitFrame(m_session, &wi, &m_frameState), "xrWaitFrame")) {
        return false;
    }
    Shared().waitFrameMs = float(MillisecondsSince(start));

    Trace("xrBeginFrame");
    XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
    if (!Check(xr.xrBeginFrame(m_session, &bi), "xrBeginFrame")) {
        return false;
    }
    m_frameActive = true;
    m_haveEye[0] = m_haveEye[1] = false;

    // A recenter the user asks for resets the full head orientation; the automatic one at session
    // start (when the headset may not be on yet) only resets yaw.
    const bool requested = m_recenterRequested.exchange(false);
    if (m_needsRecenter || requested) {
        const bool ok = Recenter(m_frameState.predictedDisplayTime, requested);
        m_needsRecenter = m_needsRecenter && !ok;
        if (requested && !ok) {
            m_recenterRequested = true; // tracking not ready; try again next frame
        }
    }
    Trace("xrLocateViews");
    LocateViews(m_frameState.predictedDisplayTime);
    Trace(m_frameState.shouldRender ? "frame begun (shouldRender=1)" : "frame begun (shouldRender=0)");
    return true;
}

void XrRuntime::EndFrame(LayerKind kind)
{
    XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    const XrCompositionLayerBaseHeader* layers[1] = {};
    uint32_t layerCount = 0;

    if (kind == LayerKind::Projection) {
        projection.space = m_space;
        projection.viewCount = 2;
        projection.views = m_layerViews.data();
        layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
    } else if (kind == LayerKind::Quad) {
        quad.space = m_space;
        quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        quad.subImage.swapchain = m_quad.handle;
        quad.subImage.imageRect = {{0, 0}, {m_quad.width, m_quad.height}};
        const SharedBlock& sb = Shared();
        const float distance = sb.screenDistance > 0.1f ? sb.screenDistance : kQuadDistance;
        const float width = sb.screenWidth > 0.1f ? sb.screenWidth : kQuadWidth;
        quad.pose.orientation.w = 1.0f;
        quad.pose.position.z = -distance;
        quad.size = {width, width * float(m_quad.height) / float(m_quad.width)};
        layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
    }

    XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};
    ei.displayTime = m_frameState.predictedDisplayTime;
    ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    ei.layerCount = layerCount;
    ei.layers = layers;
    Trace(kind == LayerKind::Projection ? "xrEndFrame (stereo)"
            : kind == LayerKind::Quad   ? "xrEndFrame (flat screen)"
                                        : "xrEndFrame (no layers)");
    const XrResult res = xr.xrEndFrame(m_session, &ei);
    Trace("xrEndFrame returned");
    m_frameActive = false;
    m_haveEye[0] = m_haveEye[1] = false;
    if (Check(res, "xrEndFrame") && layerCount > 0) {
        Shared().xrFramesSubmitted++;
    }
}

void XrRuntime::LocateViews(XrTime time)
{
    XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
    li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime = time;
    li.space = m_space;
    XrViewState state{XR_TYPE_VIEW_STATE};
    std::array<XrView, 2> views;
    views.fill({XR_TYPE_VIEW});
    uint32_t count = 0;
    const XrResult res = xr.xrLocateViews(m_session, &li, &state, 2, &count, views.data());
    if (XR_FAILED(res) || count != 2 || !(state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
        return; // keep the last good views (e.g. while tracking is briefly lost)
    }
    std::lock_guard lock(m_mutex);
    m_views = views;
    m_viewsValid = true;
}

bool XrRuntime::Recenter(XrTime time, bool fullOrientation)
{
    XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
    if (XR_FAILED(xr.xrLocateSpace(m_viewSpace, m_localSpace, time, &loc)) ||
        !(loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) ||
        !(loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
        return false;
    }

    const XrQuaternionf& o = loc.pose.orientation;
    Mat3 r = QuatToMat3({o.x, o.y, o.z, o.w});
    XrReferenceSpaceCreateInfo rs{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    rs.poseInReferenceSpace.position = loc.pose.position;
    if (fullOrientation) {
        // The current head pose becomes "looking straight ahead, level": this also removes the
        // tilt of a headset that sits slightly crooked on the head.
        rs.poseInReferenceSpace.orientation = o;
    } else {
        // Keep only the head's yaw, so the horizon stays level with the real one.
        const float yaw = atan2f(r.m[0][2], r.m[2][2]);
        rs.poseInReferenceSpace.orientation = {0.0f, sinf(yaw * 0.5f), 0.0f, cosf(yaw * 0.5f)};
    }

    Vec3 unusedPos{};
    XrToCamera(r, unusedPos);
    float pitch, yaw, roll;
    Mat3ToEuler(r, pitch, yaw, roll);
    XrSpace space = XR_NULL_HANDLE;
    if (!Check(xr.xrCreateReferenceSpace(m_session, &rs, &space), "Recenter")) {
        return false;
    }
    if (m_space != XR_NULL_HANDLE) {
        xr.xrDestroySpace(m_space);
    }
    m_space = space;
    Log("Recentered %s (head was at pitch %.1f, yaw %.1f, roll %.1f deg)",
        fullOrientation ? "fully" : "yaw only", pitch * 180.0f / kPi, yaw * 180.0f / kPi, roll * 180.0f / kPi);
    return true;
}

bool XrRuntime::EnsureSwapchains(const D3D11_TEXTURE2D_DESC& desc)
{
    const int32_t width = int32_t(desc.Width);
    const int32_t height = int32_t(desc.Height);
    if (m_eyes[0].handle != XR_NULL_HANDLE && m_eyes[0].width == width &&
        m_eyes[0].height == height && m_sourceFormat == desc.Format) {
        return true;
    }
    if (desc.Format == m_failedFormat) {
        return false;
    }
    if (m_frameActive) {
        EndFrame(LayerKind::None);
    }
    DestroySwapchains();

    const int64_t format = PickSwapchainFormat(desc.Format);
    if (format == 0) {
        m_failedFormat = desc.Format;
        return false;
    }
    if (!CreateSwapchain(m_eyes[0], format, width, height) ||
        !CreateSwapchain(m_eyes[1], format, width, height) ||
        !CreateSwapchain(m_quad, format, width, height)) {
        DestroySwapchains();
        m_failedFormat = desc.Format;
        return false;
    }

    if (desc.SampleDesc.Count > 1) {
        D3D11_TEXTURE2D_DESC rd = desc;
        rd.SampleDesc = {1, 0};
        rd.BindFlags = 0;
        rd.MiscFlags = 0;
        rd.Usage = D3D11_USAGE_DEFAULT;
        rd.CPUAccessFlags = 0;
        if (FAILED(m_device->CreateTexture2D(&rd, nullptr, &m_resolveTexture))) {
            LogError("Could not create the MSAA resolve texture");
            DestroySwapchains();
            return false;
        }
    }

    m_sourceFormat = desc.Format;
    {
        std::lock_guard lock(m_mutex);
        m_backbufferWidth = width;
        m_backbufferHeight = height;
    }
    Shared().backbufferWidth = width;
    Shared().backbufferHeight = height;
    Log("Swapchains created: %dx%d, back buffer format %d, swapchain format %lld, %u samples",
        width, height, int(desc.Format), static_cast<long long>(format), desc.SampleDesc.Count);
    return true;
}

int64_t XrRuntime::PickSwapchainFormat(DXGI_FORMAT source)
{
    uint32_t count = 0;
    xr.xrEnumerateSwapchainFormats(m_session, 0, &count, nullptr);
    std::vector<int64_t> formats(count);
    xr.xrEnumerateSwapchainFormats(m_session, count, &count, formats.data());
    const auto supported = [&](DXGI_FORMAT f) {
        return std::find(formats.begin(), formats.end(), int64_t(f)) != formats.end();
    };

    // CopyResource only needs formats from the same typeless family. Prefer the sRGB variant so
    // the runtime decodes the game's gamma-encoded pixels correctly.
    DXGI_FORMAT candidates[2] = {source, DXGI_FORMAT_UNKNOWN};
    switch (source) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        candidates[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        candidates[1] = DXGI_FORMAT_R8G8B8A8_UNORM;
        break;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        candidates[0] = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        candidates[1] = DXGI_FORMAT_B8G8R8A8_UNORM;
        break;
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        candidates[0] = DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
        candidates[1] = DXGI_FORMAT_B8G8R8X8_UNORM;
        break;
    default:
        break;
    }

    for (DXGI_FORMAT f : candidates) {
        if (f != DXGI_FORMAT_UNKNOWN && supported(f)) {
            if (!IsSrgb8(f) && f != DXGI_FORMAT_R16G16B16A16_FLOAT) {
                Log("Warning: using non-sRGB swapchain format %d; colors may look washed out", int(f));
            }
            return int64_t(f);
        }
    }

    std::string list;
    for (int64_t f : formats) {
        list += std::to_string(f) + " ";
    }
    LogError("No OpenXR swapchain format matches back buffer format %d (runtime offers: %s)",
        int(source), list.c_str());
    return 0;
}

bool XrRuntime::CreateSwapchain(Swapchain& sc, int64_t format, int32_t width, int32_t height)
{
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    ci.format = format;
    ci.sampleCount = 1;
    ci.width = uint32_t(width);
    ci.height = uint32_t(height);
    ci.faceCount = 1;
    ci.arraySize = 1;
    ci.mipCount = 1;
    if (!Check(xr.xrCreateSwapchain(m_session, &ci, &sc.handle), "xrCreateSwapchain")) {
        sc.handle = XR_NULL_HANDLE;
        return false;
    }
    uint32_t count = 0;
    xr.xrEnumerateSwapchainImages(sc.handle, 0, &count, nullptr);
    sc.images.assign(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    if (!Check(xr.xrEnumerateSwapchainImages(sc.handle, count, &count,
                   reinterpret_cast<XrSwapchainImageBaseHeader*>(sc.images.data())),
            "xrEnumerateSwapchainImages")) {
        return false;
    }
    sc.width = width;
    sc.height = height;
    return true;
}

void XrRuntime::DestroySwapchains()
{
    for (Swapchain* sc : {&m_eyes[0], &m_eyes[1], &m_quad}) {
        if (sc->handle != XR_NULL_HANDLE) {
            xr.xrDestroySwapchain(sc->handle);
        }
        *sc = {};
    }
    m_resolveTexture.Reset();
    m_sourceFormat = DXGI_FORMAT_UNKNOWN;
}

bool XrRuntime::CopyToSwapchain(Swapchain& sc, ID3D11Texture2D* source, const D3D11_TEXTURE2D_DESC& desc)
{
    uint32_t index = 0;
    XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    Trace(&sc == &m_quad ? "xrAcquireSwapchainImage (flat screen)" : "xrAcquireSwapchainImage (eye)");
    if (!Check(xr.xrAcquireSwapchainImage(sc.handle, &ai, &index), "xrAcquireSwapchainImage")) {
        return false;
    }
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wi.timeout = XR_INFINITE_DURATION;
    Trace("xrWaitSwapchainImage");
    if (Check(xr.xrWaitSwapchainImage(sc.handle, &wi), "xrWaitSwapchainImage")) {
        ID3D11Texture2D* target = sc.images[index].texture;
        Trace("CopyResource");
        if (desc.SampleDesc.Count > 1) {
            m_context->ResolveSubresource(m_resolveTexture.Get(), 0, source, 0, desc.Format);
            m_context->CopyResource(target, m_resolveTexture.Get());
        } else {
            m_context->CopyResource(target, source);
        }
    }
    Trace("xrReleaseSwapchainImage");
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    return Check(xr.xrReleaseSwapchainImage(sc.handle, &ri), "xrReleaseSwapchainImage");
}

bool XrRuntime::PrepareEye(SharedBlock& sb, EyePose& out)
{
    if (!m_sessionRunning || !m_viewsValid) {
        return false;
    }

    // Plugin scripts run inside Openplanet's Present hook. When we capture before it, the
    // present counter already counts the current frame; when we capture after it, it doesn't
    // yet. Either way a camera written now shows up in the next frame.
    const uint64_t next = m_presentIndex.load() + (CapturesAfterOverlay() ? 1 : 0);
    const uint64_t target = next + uint64_t(std::clamp(sb.latencyFrames, 0, 3));
    const int eye = int(target & 1);
    const XrView& view = m_views[eye];

    // The game renders a symmetric frustum, so pick one that covers both eyes' asymmetric FOVs
    // at the back buffer's aspect ratio and tell the runtime exactly what we rendered.
    const float aspect = (m_backbufferWidth > 0 && m_backbufferHeight > 0)
        ? float(m_backbufferWidth) / float(m_backbufferHeight)
        : 1.0f;
    float tanH = 0.0f;
    float tanV = 0.0f;
    for (const XrView& v : m_views) {
        tanH = std::max({tanH, tanf(-v.fov.angleLeft), tanf(v.fov.angleRight)});
        tanV = std::max({tanV, tanf(v.fov.angleUp), tanf(-v.fov.angleDown)});
    }
    sb.headsetFovH = 2.0f * atanf(tanH) * 180.0f / kPi;
    sb.headsetFovV = 2.0f * atanf(tanV) * 180.0f / kPi;
    tanH = std::max(tanH, tanV * aspect);
    tanV = tanH / aspect;
    const XrFovf fov{-atanf(tanH), atanf(tanH), atanf(tanV), -atanf(tanV)};
    sb.outFov = 2.0f * atanf(sb.fovIsHorizontal ? tanH : tanV) * 180.0f / kPi;

    const XrQuaternionf& o = view.pose.orientation;
    Mat3 rot = QuatToMat3({o.x, o.y, o.z, o.w});
    Vec3 pos{view.pose.position.x, view.pose.position.y, view.pose.position.z};
    XrToCamera(rot, pos);
    pos = pos * sb.worldScale;
    if (sb.eyeCheck && eye == 1) {
        RollHalfTurn(rot);
    }
    out = {target, eye, view.pose, fov, rot, pos};
    return true;
}

void XrRuntime::Commit(SharedBlock& sb, const EyePose& e)
{
    sb.outEye = e.eye;
    m_records[e.target % m_records.size()] = {e.target, e.eye, e.xrPose, e.fov};
    sb.composeCount++;
}

bool XrRuntime::ComposeModel(SharedBlock& sb)
{
    std::lock_guard lock(m_mutex);
    EyePose e;
    if (!PrepareEye(sb, e)) {
        return false;
    }

    // Tell the headset the FOV the game really rendered with, which can differ from what we
    // asked for (the cockpit model's Fov may be clamped). A mismatch makes the world slide
    // when you turn your head.
    const float toDegrees = 180.0f / kPi;
    sb.requestedFovH = 2.0f * e.fov.angleRight * toDegrees;
    sb.requestedFovV = 2.0f * e.fov.angleUp * toDegrees;
    if (sb.camFov > 1.0f && sb.camFov < 179.0f && sb.camAspect > 0.1f) {
        const float t = tanf(sb.camFov * 0.5f / toDegrees);
        const bool horizontal = sb.camFovAxis == 2; // EViewportRatio::FovX
        const float tanH = horizontal ? t : t * sb.camAspect;
        const float tanV = horizontal ? t / sb.camAspect : t;
        e.fov = {-atanf(tanH), atanf(tanH), atanf(tanV), -atanf(tanV)};
    }
    sb.renderedFovH = 2.0f * e.fov.angleRight * toDegrees;
    sb.renderedFovV = 2.0f * e.fov.angleUp * toDegrees;

    // The cockpit camera is car * seat(RelativePos, PitchYawRoll). Put the eye pose inside the
    // seat transform: seat' = seat * eye.
    const Mat3 seat = ModelRotation(sb.modelBasePyr, sb.modelFlags);
    const Mat3 total = Mul(seat, e.rot);
    const Vec3 position = ModelPosToOurs(sb.modelBasePos, sb.modelFlags) + Mul(seat, e.pos);
    ModelAnglesFromRotation(total, sb.modelFlags, sb.outPyr);
    OurPosToModel(position, sb.modelFlags, sb.outPos);

    // Logged/calibrated in our own Euler convention, the same one MeasureCamera uses.
    float pitch, yaw, roll;
    Mat3ToEuler(total, pitch, yaw, roll);

    const float requested[6] = {pitch * toDegrees, yaw * toDegrees, roll * toDegrees,
        position.x, position.y, position.z};
    memcpy(sb.requestedRel, requested, sizeof(requested));
    RequestedRecord& record = m_requested[e.target % m_requested.size()];
    record.target = e.target;
    memcpy(record.values, requested, sizeof(requested));
    Commit(sb, e);
    return true;
}

void XrRuntime::RequestedForCurrentFrame(const SharedBlock& sb, float out[6])
{
    std::lock_guard lock(m_mutex);
    // The game computes its camera latencyFrames - 1 frames ahead of the frame being presented
    // (that's why a camera write needs the latency), so the camera the plugin reads now belongs
    // to that frame.
    const uint64_t next = m_presentIndex.load() + (CapturesAfterOverlay() ? 1 : 0);
    const uint64_t current = next + uint64_t(std::clamp(sb.latencyFrames, 0, 3)) - 1;
    const RequestedRecord& record = m_requested[current % m_requested.size()];
    memcpy(out, record.target == current ? record.values : sb.requestedRel, 6 * sizeof(float));
}

void XrRuntime::LogStats()
{
    const uint64_t now = GetTickCount64();
    if (m_statsTick == 0) {
        m_statsTick = now;
        return;
    }
    if (now - m_statsTick < kStatsIntervalMs) {
        return;
    }
    const SharedBlock& sb = Shared();
    const float seconds = float(now - m_statsTick) / 1000.0f;
    Log("stats: %.0f presents/s, %.0f composes/s, %.0f xr frames/s, "
        "wait %.1f ms, eye mismatches %u, stereo %d, session %d",
        (sb.presentCount - m_statsPresents) / seconds, (sb.composeCount - m_statsComposes) / seconds,
        (sb.xrFramesSubmitted - m_statsXrFrames) / seconds, sb.waitFrameMs, sb.eyeMismatchCount,
        (sb.status & Status_Stereo) ? 1 : 0, m_sessionRunning ? 1 : 0);
    if (sb.composeCount != m_statsComposes) {
        const float* r = sb.requestedRel;
        const float* m = sb.measuredRel;
        Log("camera vs car: requested p/y/r %.1f %.1f %.1f deg, pos %.3f %.3f %.3f m; "
            "measured p/y/r %.1f %.1f %.1f deg, pos %.3f %.3f %.3f m; model flags 0x%x",
            r[0], r[1], r[2], r[3], r[4], r[5], m[0], m[1], m[2], m[3], m[4], m[5], unsigned(sb.modelFlags));
        Log("fov: asked game for %.1f x %.1f deg, game rendered %.1f deg (axis %d, aspect %.3f) "
            "= %.1f x %.1f deg sent to headset",
            sb.requestedFovH, sb.requestedFovV, sb.camFov, sb.camFovAxis, sb.camAspect, sb.renderedFovH,
            sb.renderedFovV);
        Log("calibration gains (samples): pitch %.3f (%d), yaw %.3f (%d), roll %.3f (%d), "
            "x %.3f (%d), y %.3f (%d), z %.3f (%d)",
            sb.calibGain[0], sb.calibSamples[0], sb.calibGain[1], sb.calibSamples[1], sb.calibGain[2],
            sb.calibSamples[2], sb.calibGain[3], sb.calibSamples[3], sb.calibGain[4], sb.calibSamples[4],
            sb.calibGain[5], sb.calibSamples[5]);
    }
    m_statsTick = now;
    m_statsPresents = sb.presentCount;
    m_statsComposes = sb.composeCount;
    m_statsXrFrames = sb.xrFramesSubmitted;
}

} // namespace tmvr
