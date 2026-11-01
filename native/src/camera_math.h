#pragma once

namespace tmvr {

struct Vec3 {
    float x, y, z;
};

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(const Vec3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }

struct Quat {
    float x, y, z, w;
};

// m[row][col]. For a rotation, column i is where basis axis i ends up.
struct Mat3 {
    float m[3][3];
};

// Trackmania iso4 memory layout: xx xy xz yx yy yz zx zy zz tx ty tz.
struct Iso4 {
    float v[12];
};

Mat3 QuatToMat3(const Quat& q);

// Maps a pose from OpenXR space (right-handed, +Y up, -Z forward) into the game camera's
// local space (X right, Y up, +Z forward) by mirroring Z: R' = S R S, p' = S p.
void XrToCamera(Mat3& rot, Vec3& pos);

// Rotates the pose 180 degrees around its own forward axis (used by the eye check).
void RollHalfTurn(Mat3& rot);

Mat3 Mul(const Mat3& a, const Mat3& b);
Vec3 Mul(const Mat3& a, const Vec3& v);
inline float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 Cross(const Vec3& a, const Vec3& b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// Euler angles in radians, in a frame with X right, Y up, Z forward:
// R = Ry(yaw) * Rx(pitch) * Rz(roll). Positive yaw looks right, positive pitch looks down.
Mat3 EulerToMat3(float pitch, float yaw, float roll);
void Mat3ToEuler(const Mat3& r, float& pitch, float& yaw, float& roll);

// Euler angles applied in any order, same frame and axes as above. order lists the axes
// outermost first (0 = X/pitch, 1 = Y/yaw, 2 = Z/roll): R = R[order0] * R[order1] * R[order2].
// kRotationOrders[0] is roll-pitch-yaw (Z X Y), [1] yaw-pitch-roll (Y X Z, same as EulerToMat3).
constexpr int kRotationOrderCount = 6;
extern const int kRotationOrders[kRotationOrderCount][3];
Mat3 OrderedEulerToMat3(const int order[3], float pitch, float yaw, float roll);
void Mat3ToOrderedEuler(const Mat3& r, const int order[3], float& pitch, float& yaw, float& roll);

// Axis i of an iso4 (row i) and its translation.
inline Vec3 IsoAxis(const Iso4& iso, int axis) { return {iso.v[axis * 3], iso.v[axis * 3 + 1], iso.v[axis * 3 + 2]}; }
inline Vec3 IsoPosition(const Iso4& iso) { return {iso.v[9], iso.v[10], iso.v[11]}; }

} // namespace tmvr
