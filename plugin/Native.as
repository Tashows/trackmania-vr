// Bindings for TrackmaniaVR.dll. All data goes through the native SharedBlock; the byte offsets
// here must match native/src/shared_block.h.
namespace Native
{
	const uint SHARED_VERSION = 6;

	const uint OFF_VERSION = 0;
	const uint OFF_STATUS = 4;
	const uint OFF_BASE_LOC = 8;
	const uint OFF_WORLD_SCALE = 56;
	const uint OFF_LATENCY = 60;
	const uint OFF_EYE_CHECK = 64;
	const uint OFF_AXIS_FLAGS = 68;
	const uint OFF_FOV_SCALE = 72;
	const uint OFF_FOV_HORIZONTAL = 76;
	const uint OFF_XR_ENABLED = 80;
	const uint OFF_CAPTURE_AFTER_OVERLAY = 84;
	const uint OFF_OUT_LOC = 88;
	const uint OFF_OUT_FOV = 136;
	const uint OFF_OUT_EYE = 140;
	const uint OFF_PRESENT_COUNT = 144;
	const uint OFF_COMPOSE_COUNT = 148;
	const uint OFF_BASE_REUSED = 152;
	const uint OFF_XR_FRAMES = 156;
	const uint OFF_BACKBUFFER_W = 160;
	const uint OFF_BACKBUFFER_H = 164;
	const uint OFF_RECOMMENDED_W = 168;
	const uint OFF_RECOMMENDED_H = 172;
	const uint OFF_WAIT_FRAME_MS = 176;
	const uint OFF_EYE_MISMATCH = 180;
	const uint OFF_LAST_ERROR = 184;
	const uint OFF_MODEL_BASE_PYR = 440;
	const uint OFF_MODEL_BASE_POS = 452;
	const uint OFF_MODEL_FLAGS = 464;
	const uint OFF_OUT_PYR = 468;
	const uint OFF_OUT_POS = 480;
	const uint OFF_CAR_LEFT = 492;
	const uint OFF_CAR_UP = 504;
	const uint OFF_CAR_DIR = 516;
	const uint OFF_CAR_POS = 528;
	const uint OFF_REQUESTED_REL = 540;
	const uint OFF_MEASURED_REL = 564;
	const uint OFF_CALIB_GAIN = 588;
	const uint OFF_CALIB_SAMPLES = 612;
	const uint OFF_CAM_FOV = 636;
	const uint OFF_CAM_ASPECT = 640;
	const uint OFF_CAM_FOV_AXIS = 644;
	const uint OFF_REQUESTED_FOV_H = 648;
	const uint OFF_REQUESTED_FOV_V = 652;
	const uint OFF_RENDERED_FOV_H = 656;
	const uint OFF_RENDERED_FOV_V = 660;
	const uint OFF_HEADSET_FOV_H = 664;
	const uint OFF_HEADSET_FOV_V = 668;
	const uint OFF_SCREEN_DISTANCE = 672;
	const uint OFF_SCREEN_WIDTH = 676;

	const uint STATUS_HOOKS = 1;
	const uint STATUS_XR_INSTANCE = 2;
	const uint STATUS_SESSION_RUNNING = 4;
	const uint STATUS_STEREO = 8;

	const int AXIS_FLIP_X = 1;
	const int AXIS_FLIP_Y = 2;
	const int AXIS_FLIP_Z = 4;
	const int AXIS_COLUMN_BASIS = 8;

	const int MODEL_DEGREES = 1;
	const int MODEL_NEGATE_PITCH = 2;
	const int MODEL_NEGATE_YAW = 4;
	const int MODEL_NEGATE_ROLL = 8;
	const int MODEL_NEGATE_POS_X = 16;
	const int MODEL_NEGATE_POS_Y = 32;
	const int MODEL_NEGATE_POS_Z = 64;
	const int MODEL_ORDER_SHIFT = 8;

	Import::Library@ g_lib;
	Import::Function@ g_fnInit;
	Import::Function@ g_fnStart;
	Import::Function@ g_fnCompose;
	Import::Function@ g_fnComposeModel;
	Import::Function@ g_fnMeasure;
	Import::Function@ g_fnCalibrationReset;
	Import::Function@ g_fnRecenter;
	Import::Function@ g_fnShutdown;
	uint64 g_shared = 0;
	string g_loadError;

