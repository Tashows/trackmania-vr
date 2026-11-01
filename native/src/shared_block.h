#pragma once
#include <cstddef>
#include <cstdint>

// Memory shared with the Openplanet plugin. The plugin reads and writes these fields with
// Dev::Read*/Dev::Write at fixed byte offsets, so any layout change must also be made in
// plugin/Native.as (and kSharedVersion bumped).
namespace tmvr {

constexpr uint32_t kSharedVersion = 6;

enum StatusFlags : uint32_t {
    Status_HooksInstalled = 1u << 0,
    Status_XrInstance     = 1u << 1,
    Status_SessionRunning = 1u << 2,
    Status_Stereo         = 1u << 3, // last presented frame went to the headset as a stereo eye
};

enum AxisFlags : int32_t {
    Axis_FlipX       = 1 << 0,
    Axis_FlipY       = 1 << 1,
    Axis_FlipZ       = 1 << 2,
    Axis_ColumnBasis = 1 << 3, // iso4 stores axes in columns instead of rows
};

#pragma pack(push, 4)
struct SharedBlock {
    uint32_t version;           // 0
    uint32_t status;            // 4   StatusFlags

    // Inputs, written by the plugin before TMVR_Compose()
    float    baseLoc[12];       // 8   game camera iso4: xx xy xz yx yy yz zx zy zz tx ty tz
    float    worldScale;        // 56  game units per real-world meter
    int32_t  latencyFrames;     // 60  presents between the camera write and the frame that shows it
    int32_t  eyeCheck;          // 64  1 = render the right eye upside down (calibration)
    int32_t  axisFlags;         // 68  AxisFlags
    float    fovScale;          // 72  multiplier on the FOV written to the game (calibration)
    int32_t  fovIsHorizontal;   // 76  camera Fov is horizontal (EViewportRatio::FovX)
    int32_t  xrEnabled;         // 80  1 = run OpenXR; 0 = Present hook only
    int32_t  captureAfterOverlay; // 84 1 = frames include Openplanet's overlay (UI visible in VR)

    // Outputs, valid when TMVR_Compose() returns 1
    float    outLoc[12];        // 88
    float    outFov;            // 136 degrees
    int32_t  outEye;            // 140 0 = left, 1 = right

    // Stats, written by the native side
    uint32_t presentCount;      // 144
    uint32_t composeCount;      // 148
    uint32_t baseReusedCount;   // 152 compose calls where the game had not overwritten our camera
    uint32_t xrFramesSubmitted; // 156
    int32_t  backbufferWidth;   // 160
    int32_t  backbufferHeight;  // 164
    int32_t  recommendedWidth;  // 168
    int32_t  recommendedHeight; // 172
    float    waitFrameMs;       // 176
    uint32_t eyeMismatchCount;  // 180 eye images that arrived out of left/right order
    char     lastError[256];    // 184

    // Cockpit camera model (CPlugVehicleCameraInternalModel), used by TMVR_ComposeModel().
    // The game builds the cockpit camera from these during its own update, so writing them
    // works no matter when the plugin runs, unlike writing CHmsCamera.Location.
    float    modelBasePyr[3];   // 440 the model's original PitchYawRoll
    float    modelBasePos[3];   // 452 the model's original RelativePos
    int32_t  modelFlags;        // 464 ModelFlags: how the game interprets those fields
    float    outPyr[3];         // 468
    float    outPos[3];         // 480

    // TMVR_Measure() input: the car's axes and position, with baseLoc = rendered camera
    float    carLeft[3];        // 492 CSceneVehicleVisState::Left (points to the car's right)
    float    carUp[3];          // 504
    float    carDir[3];         // 516
    float    carPos[3];         // 528

    // Camera relative to the car in our convention: pitch, yaw, roll (degrees), x, y, z
    // (meters, x right). requested = what the last compose asked for, measured = what the
    // game rendered. Gains compare the two since TMVR_CalibrationReset(): 1 = correct,
    // -1 = sign flipped, ~0.017 = radians read as degrees.
    float    requestedRel[6];   // 540
    float    measuredRel[6];    // 564
    float    calibGain[6];      // 588
    int32_t  calibSamples[6];   // 612

    // The FOV the game actually rendered the last frame with (CHmsCamera Fov / Width_Height /
    // m_ViewportRatio). It can differ from what we asked for (the game may clamp it), and the
    // headset must be told the real one or the world slides when you turn your head.
    float    camFov;            // 636 degrees, 0 = unknown
    float    camAspect;         // 640 width / height
    int32_t  camFovAxis;        // 644 CHmsCamera::EViewportRatio: 0 None, 1 FovY, 2 FovX
    float    requestedFovH;     // 648 degrees, what we asked the game for
    float    requestedFovV;     // 652
    float    renderedFovH;      // 656 degrees, what the headset is told
    float    renderedFovV;      // 660
    float    headsetFovH;       // 664 degrees, symmetric FOV covering both eyes
    float    headsetFovV;       // 668

    // The flat virtual screen (menus, replays): fixed in the world in front of the recentered head.
    float    screenDistance;    // 672 meters, 0 = default
    float    screenWidth;       // 676 meters, 0 = default
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

static_assert(offsetof(SharedBlock, baseLoc) == 8);
static_assert(offsetof(SharedBlock, worldScale) == 56);
static_assert(offsetof(SharedBlock, latencyFrames) == 60);
static_assert(offsetof(SharedBlock, eyeCheck) == 64);
static_assert(offsetof(SharedBlock, axisFlags) == 68);
static_assert(offsetof(SharedBlock, fovScale) == 72);
static_assert(offsetof(SharedBlock, fovIsHorizontal) == 76);
static_assert(offsetof(SharedBlock, xrEnabled) == 80);
static_assert(offsetof(SharedBlock, outLoc) == 88);
static_assert(offsetof(SharedBlock, outFov) == 136);
static_assert(offsetof(SharedBlock, outEye) == 140);
static_assert(offsetof(SharedBlock, presentCount) == 144);
static_assert(offsetof(SharedBlock, waitFrameMs) == 176);
static_assert(offsetof(SharedBlock, eyeMismatchCount) == 180);
static_assert(offsetof(SharedBlock, lastError) == 184);
static_assert(offsetof(SharedBlock, modelBasePyr) == 440);
static_assert(offsetof(SharedBlock, modelBasePos) == 452);
static_assert(offsetof(SharedBlock, modelFlags) == 464);
static_assert(offsetof(SharedBlock, outPyr) == 468);
static_assert(offsetof(SharedBlock, outPos) == 480);
static_assert(offsetof(SharedBlock, carLeft) == 492);
static_assert(offsetof(SharedBlock, carUp) == 504);
static_assert(offsetof(SharedBlock, carDir) == 516);
static_assert(offsetof(SharedBlock, carPos) == 528);
static_assert(offsetof(SharedBlock, requestedRel) == 540);
static_assert(offsetof(SharedBlock, measuredRel) == 564);
static_assert(offsetof(SharedBlock, calibGain) == 588);
static_assert(offsetof(SharedBlock, calibSamples) == 612);
static_assert(offsetof(SharedBlock, camFov) == 636);
static_assert(offsetof(SharedBlock, camFovAxis) == 644);
static_assert(offsetof(SharedBlock, renderedFovV) == 660);
static_assert(offsetof(SharedBlock, captureAfterOverlay) == 84);
static_assert(offsetof(SharedBlock, headsetFovV) == 668);
static_assert(offsetof(SharedBlock, screenWidth) == 676);

SharedBlock& Shared();

} // namespace tmvr
