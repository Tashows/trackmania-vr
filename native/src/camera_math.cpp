#include "camera_math.h"

#include <algorithm>
#include <cmath>

namespace tmvr {

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

void XrToCamera(Mat3& rot, Vec3& pos)
{
    // S = diag(1, 1, -1): only the entries mixing Z with X or Y change sign.
    rot.m[0][2] = -rot.m[0][2];
    rot.m[1][2] = -rot.m[1][2];
    rot.m[2][0] = -rot.m[2][0];
    rot.m[2][1] = -rot.m[2][1];
    pos.z = -pos.z;
}

void RollHalfTurn(Mat3& rot)
{
    for (int i = 0; i < 3; ++i) {
        rot.m[i][0] = -rot.m[i][0];
        rot.m[i][1] = -rot.m[i][1];
    }
}

} // namespace tmvr
