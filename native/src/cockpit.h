#pragma once
#include "camera_math.h"

namespace tmvr {

struct SharedBlock;

// Conversions between the cockpit camera model's fields (CPlugVehicleCameraInternalModel
// PitchYawRoll / RelativePos) and rotations/positions in our frame (X right, Y up, Z forward).
// ModelFlags describe units, which fields are negated relative to our axes, and the order the
// game applies the angles in (Model_OrderShift, see kRotationOrders).
Mat3 ModelRotation(const float pyr[3], int flags);
void ModelAnglesFromRotation(const Mat3& rotation, int flags, float out[3]);
Vec3 ModelPosToOurs(const float pos[3], int flags);
void OurPosToModel(const Vec3& pos, int flags, float out[3]);

// Measures the rendered camera (SharedBlock::cameraLoc) against the car's axes and updates the
// calibration gains (SharedBlock::calibGain) from changes since the last reset. requestedRel is
// what was asked for the frame being measured.
void MeasureCamera(SharedBlock& sb, const float requestedRel[6]);
void ResetCalibration(SharedBlock& sb);

} // namespace tmvr
