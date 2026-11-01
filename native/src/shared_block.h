#pragma once
#include <cstddef>
#include <cstdint>

// Memory shared with the Openplanet plugin. The plugin reads and writes these fields with
// Dev::Read*/Dev::Write at fixed byte offsets, so any layout change must also be made in
// plugin/Native.as (and kSharedVersion bumped).
namespace tmvr {

constexpr uint32_t kSharedVersion = 7;

enum StatusFlags : uint32_t {
    Status_HooksInstalled = 1u << 0,
    Status_XrInstance     = 1u << 1,
    Status_SessionRunning = 1u << 2,
    Status_Stereo         = 1u << 3, // last presented frame went to the headset as a stereo eye
};

#pragma pack(push, 4)
struct SharedBlock {
    uint32_t version;           // 0
    uint32_t status;            // 4   StatusFlags

    // TMVR_Measure() input: the camera the game rendered, as an iso4
    float    cameraLoc[12];     // 8   xx xy xz yx yy yz zx zy zz tx ty tz

    // Inputs, written by the plugin before TMVR_ComposeModel()
    float    worldScale;        // 56  game units per real-world meter
    int32_t  latencyFrames;     // 60  presents between the camera write and the frame that shows it
    int32_t  eyeCheck;          // 64  1 = render the right eye upside down
    int32_t  fovIsHorizontal;   // 68  camera Fov is horizontal (EViewportRatio::FovX)
    int32_t  xrEnabled;         // 72  1 = run OpenXR; 0 = Present hook only
    int32_t  captureAfterOverlay; // 76 1 = frames include Openplanet's overlay (UI visible in VR)

    // Outputs, valid when TMVR_ComposeModel() returns 1
    float    outFov;            // 80  degrees
    int32_t  outEye;            // 84  0 = left, 1 = right

    // Stats, written by the native side
    uint32_t presentCount;      // 88
    uint32_t composeCount;      // 92
    uint32_t xrFramesSubmitted; // 96
    int32_t  backbufferWidth;   // 100
    int32_t  backbufferHeight;  // 104
    int32_t  recommendedWidth;  // 108
    int32_t  recommendedHeight; // 112
    float    waitFrameMs;       // 116
    uint32_t eyeMismatchCount;  // 120 eye images that arrived out of left/right order
    char     lastError[256];    // 124

    // Cockpit camera model (CPlugVehicleCameraInternalModel). The game builds the cockpit
    // camera from these during its own update, before rendering.
    float    modelBasePyr[3];   // 380 the model's original PitchYawRoll
    float    modelBasePos[3];   // 392 the model's original RelativePos
    int32_t  modelFlags;        // 404 ModelFlags: how the game interprets those fields
    float    outPyr[3];         // 408
    float    outPos[3];         // 420

    // TMVR_Measure() input: the car's axes and position
    float    carLeft[3];        // 432 CSceneVehicleVisState::Left (points to the car's right)
    float    carUp[3];          // 444
    float    carDir[3];         // 456
    float    carPos[3];         // 468

    // Camera relative to the car in our convention: pitch, yaw, roll (degrees), x, y, z
    // (meters, x right). requested = what the last compose asked for, measured = what the
    // game rendered. Gains compare the two since TMVR_CalibrationReset(): 1 = correct,
    // -1 = sign flipped, ~0.017 = radians read as degrees. Diagnostics only (logged).
    float    requestedRel[6];   // 480
    float    measuredRel[6];    // 504
    float    calibGain[6];      // 528
    int32_t  calibSamples[6];   // 552

    // The FOV the game actually rendered the last frame with (CHmsCamera Fov / Width_Height /
    // m_ViewportRatio). It can differ from what we asked for (the game may clamp it), and the
    // headset must be told the real one or the world slides when you turn your head.
    float    camFov;            // 576 degrees, 0 = unknown
    float    camAspect;         // 580 width / height
    int32_t  camFovAxis;        // 584 CHmsCamera::EViewportRatio: 0 None, 1 FovY, 2 FovX
    float    requestedFovH;     // 588 degrees, what we asked the game for
    float    requestedFovV;     // 592
    float    renderedFovH;      // 596 degrees, what the headset is told
    float    renderedFovV;      // 600
    float    headsetFovH;       // 604 degrees, symmetric FOV covering both eyes
    float    headsetFovV;       // 608

