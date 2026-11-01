// Checks the camera math used by the cockpit-model VR camera.
#include "../src/camera_math.h"
#include "../src/cockpit.h"
#include "../src/shared_block.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace tmvr;

namespace {

constexpr float kPi = 3.14159265358979f;
int g_failures = 0;

void Check(bool ok, const char* what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        ++g_failures;
    }
}

bool Near(float a, float b, float eps = 1e-3f) { return fabsf(a - b) < eps; }

Iso4 IsoFromMat(const Mat3& r, const Vec3& t)
{
    // Rows hold the axes (column i of r is axis i).
    Iso4 iso{};
    for (int axis = 0; axis < 3; ++axis) {
        for (int c = 0; c < 3; ++c) {
            iso.v[axis * 3 + c] = r.m[c][axis];
        }
    }
    iso.v[9] = t.x;
    iso.v[10] = t.y;
    iso.v[11] = t.z;
    return iso;
}

} // namespace

int main()
{
    // Euler round trip.
    bool roundTrip = true;
    for (float p = -1.2f; p <= 1.2f; p += 0.3f) {
        for (float y = -3.0f; y <= 3.0f; y += 0.5f) {
            for (float r = -3.0f; r <= 3.0f; r += 0.5f) {
                float p2, y2, r2;
                Mat3ToEuler(EulerToMat3(p, y, r), p2, y2, r2);
                roundTrip = roundTrip && Near(p, p2) && Near(y, y2) && Near(r, r2);
            }
        }
    }
    Check(roundTrip, "Euler -> matrix -> Euler round trip");

    const Vec3 forward{0, 0, 1};
    Check(Mul(EulerToMat3(0, 0.5f, 0), forward).x > 0.4f, "positive yaw looks right");
    Check(Mul(EulerToMat3(0.5f, 0, 0), forward).y < -0.4f, "positive pitch looks down");

    // OpenXR: turning the head 30 degrees left is +30 degrees about +Y (right-handed).
    const float half = 15.0f * kPi / 180.0f;
    Mat3 head = QuatToMat3({0, sinf(half), 0, cosf(half)});
    Vec3 pos{0.1f, 0.2f, -0.3f}; // 0.3 m forward in OpenXR
    XrToCamera(head, pos);
    float p, y, r;
    Mat3ToEuler(head, p, y, r);
    Check(Near(y * 180.0f / kPi, -30.0f, 0.01f) && Near(p, 0) && Near(r, 0), "head turned left -> yaw -30");
    Check(Near(pos.z, 0.3f), "OpenXR forward (-Z) -> our forward (+Z)");

    // OpenXR: looking down 20 degrees is -20 degrees about +X.
    const float halfPitch = -10.0f * kPi / 180.0f;
    head = QuatToMat3({sinf(halfPitch), 0, 0, cosf(halfPitch)});
    pos = {};
    XrToCamera(head, pos);
    Mat3ToEuler(head, p, y, r);
    Check(Near(p * 180.0f / kPi, 20.0f, 0.01f), "head looks down -> pitch +20");

    // Every rotation order round-trips (the middle angle stays within +-90 degrees).
    bool orderRoundTrip = true;
    for (int o = 0; o < kRotationOrderCount; ++o) {
        const int* order = kRotationOrders[o];
        for (float a = -3.0f; a <= 3.0f; a += 0.5f) {
            for (float b = -1.2f; b <= 1.2f; b += 0.3f) {
                for (float c = -3.0f; c <= 3.0f; c += 0.5f) {
                    float angles[3];
                    angles[order[0]] = a;
                    angles[order[1]] = b;
                    angles[order[2]] = c;
                    float p2, y2, r2;
                    Mat3ToOrderedEuler(OrderedEulerToMat3(order, angles[0], angles[1], angles[2]), order, p2, y2, r2);
                    orderRoundTrip = orderRoundTrip && Near(angles[0], p2) && Near(angles[1], y2) && Near(angles[2], r2);
                }
            }
        }
    }
    Check(orderRoundTrip, "all 6 rotation orders round trip");

    // yaw-pitch-roll is the same convention as EulerToMat3.
    {
        const Mat3 a = EulerToMat3(0.3f, -1.1f, 0.7f);
        const Mat3 b = OrderedEulerToMat3(kRotationOrders[1], 0.3f, -1.1f, 0.7f);
        bool same = true;
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                same = same && Near(a.m[i][j], b.m[i][j]);
            }
        }
        Check(same, "yaw-pitch-roll order matches EulerToMat3");
    }

    // Model field conversions round-trip for every flag combination and rotation order.
    bool conversions = true;
    const Mat3 someRotation = EulerToMat3(0.4f, -0.9f, 0.6f);
    for (int combo = 0; combo < 128 * kRotationOrderCount; ++combo) {
        const int flags = (combo % 128) | ((combo / 128) << Model_OrderShift);
        float fields[3];
        ModelAnglesFromRotation(someRotation, flags, fields);
        const Mat3 back = ModelRotation(fields, flags);
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                conversions = conversions && Near(back.m[i][j], someRotation.m[i][j]);
            }
        }
        const float in[3] = {0.1f, -0.7f, 0.3f};
        float posBack[3];
        OurPosToModel(ModelPosToOurs(in, flags), flags, posBack);
        for (int i = 0; i < 3; ++i) {
            conversions = conversions && Near(in[i], posBack[i]);
        }
    }
    Check(conversions, "model field conversions round-trip for all flags");

    // Calibration: the game renders exactly what we asked for -> gains of +1.
    static SharedBlock sb{};
    const float carLeft[3] = {1, 0, 0} /* the game's "Left" points right */, carUp[3] = {0, 1, 0}, carDir[3] = {0, 0, 1}, carPos[3] = {5, 6, 7};
    auto runCalibration = [&](float yawSign, float xSign) {
        ResetCalibration(sb);
        memcpy(sb.carLeft, carLeft, sizeof(carLeft));
        memcpy(sb.carUp, carUp, sizeof(carUp));
        memcpy(sb.carDir, carDir, sizeof(carDir));
        memcpy(sb.carPos, carPos, sizeof(carPos));
        for (int i = 0; i < 40; ++i) {
            const float yawDeg = -40.0f + 2.0f * i;
            const float x = -0.2f + 0.01f * i;
            const float requested[6] = {5, yawDeg, 0, x, 1.1f, 0.2f};
            const Mat3 rendered = EulerToMat3(5 * kPi / 180, yawSign * yawDeg * kPi / 180, 0);
            const Iso4 cam = IsoFromMat(rendered, {5 + xSign * x, 6 + 1.1f, 7 + 0.2f});
            memcpy(sb.cameraLoc, cam.v, sizeof(cam.v));
            MeasureCamera(sb, requested);
        }
    };
    runCalibration(1, 1);
    Check(Near(sb.calibGain[1], 1, 0.01f) && Near(sb.calibGain[3], 1, 0.01f) && sb.calibSamples[1] > 20,
        "calibration: correct mapping -> yaw and x gains +1");
    Check(Near(sb.measuredRel[0], 5, 0.01f) && Near(sb.measuredRel[4], 1.1f, 0.001f),
        "measurement recovers pitch and height relative to the car");
    runCalibration(-1, -1);
    Check(Near(sb.calibGain[1], -1, 0.01f) && Near(sb.calibGain[3], -1, 0.01f),
        "calibration: flipped mapping -> yaw and x gains -1");

    // The game's camera X axis points left (like the car's Left/Up/Dir). Roll must still be
    // measured physically: positive = right side up.
    {
        const float rollDeg = 20.0f;
        const Mat3 rendered = EulerToMat3(0, 0, rollDeg * kPi / 180);
        Iso4 cam = IsoFromMat(rendered, {5, 7, 7});
        for (int c = 0; c < 3; ++c) {
            cam.v[c] = -cam.v[c]; // X row: right -> left
        }
        memcpy(sb.cameraLoc, cam.v, sizeof(cam.v));
        const float requested[6] = {0, 0, rollDeg, 0, 1, 0};
        MeasureCamera(sb, requested);
        Check(Near(sb.measuredRel[2], rollDeg, 0.01f), "roll measured correctly when the camera's X axis points left");
    }

    printf(g_failures ? "\n%d FAILED\n" : "\nall passed\n", g_failures);
    return g_failures ? 1 : 0;
}
