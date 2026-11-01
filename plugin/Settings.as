// Which plugin callback writes the VR camera. Which one lands before the game renders the
// scene is found by testing (see README, "Calibration").
enum WritePoint
{
	InUpdate,
	InRenderEarly,
	InRender
}

// Which CHmsCamera field the game's renderer reads.
enum CameraField
{
	CamLocation,
	CamNextLocation
}

// How the head pose reaches the game.
//  CockpitModel: write the cockpit camera's seat transform (CPlugVehicleCameraInternalModel);
//                the game builds the camera from it during its own update. Cockpit cam only.
//  CameraOverride: overwrite the rendered camera (CHmsCamera) from a plugin callback. Only works
//                if that callback runs between the game's camera update and its rendering.
enum CameraMethod
{
	CockpitModel,
	CameraOverride
}

[Setting category="Camera" name="Camera method"]
CameraMethod S_CameraMethod = CameraMethod::CockpitModel;

[Setting category="Comfort" name="Turn off the game's head motion" description="The cockpit camera's simulated head movement (PilotHeadCoef)."]
bool S_DisableHeadMotion = true;

// How the game interprets the cockpit model's PitchYawRoll / RelativePos. Defaults are what the
// guided calibration settled on in VR (Trackmania 2026-02-02): angles in degrees, nothing inverted.
[Setting category="Calibration" name="Cockpit angles are degrees"]
bool S_ModelDegrees = true;
[Setting category="Calibration" name="Cockpit: invert pitch"]
bool S_ModelNegPitch = false;
[Setting category="Calibration" name="Cockpit: invert yaw"]
bool S_ModelNegYaw = false;
[Setting category="Calibration" name="Cockpit: invert roll"]
bool S_ModelNegRoll = false;
[Setting category="Calibration" name="Cockpit: invert X position"]
bool S_ModelNegPosX = false;
[Setting category="Calibration" name="Cockpit: invert Y position"]
bool S_ModelNegPosY = false;
[Setting category="Calibration" name="Cockpit: invert Z position"]
bool S_ModelNegPosZ = false;

// The order the game combines PitchYawRoll in; only matters for combined movements (e.g. a
// head tilt while turned). Index into these (outermost rotation first):
const array<string> ROTATION_ORDER_NAMES = {
	"roll, pitch, yaw", "yaw, pitch, roll", "pitch, yaw, roll", "yaw, roll, pitch", "roll, yaw, pitch", "pitch, roll, yaw"
};
// Whether yaw is applied outside roll / outside pitch in each order above.
const array<bool> ROTATION_YAW_OUTSIDE_ROLL = { false, true, true, true, false, false };
const array<bool> ROTATION_YAW_OUTSIDE_PITCH = { false, true, false, true, true, false };

[Setting category="Calibration" name="Cockpit rotation order" min=0 max=5 description="Set by calibration steps 7 and 8."]
int S_RotationOrder = 1;

void SetModelFlags(int flags)
{
	S_RotationOrder = (flags >> Native::MODEL_ORDER_SHIFT) & 0x7;
	S_ModelDegrees = (flags & Native::MODEL_DEGREES) != 0;
	S_ModelNegPitch = (flags & Native::MODEL_NEGATE_PITCH) != 0;
	S_ModelNegYaw = (flags & Native::MODEL_NEGATE_YAW) != 0;
	S_ModelNegRoll = (flags & Native::MODEL_NEGATE_ROLL) != 0;
	S_ModelNegPosX = (flags & Native::MODEL_NEGATE_POS_X) != 0;
	S_ModelNegPosY = (flags & Native::MODEL_NEGATE_POS_Y) != 0;
	S_ModelNegPosZ = (flags & Native::MODEL_NEGATE_POS_Z) != 0;
}