    // The flat virtual screen (menus, replays): fixed in the world in front of the recentered head.
    float    screenDistance;    // 612 meters, 0 = default
    float    screenWidth;       // 616 meters, 0 = default
};
#pragma pack(pop)

enum ModelFlags : int32_t {
    Model_AnglesInDegrees = 1 << 0,
    Model_NegatePitch     = 1 << 1,
    Model_NegateYaw       = 1 << 2,
    Model_NegateRoll      = 1 << 3,
    Model_NegatePosX      = 1 << 4,
    Model_NegatePosY      = 1 << 5,
    Model_NegatePosZ      = 1 << 6,
    Model_OrderShift      = 8, // bits 8-10: index into kRotationOrders
};

static_assert(offsetof(SharedBlock, cameraLoc) == 8);
static_assert(offsetof(SharedBlock, worldScale) == 56);
static_assert(offsetof(SharedBlock, latencyFrames) == 60);
static_assert(offsetof(SharedBlock, eyeCheck) == 64);
static_assert(offsetof(SharedBlock, fovIsHorizontal) == 68);
static_assert(offsetof(SharedBlock, xrEnabled) == 72);
static_assert(offsetof(SharedBlock, captureAfterOverlay) == 76);
static_assert(offsetof(SharedBlock, outFov) == 80);
static_assert(offsetof(SharedBlock, outEye) == 84);
static_assert(offsetof(SharedBlock, presentCount) == 88);
static_assert(offsetof(SharedBlock, composeCount) == 92);
static_assert(offsetof(SharedBlock, xrFramesSubmitted) == 96);
static_assert(offsetof(SharedBlock, backbufferWidth) == 100);
static_assert(offsetof(SharedBlock, backbufferHeight) == 104);
static_assert(offsetof(SharedBlock, recommendedWidth) == 108);
static_assert(offsetof(SharedBlock, recommendedHeight) == 112);
static_assert(offsetof(SharedBlock, waitFrameMs) == 116);
static_assert(offsetof(SharedBlock, eyeMismatchCount) == 120);
static_assert(offsetof(SharedBlock, lastError) == 124);
static_assert(offsetof(SharedBlock, modelBasePyr) == 380);
static_assert(offsetof(SharedBlock, modelBasePos) == 392);
static_assert(offsetof(SharedBlock, modelFlags) == 404);
static_assert(offsetof(SharedBlock, outPyr) == 408);
static_assert(offsetof(SharedBlock, outPos) == 420);
static_assert(offsetof(SharedBlock, carLeft) == 432);
static_assert(offsetof(SharedBlock, carUp) == 444);
static_assert(offsetof(SharedBlock, carDir) == 456);
static_assert(offsetof(SharedBlock, carPos) == 468);
static_assert(offsetof(SharedBlock, requestedRel) == 480);
static_assert(offsetof(SharedBlock, measuredRel) == 504);
static_assert(offsetof(SharedBlock, calibGain) == 528);
static_assert(offsetof(SharedBlock, calibSamples) == 552);
static_assert(offsetof(SharedBlock, camFov) == 576);
static_assert(offsetof(SharedBlock, camAspect) == 580);
static_assert(offsetof(SharedBlock, camFovAxis) == 584);
static_assert(offsetof(SharedBlock, requestedFovH) == 588);
static_assert(offsetof(SharedBlock, requestedFovV) == 592);
static_assert(offsetof(SharedBlock, renderedFovH) == 596);
static_assert(offsetof(SharedBlock, renderedFovV) == 600);
static_assert(offsetof(SharedBlock, headsetFovH) == 604);
static_assert(offsetof(SharedBlock, headsetFovV) == 608);
static_assert(offsetof(SharedBlock, screenDistance) == 612);
static_assert(offsetof(SharedBlock, screenWidth) == 616);

SharedBlock& Shared();

} // namespace tmvr
