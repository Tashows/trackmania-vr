// Drives the cockpit camera (camera 3) through its model, CPlugVehicleCameraInternalModel.
// The game computes the cockpit camera from the model's seat offset and angles during its own
// update, before rendering, so writing the head pose there reaches the rendered frame.

// The game's camera manager is right after App.GameScene; its current camera model is at 0x70.
// Same layout as XertroV's Editor++ DGameCamera (0x70 CurrentCam, 0x80 CurrentCamCtrl).
const uint GAME_CAMERA_AFTER_SCENE = 0x10;
const uint GAME_CAMERA_CURRENT_MODEL = 0x70;

int g_gameSceneOffset = -1;

CPlugVehicleCameraInternalModel@ g_model;
vec3 g_modelPyr;
vec3 g_modelPos;
float g_modelFov;
float g_modelHeadCoef;
bool g_modelWritten = false;

bool PointerLooksBad(uint64 ptr)
{
	return ptr < 0x10000000000 || ptr > 0x7FFFFFFFFFFF || (ptr & 0x7) != 0;
}

CPlugVehicleCameraInternalModel@ FindCockpitModel()
{
	auto app = GetApp();
	if (app.GameScene is null || app.CurrentPlayground is null) {
		return null;
	}
	if (g_gameSceneOffset < 0) {
		g_gameSceneOffset = Reflection::GetType("CGameManiaPlanet").GetMember("GameScene").Offset;
	}
	uint cameraOffset = uint(g_gameSceneOffset) + GAME_CAMERA_AFTER_SCENE;
	if (PointerLooksBad(Dev::GetOffsetUint64(app, cameraOffset))) {
		return null;
	}
	auto gameCamera = Dev::GetOffsetNod(app, cameraOffset);
	if (gameCamera is null || PointerLooksBad(Dev::GetOffsetUint64(gameCamera, GAME_CAMERA_CURRENT_MODEL))) {
		return null;
	}
	// Null unless the cockpit camera is active (the chase cameras use other model classes).
	return cast<CPlugVehicleCameraInternalModel>(Dev::GetOffsetNod(gameCamera, GAME_CAMERA_CURRENT_MODEL));
}

// Remembers the model's own values the first time we see it, so we can build on and restore them.
void TrackModel(CPlugVehicleCameraInternalModel@ model)
{
	if (model is g_model) {
		return;
	}
	RestoreCockpitModel();
	@g_model = model;
	g_modelPyr = model.PitchYawRoll;
	g_modelPos = model.RelativePos;
	g_modelFov = model.Fov;
	g_modelHeadCoef = model.PilotHeadCoef;
	print("Cockpit camera model: PitchYawRoll " + Vec3Text(g_modelPyr) + ", RelativePos " + Vec3Text(g_modelPos)
		+ ", Fov " + Text::Format("%.1f", g_modelFov) + ", PilotHeadCoef " + Text::Format("%.3f", g_modelHeadCoef)
		+ ", Hmd_Model " + model.Hmd_Model + ", Hmd_NoPitchRoll " + (model.Hmd_NoPitchRoll ? "true" : "false"));
}

string Vec3Text(const vec3 &in v)
{
	return Text::Format("%.3f", v.x) + ", " + Text::Format("%.3f", v.y) + ", " + Text::Format("%.3f", v.z);
}

void RestoreCockpitModel()
{
	if (g_model is null || !g_modelWritten) {
		return;
	}
	g_model.PitchYawRoll = g_modelPyr;
	g_model.RelativePos = g_modelPos;
	g_model.Fov = g_modelFov;
	g_model.PilotHeadCoef = g_modelHeadCoef;
	g_modelWritten = false;
}

void ForgetCockpitModel()
{
	RestoreCockpitModel();
	@g_model = null;
}

// Returns false when the cockpit camera isn't active.
bool ApplyVrCockpit()
{
	auto model = FindCockpitModel();
	if (model is null) {
		RestoreCockpitModel();
		return false;
	}
	TrackModel(model);

	auto camera = FindGameCamera();
	auto car = VehicleState::ViewingPlayerState();
	if (camera !is null && car !is null) {
		Native::Measure(camera.Location, car);
	}
	UpdateCalibration();

	vec3 pyr, pos;
	float fov;
	if (!Native::ComposeModel(g_modelPyr, g_modelPos, ModelFlags(), camera, pyr, pos, fov)) {
		RestoreCockpitModel();
		return true;
	}
	model.PitchYawRoll = pyr;
	model.RelativePos = pos;
	model.Fov = fov;
	model.PilotHeadCoef = S_DisableHeadMotion ? 0.0f : g_modelHeadCoef;
	g_modelWritten = true;
	return true;
}