int ModelFlags()
{
	int flags = 0;
	if (S_ModelDegrees) flags |= Native::MODEL_DEGREES;
	if (S_ModelNegPitch) flags |= Native::MODEL_NEGATE_PITCH;
	if (S_ModelNegYaw) flags |= Native::MODEL_NEGATE_YAW;
	if (S_ModelNegRoll) flags |= Native::MODEL_NEGATE_ROLL;
	if (S_ModelNegPosX) flags |= Native::MODEL_NEGATE_POS_X;
	if (S_ModelNegPosY) flags |= Native::MODEL_NEGATE_POS_Y;
	if (S_ModelNegPosZ) flags |= Native::MODEL_NEGATE_POS_Z;
	flags |= Math::Clamp(S_RotationOrder, 0, 5) << Native::MODEL_ORDER_SHIFT;
	return flags;
}

[Setting category="General" name="Start VR automatically" description="Otherwise press Start VR in the status window."]
bool S_AutoStartVr = false;

[Setting category="General" name="Stereo VR enabled" description="When off, the game is shown on a flat virtual screen in the headset."]
bool S_StereoEnabled = true;

[Setting category="General" name="Stereo only while in a map" description="Show menus on the flat virtual screen."]
bool S_StereoOnlyInMap = true;

[Setting category="General" name="Stereo only while driving" description="Pause menu, intro, end-of-round replay and podium go to the flat virtual screen."]
bool S_StereoOnlyWhileDriving = true;

[Setting category="General" name="Virtual screen distance" description="Meters in front of you, fixed in the world." min=1 max=8]
float S_ScreenDistance = 3.0f;

[Setting category="General" name="Virtual screen width" description="Meters." min=1 max=10]
float S_ScreenWidth = 3.6f;

[Setting category="General" name="Recenter key"]
VirtualKey S_RecenterKey = VirtualKey::F8;

[Setting category="General" name="Toggle stereo key"]
VirtualKey S_ToggleStereoKey = VirtualKey::F9;

[Setting category="General" name="Show/hide UI in VR key" description="Openplanet's overlay and the VR panel."]
VirtualKey S_PanelKey = VirtualKey::F7;

[Setting category="General" name="Calibrate key" description="Starts guided head-tracking calibration; press again to exit without changes."]
VirtualKey S_CalibrateKey = VirtualKey::F10;

// F and J have the bumps on most keyboards, so they're easy to find with the headset on.
[Setting category="General" name="Calibration: feels natural key"]
VirtualKey S_CalibrationYesKey = VirtualKey::F;

[Setting category="General" name="Calibration: something's off key"]
VirtualKey S_CalibrationNoKey = VirtualKey::J;

[Setting category="General" name="Eye check key"]
VirtualKey S_EyeCheckKey = VirtualKey::F11;

[Setting category="General" name="Show UI in VR" description="Openplanet's overlay and the VR panel. When off, the headset shows only the game."]
bool S_ShowUiInVr = true;

[Setting category="General" name="Hide game HUD while driving in VR" description="Records, timer and the rest of the game's own HUD."]
bool S_HideGameHud = false;

[Setting category="General" name="Show/hide game HUD key"]
VirtualKey S_GameHudKey = VirtualKey::F6;

// Bumped when a release needs to reset saved settings (see MigrateSettings).
[Setting hidden]
int S_SettingsVersion = 0;