	bool IsReady()
	{
		return g_shared != 0;
	}

	bool Load()
	{
		if (g_shared != 0) {
			return true;
		}
		g_loadError = "";

		string dir = Meta::ExecutingPlugin().SourcePath;
		if (!dir.EndsWith("/") && !dir.EndsWith("\\")) {
			dir += "/";
		}
		@g_lib = Import::GetLibrary(dir + "TrackmaniaVR.dll");
		if (g_lib is null) {
			@g_lib = Import::GetLibrary("TrackmaniaVR.dll");
		}
		if (g_lib is null) {
			return Fail("Could not load TrackmaniaVR.dll from " + dir);
		}

		auto fnGetShared = g_lib.GetFunction("TMVR_GetShared");
		@g_fnInit = g_lib.GetFunction("TMVR_Init");
		@g_fnStart = g_lib.GetFunction("TMVR_Start");
		@g_fnCompose = g_lib.GetFunction("TMVR_Compose");
		@g_fnComposeModel = g_lib.GetFunction("TMVR_ComposeModel");
		@g_fnMeasure = g_lib.GetFunction("TMVR_Measure");
		@g_fnCalibrationReset = g_lib.GetFunction("TMVR_CalibrationReset");
		@g_fnRecenter = g_lib.GetFunction("TMVR_Recenter");
		@g_fnShutdown = g_lib.GetFunction("TMVR_Shutdown");
		if (fnGetShared is null || g_fnInit is null || g_fnStart is null || g_fnCompose is null
			|| g_fnComposeModel is null || g_fnMeasure is null || g_fnCalibrationReset is null
			|| g_fnRecenter is null || g_fnShutdown is null) {
			return Fail("TrackmaniaVR.dll is missing exports (old build?)");
		}

		uint64 shared = fnGetShared.CallPointer();
		uint version = Dev::ReadUInt32(shared + OFF_VERSION);
		if (version != SHARED_VERSION) {
			return Fail("TrackmaniaVR.dll version " + version + " does not match the plugin (" + SHARED_VERSION + ")");
		}
		if (g_fnInit.CallInt32() == 0) {
			string nativeError = Dev::ReadCString(shared + OFF_LAST_ERROR);
			return Fail("Native init failed: " + nativeError);
		}
		g_shared = shared;
		print("TrackmaniaVR.dll loaded");
		return true;
	}

	bool Fail(const string &in message)
	{
		g_loadError = message;
		error(message);
		Unload();
		return false;
	}

	void Unload()
	{
		if (g_fnShutdown !is null) {
			g_fnShutdown.Call();
		}
		g_shared = 0;
		@g_fnInit = null;
		@g_fnStart = null;
		@g_fnCompose = null;
		@g_fnComposeModel = null;
		@g_fnMeasure = null;
		@g_fnCalibrationReset = null;
		@g_fnRecenter = null;
		@g_fnShutdown = null;
		@g_lib = null;
	}

	// Installs the frame hook in the game. Nothing in the game is touched before this.
	bool Start()
	{
		if (g_fnStart.CallInt32() == 0) {
			error("Could not start VR: " + LastError());
			return false;
		}
		return true;
	}

	void SetXrEnabled(bool enabled)
	{
		int value = enabled ? 1 : 0;
		Dev::Write(g_shared + OFF_XR_ENABLED, value);
	}

	// The flat virtual screen used for menus and replays.
	void SetScreen(float distance, float width)
	{
		Dev::Write(g_shared + OFF_SCREEN_DISTANCE, distance);
		Dev::Write(g_shared + OFF_SCREEN_WIDTH, width);
	}

	// true: the headset sees Openplanet's overlay and the VR panel; false: the game only.
	void SetUiInVr(bool visible)
	{
		int value = visible ? 1 : 0;
		Dev::Write(g_shared + OFF_CAPTURE_AFTER_OVERLAY, value);
	}

	void WriteConfig(float worldScale, int latency, bool eyeCheck, int axisFlags, float fovScale)
	{
		int eyeCheckValue = eyeCheck ? 1 : 0;
		Dev::Write(g_shared + OFF_WORLD_SCALE, worldScale);
		Dev::Write(g_shared + OFF_LATENCY, latency);
		Dev::Write(g_shared + OFF_EYE_CHECK, eyeCheckValue);
		Dev::Write(g_shared + OFF_AXIS_FLAGS, axisFlags);
		Dev::Write(g_shared + OFF_FOV_SCALE, fovScale);
	}

