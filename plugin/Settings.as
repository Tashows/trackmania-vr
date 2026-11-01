[Setting category="Comfort" name="Turn off the game's head motion" description="The cockpit camera's simulated head movement (PilotHeadCoef)."]
bool S_DisableHeadMotion = true;

[Setting category="Comfort" name="World scale" description="Game units per real-world meter. Higher makes the world feel smaller." min=0.25 max=4]
float S_WorldScale = 1.0f;

// How the game interprets the cockpit model's PitchYawRoll / RelativePos. Defaults are what the
// guided calibration settled on in VR: angles in degrees, nothing inverted.
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

[Setting category="Calibration" name="Frame latency" description="Frames between writing the camera and the frame that shows it." min=0 max=3]
int S_Latency = 1;

[Setting category="Calibration" name="Eye check" description="Renders the right eye upside down. Only the right eye should look flipped."]
bool S_EyeCheck = false;

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

[Setting category="General" name="Turn off async rendering" description="Async rendering adds a frame of lag and breaks eye alternation."]
bool S_DisableAsyncRender = true;

[Setting category="General" name="Show status window"]
bool S_ShowStatus = true;
