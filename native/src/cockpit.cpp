#include "cockpit.h"
#include "shared_block.h"

#include <cmath>
#include <cstring>

namespace tmvr {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kToDegrees = 180.0f / kPi;
// Changes smaller than this are noise for calibration purposes.
constexpr float kAngleThreshold = 3.0f;      // degrees
constexpr float kPositionThreshold = 0.02f;  // meters
constexpr float kMaxCockpitOffset = 3.0f;    // meters from the car's origin

struct Calibration {
    bool haveReference = false;
    float requested[6] = {};
    float measured[6] = {};
    double sumXY[6] = {};
    double sumXX[6] = {};
};

Calibration g_calibration;
int g_calibrationFlags = -1; // modelFlags the current samples were taken with

float AngleSign(int flags, int negateFlag) { return (flags & negateFlag) ? -1.0f : 1.0f; }

float WrapDegrees(float a)
{
    while (a > 180.0f) a -= 360.0f;
    while (a < -180.0f) a += 360.0f;
    return a;
}

} // namespace

const int* ModelRotationOrder(int flags)
{
    const int index = (flags >> Model_OrderShift) & 0x7;
    return kRotationOrders[index < kRotationOrderCount ? index : 0];
}

Mat3 ModelRotation(const float pyr[3], int flags)
{
    const float unit = (flags & Model_AnglesInDegrees) ? 1.0f / kToDegrees : 1.0f;
    return OrderedEulerToMat3(ModelRotationOrder(flags), pyr[0] * unit * AngleSign(flags, Model_NegatePitch),
        pyr[1] * unit * AngleSign(flags, Model_NegateYaw), pyr[2] * unit * AngleSign(flags, Model_NegateRoll));
}

void ModelAnglesFromRotation(const Mat3& rotation, int flags, float out[3])
{
    float pitch, yaw, roll;
    Mat3ToOrderedEuler(rotation, ModelRotationOrder(flags), pitch, yaw, roll);
    const float unit = (flags & Model_AnglesInDegrees) ? kToDegrees : 1.0f;
    out[0] = pitch * unit * AngleSign(flags, Model_NegatePitch);
    out[1] = yaw * unit * AngleSign(flags, Model_NegateYaw);
    out[2] = roll * unit * AngleSign(flags, Model_NegateRoll);
}

Vec3 ModelPosToOurs(const float pos[3], int flags)
{
    return {
        pos[0] * AngleSign(flags, Model_NegatePosX),
        pos[1] * AngleSign(flags, Model_NegatePosY),
        pos[2] * AngleSign(flags, Model_NegatePosZ),
    };
}

void OurPosToModel(const Vec3& pos, int flags, float out[3])
{
    const float ours[3] = {pos.x, pos.y, pos.z};
    const Vec3 v = ModelPosToOurs(ours, flags); // negation is its own inverse
    out[0] = v.x;
    out[1] = v.y;
    out[2] = v.z;
}

void MeasureCamera(SharedBlock& sb, const float requestedRel[6])
{
    Iso4 camera;
    memcpy(camera.v, sb.cameraLoc, sizeof(camera.v));
    // CSceneVehicleVisState::Left is the car's X axis. Which way it really points is uncertain
    // (tests in VR contradicted each other), so this measurement only feeds the log; the
    // guided calibration lets the player decide.
    const Vec3 carAxes[3] = {
        {sb.carLeft[0], sb.carLeft[1], sb.carLeft[2]}, // right
        {sb.carUp[0], sb.carUp[1], sb.carUp[2]},
        {sb.carDir[0], sb.carDir[1], sb.carDir[2]},
    };

    // Don't rely on which way the camera's X axis points either: derive the camera's right from
    // its up and forward with the same handedness the car's right has to the car's up/forward.
    const Vec3 cameraUp = IsoAxis(camera, 1);
    const Vec3 cameraForward = IsoAxis(camera, 2);
    const float handedness = Dot(Cross(carAxes[1], carAxes[2]), carAxes[0]) < 0 ? -1.0f : 1.0f;
    const Vec3 cameraAxes[3] = {Cross(cameraUp, cameraForward) * handedness, cameraUp, cameraForward};

    // Camera axes expressed in the car's frame.
    Mat3 relative{};
    for (int j = 0; j < 3; ++j) {
        for (int i = 0; i < 3; ++i) {
            relative.m[i][j] = Dot(carAxes[i], cameraAxes[j]);
        }
    }
    float pitch, yaw, roll;
    Mat3ToEuler(relative, pitch, yaw, roll);
    const Vec3 offset = IsoPosition(camera) - Vec3{sb.carPos[0], sb.carPos[1], sb.carPos[2]};
    const float measured[6] = {pitch * kToDegrees, yaw * kToDegrees, roll * kToDegrees,
        Dot(carAxes[0], offset), Dot(carAxes[1], offset), Dot(carAxes[2], offset)};
    memcpy(sb.measuredRel, measured, sizeof(measured));

    // A camera far from the car isn't the cockpit camera (menus, replays, respawn transitions).
    if (fabsf(measured[3]) > kMaxCockpitOffset || fabsf(measured[4]) > kMaxCockpitOffset ||
        fabsf(measured[5]) > kMaxCockpitOffset) {
        return;
    }

    // Samples taken with different settings can't be compared; start over when they change.
    if (sb.modelFlags != g_calibrationFlags) {
        ResetCalibration(sb);
        g_calibrationFlags = sb.modelFlags;
    }

    Calibration& c = g_calibration;
    if (!c.haveReference) {
        memcpy(c.requested, requestedRel, sizeof(c.requested));
        memcpy(c.measured, measured, sizeof(c.measured));
        c.haveReference = true;
        return;
    }
    for (int i = 0; i < 6; ++i) {
        float dr = requestedRel[i] - c.requested[i];
        float dm = measured[i] - c.measured[i];
        if (i < 3) {
            dr = WrapDegrees(dr);
            dm = WrapDegrees(dm);
        }
        if (fabsf(dr) < (i < 3 ? kAngleThreshold : kPositionThreshold)) {
            continue;
        }
        c.sumXY[i] += double(dr) * dm;
        c.sumXX[i] += double(dr) * dr;
        sb.calibSamples[i]++;
        sb.calibGain[i] = float(c.sumXY[i] / c.sumXX[i]);
    }
}

void ResetCalibration(SharedBlock& sb)
{
    g_calibration = {};
    memset(sb.calibGain, 0, sizeof(sb.calibGain));
    memset(sb.calibSamples, 0, sizeof(sb.calibSamples));
}

} // namespace tmvr
