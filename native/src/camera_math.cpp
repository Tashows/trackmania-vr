#include "camera_math.h"
#include "shared_block.h"

#include <algorithm>
#include <cmath>

namespace tmvr {
namespace {

void SetAxis(Iso4& iso, int axis, const Vec3& a, bool columnBasis)
{
    if (columnBasis) {
        iso.v[axis] = a.x;
        iso.v[3 + axis] = a.y;
        iso.v[6 + axis] = a.z;
    } else {
        iso.v[axis * 3] = a.x;
        iso.v[axis * 3 + 1] = a.y;
        iso.v[axis * 3 + 2] = a.z;
    }
}

} // namespace

Vec3 IsoAxis(const Iso4& iso, int axis, bool columnBasis)
{
    if (columnBasis) {
        return {iso.v[axis], iso.v[3 + axis], iso.v[6 + axis]};
    }
    return {iso.v[axis * 3], iso.v[axis * 3 + 1], iso.v[axis * 3 + 2]};
}

Mat3 Mul(const Mat3& a, const Mat3& b)
{
    Mat3 r{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
        }
    }
    return r;
}

Vec3 Mul(const Mat3& a, const Vec3& v)
{
    return {
        a.m[0][0] * v.x + a.m[0][1] * v.y + a.m[0][2] * v.z,
        a.m[1][0] * v.x + a.m[1][1] * v.y + a.m[1][2] * v.z,
        a.m[2][0] * v.x + a.m[2][1] * v.y + a.m[2][2] * v.z,
    };
}

Mat3 EulerToMat3(float pitch, float yaw, float roll)
{
    const float cp = cosf(pitch), sp = sinf(pitch);
    const float cy = cosf(yaw), sy = sinf(yaw);
    const float cr = cosf(roll), sr = sinf(roll);
    const Mat3 ry{{{cy, 0, sy}, {0, 1, 0}, {-sy, 0, cy}}};
    const Mat3 rx{{{1, 0, 0}, {0, cp, -sp}, {0, sp, cp}}};
    const Mat3 rz{{{cr, -sr, 0}, {sr, cr, 0}, {0, 0, 1}}};
    return Mul(Mul(ry, rx), rz);
}

void Mat3ToEuler(const Mat3& r, float& pitch, float& yaw, float& roll)
{
    pitch = asinf(std::clamp(-r.m[1][2], -1.0f, 1.0f));
    yaw = atan2f(r.m[0][2], r.m[2][2]);
    roll = atan2f(r.m[1][0], r.m[1][1]);
}

const int kRotationOrders[kRotationOrderCount][3] = {
    {2, 0, 1}, // roll-pitch-yaw
    {1, 0, 2}, // yaw-pitch-roll
    {0, 1, 2}, // pitch-yaw-roll
    {1, 2, 0}, // yaw-roll-pitch
    {2, 1, 0}, // roll-yaw-pitch
    {0, 2, 1}, // pitch-roll-yaw
};

namespace {

Mat3 AxisRotation(int axis, float angle)
{
    const float c = cosf(angle), s = sinf(angle);
    if (axis == 0) return {{{1, 0, 0}, {0, c, -s}, {0, s, c}}};
    if (axis == 1) return {{{c, 0, s}, {0, 1, 0}, {-s, 0, c}}};
    return {{{c, -s, 0}, {s, c, 0}, {0, 0, 1}}};
}

} // namespace

Mat3 OrderedEulerToMat3(const int order[3], float pitch, float yaw, float roll)
{
    const float angles[3] = {pitch, yaw, roll};
    return Mul(Mul(AxisRotation(order[0], angles[order[0]]), AxisRotation(order[1], angles[order[1]])),
        AxisRotation(order[2], angles[order[2]]));
}

void Mat3ToOrderedEuler(const Mat3& r, const int order[3], float& pitch, float& yaw, float& roll)
{
    // Tait-Bryan angles for R = Ri(a) * Rj(b) * Rk(c); e = +1 when (i, j, k) is a cyclic order.
    const int i = order[0], j = order[1], k = order[2];
    const float e = (j - i + 3) % 3 == 1 ? 1.0f : -1.0f;
    float angles[3];
    angles[j] = asinf(std::clamp(e * r.m[i][k], -1.0f, 1.0f));
    angles[i] = atan2f(-e * r.m[j][k], r.m[k][k]);
    angles[k] = atan2f(-e * r.m[i][j], r.m[i][i]);
    pitch = angles[0];
    yaw = angles[1];
    roll = angles[2];
}

Mat3 QuatToMat3(const Quat& q)
{
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    Mat3 r;
    r.m[0][0] = 1 - 2 * (yy + zz); r.m[0][1] = 2 * (xy - wz);     r.m[0][2] = 2 * (xz + wy);
    r.m[1][0] = 2 * (xy + wz);     r.m[1][1] = 1 - 2 * (xx + zz); r.m[1][2] = 2 * (yz - wx);
    r.m[2][0] = 2 * (xz - wy);     r.m[2][1] = 2 * (yz + wx);     r.m[2][2] = 1 - 2 * (xx + yy);
    return r;
}

void MirrorPose(Mat3& rot, Vec3& pos, int axisFlags)
{
    const float s[3] = {
        (axisFlags & Axis_FlipX) ? -1.0f : 1.0f,
        (axisFlags & Axis_FlipY) ? -1.0f : 1.0f,
        (axisFlags & Axis_FlipZ) ? -1.0f : 1.0f,
    };
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            rot.m[i][j] *= s[i] * s[j];
        }
    }
    pos = {pos.x * s[0], pos.y * s[1], pos.z * s[2]};
}

void RollHalfTurn(Mat3& rot)
{
    for (int i = 0; i < 3; ++i) {
        rot.m[i][0] = -rot.m[i][0];
        rot.m[i][1] = -rot.m[i][1];
    }
}

Iso4 ComposeCamera(const Iso4& base, const Mat3& eyeRot, const Vec3& eyePos, bool columnBasis)
{
    Vec3 axes[3];
    for (int i = 0; i < 3; ++i) {
        axes[i] = IsoAxis(base, i, columnBasis);
    }

    Iso4 out = base;
    for (int i = 0; i < 3; ++i) {
        // Column i of eyeRot is the eye's axis i in camera-local coordinates.
        Vec3 a = axes[0] * eyeRot.m[0][i] + axes[1] * eyeRot.m[1][i] + axes[2] * eyeRot.m[2][i];
        SetAxis(out, i, a, columnBasis);
    }

    Vec3 t{base.v[9], base.v[10], base.v[11]};
    t = t + axes[0] * eyePos.x + axes[1] * eyePos.y + axes[2] * eyePos.z;
    out.v[9] = t.x;
    out.v[10] = t.y;
    out.v[11] = t.z;
    return out;
}

} // namespace tmvr