void MigrateSettings()
{
	if (S_SettingsVersion < 1) {
		// 0.1.0 test builds saved calibration results that were mixed with manual changes, and
		// defaulted to writing the camera in Update. Start from the measured values instead.
		S_ModelDegrees = true;
		S_ModelNegPitch = false;
		S_ModelNegYaw = false;
		S_ModelNegRoll = false;
		S_ModelNegPosX = true;
		S_ModelNegPosY = false;
		S_ModelNegPosZ = false;
		S_EyeCheck = false;
		S_WritePoint = WritePoint::InRenderEarly;
		S_SettingsVersion = 1;
		print("Settings reset to the calibrated defaults");
	}
	if (S_SettingsVersion < 2) {
		// 0.1.0 measured the car's "Left" axis as pointing left (it points right), which made
		// calibration pick wrong yaw/position signs, and was one frame early (eyes swapped).
		// Mirror X + invert roll worked around both; replace them with the real settings.
		S_FlipX = false;
		S_FlipY = false;
		S_FlipZ = true;
		S_ColumnBasis = false;
		S_ModelDegrees = true;
		S_ModelNegPitch = false;
		S_ModelNegYaw = false;
		S_ModelNegRoll = false;
		S_ModelNegPosX = false;
		S_ModelNegPosY = false;
		S_ModelNegPosZ = false;
		S_Latency = 1;
		S_EyeCheck = false;
		S_SettingsVersion = 2;
		print("Settings updated: frame latency 1, Mirror X and invert roll off");
	}
	if (S_SettingsVersion < 3) {
		// Logged samples showed roll is not inverted once the game's rotation order is used.
		S_ModelNegRoll = false;
		S_SettingsVersion = 3;
		print("Settings updated: invert roll off");
	}
	if (S_SettingsVersion < 4) {
		// Roll-pitch-yaw made a head tilt feel wrong once the head was turned; yaw-pitch-roll
		// matches that report. Calibration steps 7 and 8 confirm it.
		S_RotationOrder = 1;
		S_SettingsVersion = 4;
		print("Settings updated: rotation order yaw, pitch, roll");
	}
}

[Setting category="General" name="Turn off async rendering" description="Async rendering adds a frame of lag and breaks eye alternation."]
bool S_DisableAsyncRender = true;

[Setting category="General" name="Show status window"]
bool S_ShowStatus = true;

[Setting category="Comfort" name="World scale" description="Game units per real-world meter. Higher makes the world feel smaller." min=0.25 max=4]
float S_WorldScale = 1.0f;

// RenderEarly is guaranteed to run inside Openplanet's frame (and so in a known frame).
[Setting category="Calibration" name="Camera write point"]
WritePoint S_WritePoint = WritePoint::InRenderEarly;

[Setting category="Calibration" name="Camera field"]
CameraField S_CameraField = CameraField::CamLocation;

[Setting category="Calibration" name="Frame latency" description="Frames between writing the camera and the frame that shows it." min=0 max=3]
int S_Latency = 1;

[Setting category="Calibration" name="Eye check" description="Renders the right eye upside down. Only the right eye should look flipped."]
bool S_EyeCheck = false;

[Setting category="Calibration" name="FOV correction" description="Adjust until the world stays still when you turn your head." min=0.5 max=2]
float S_FovScale = 1.0f;

[Setting category="Calibration" name="Mirror X axis"]
bool S_FlipX = false;

[Setting category="Calibration" name="Mirror Y axis"]
bool S_FlipY = false;

[Setting category="Calibration" name="Mirror Z axis" description="OpenXR looks down -Z, the game down +Z."]
bool S_FlipZ = true;

[Setting category="Calibration" name="Camera axes are columns"]
bool S_ColumnBasis = false;

int AxisFlags()
{
	int flags = 0;
	if (S_FlipX) flags |= Native::AXIS_FLIP_X;
	if (S_FlipY) flags |= Native::AXIS_FLIP_Y;
	if (S_FlipZ) flags |= Native::AXIS_FLIP_Z;
	if (S_ColumnBasis) flags |= Native::AXIS_COLUMN_BASIS;
	return flags;
}

string WritePointName(WritePoint p)
{
	switch (p) {
		case WritePoint::InUpdate: return "Update";
		case WritePoint::InRenderEarly: return "RenderEarly";
		case WritePoint::InRender: return "Render";
	}
	return "?";
}

string CameraFieldName(CameraField f)
{
	switch (f) {
		case CameraField::CamLocation: return "Location";
		case CameraField::CamNextLocation: return "NextLocation";
	}
	return "?";
}