	// Asks the native side for this frame's eye camera, built on top of the game's camera.
	bool Compose(const iso4 &in base, bool fovIsHorizontal, iso4 &out outLoc, float &out outFov)
	{
		int fovHorizontalValue = fovIsHorizontal ? 1 : 0;
		Dev::Write(g_shared + OFF_FOV_HORIZONTAL, fovHorizontalValue);
		Dev::Write(g_shared + OFF_BASE_LOC, base);
		if (g_fnCompose.CallInt32() == 0) {
			return false;
		}
		outLoc = Dev::ReadIso4(g_shared + OFF_OUT_LOC);
		outFov = Dev::ReadFloat(g_shared + OFF_OUT_FOV);
		return true;
	}

	// Asks the native side for this frame's eye, as cockpit camera model fields built on top of
	// the model's original seat values.
	// camera: the game camera as rendered this frame, so the headset is told its real FOV.
	bool ComposeModel(const vec3 &in basePyr, const vec3 &in basePos, int modelFlags, CHmsCamera@ camera,
		vec3 &out outPyr, vec3 &out outPos, float &out outFov)
	{
		int fovAxis = camera is null ? 0 : int(camera.m_ViewportRatio);
		int fovHorizontalValue = fovAxis == 2 ? 1 : 0;
		float cameraFov = camera is null ? 0.0f : camera.Fov;
		float cameraAspect = camera is null ? 0.0f : camera.Width_Height;
		Dev::Write(g_shared + OFF_FOV_HORIZONTAL, fovHorizontalValue);
		Dev::Write(g_shared + OFF_CAM_FOV, cameraFov);
		Dev::Write(g_shared + OFF_CAM_ASPECT, cameraAspect);
		Dev::Write(g_shared + OFF_CAM_FOV_AXIS, fovAxis);
		Dev::Write(g_shared + OFF_MODEL_BASE_PYR, basePyr);
		Dev::Write(g_shared + OFF_MODEL_BASE_POS, basePos);
		Dev::Write(g_shared + OFF_MODEL_FLAGS, modelFlags);
		if (g_fnComposeModel.CallInt32() == 0) {
			return false;
		}
		outPyr = Dev::ReadVec3(g_shared + OFF_OUT_PYR);
		outPos = Dev::ReadVec3(g_shared + OFF_OUT_POS);
		outFov = Dev::ReadFloat(g_shared + OFF_OUT_FOV);
		return true;
	}

	// Compares the camera the game rendered with what the last compose asked for.
	void Measure(const iso4 &in camera, CSceneVehicleVisState@ car)
	{
		Dev::Write(g_shared + OFF_BASE_LOC, camera);
		Dev::Write(g_shared + OFF_CAR_LEFT, car.Left);
		Dev::Write(g_shared + OFF_CAR_UP, car.Up);
		Dev::Write(g_shared + OFF_CAR_DIR, car.Dir);
		Dev::Write(g_shared + OFF_CAR_POS, car.Position);
		g_fnMeasure.Call();
	}

	void CalibrationReset()
	{
		g_fnCalibrationReset.Call();
	}

	float CalibrationGain(int channel) { return ReadFloat(OFF_CALIB_GAIN + 4 * channel); }
	int CalibrationSamples(int channel) { return ReadInt(OFF_CALIB_SAMPLES + 4 * channel); }
	float RequestedRel(int channel) { return ReadFloat(OFF_REQUESTED_REL + 4 * channel); }
	float MeasuredRel(int channel) { return ReadFloat(OFF_MEASURED_REL + 4 * channel); }

	void Recenter()
	{
		if (g_fnRecenter !is null) {
			g_fnRecenter.Call();
		}
	}

	uint ReadUInt(uint offset) { return Dev::ReadUInt32(g_shared + offset); }
	int ReadInt(uint offset) { return Dev::ReadInt32(g_shared + offset); }
	float ReadFloat(uint offset) { return Dev::ReadFloat(g_shared + offset); }
	string LastError() { return Dev::ReadCString(g_shared + OFF_LAST_ERROR); }
}
