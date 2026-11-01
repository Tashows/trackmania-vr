// Bindings for TrackmaniaVR.dll. All data goes through the native SharedBlock; the byte offsets
// here must match native/src/shared_block.h.
namespace Native
{
	const uint SHARED_VERSION = 7;

	const uint OFF_VERSION = 0;
	const uint OFF_STATUS = 4;
	const uint OFF_CAMERA_LOC = 8;
	const uint OFF_WORLD_SCALE = 56;
	const uint OFF_LATENCY = 60;
	const uint OFF_EYE_CHECK = 64;
	const uint OFF_FOV_HORIZONTAL = 68;
	const uint OFF_XR_ENABLED = 72;
	const uint OFF_CAPTURE_AFTER_OVERLAY = 76;
	const uint OFF_OUT_FOV = 80;
	const uint OFF_OUT_EYE = 84;
	const uint OFF_PRESENT_COUNT = 88;
	const uint OFF_COMPOSE_COUNT = 92;
	const uint OFF_XR_FRAMES = 96;
	const uint OFF_BACKBUFFER_W = 100;
	const uint OFF_BACKBUFFER_H = 104;
	const uint OFF_RECOMMENDED_W = 108;
	const uint OFF_RECOMMENDED_H = 112;
	const uint OFF_WAIT_FRAME_MS = 116;
	const uint OFF_EYE_MISMATCH = 120;
	const uint OFF_LAST_ERROR = 124;
	const uint OFF_MODEL_BASE_PYR = 380;
	const uint OFF_MODEL_BASE_POS = 392;
	const uint OFF_MODEL_FLAGS = 404;
	const uint OFF_OUT_PYR = 408;
	const uint OFF_OUT_POS = 420;
	const uint OFF_CAR_LEFT = 432;
	const uint OFF_CAR_UP = 444;
	const uint OFF_CAR_DIR = 456;
	const uint OFF_CAR_POS = 468;
	const uint OFF_REQUESTED_REL = 480;
	const uint OFF_MEASURED_REL = 504;
	const uint OFF_CALIB_GAIN = 528;
	const uint OFF_CALIB_SAMPLES = 552;
	const uint OFF_CAM_FOV = 576;
	const uint OFF_CAM_ASPECT = 580;
	const uint OFF_CAM_FOV_AXIS = 584;
	const uint OFF_REQUESTED_FOV_H = 588;
	const uint OFF_REQUESTED_FOV_V = 592;
	const uint OFF_RENDERED_FOV_H = 596;
	const uint OFF_RENDERED_FOV_V = 600;
	const uint OFF_HEADSET_FOV_H = 604;
	const uint OFF_HEADSET_FOV_V = 608;
	const uint OFF_SCREEN_DISTANCE = 612;
	const uint OFF_SCREEN_WIDTH = 616;

	const uint STATUS_HOOKS = 1;
	const uint STATUS_XR_INSTANCE = 2;
	const uint STATUS_SESSION_RUNNING = 4;
	const uint STATUS_STEREO = 8;

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
		@g_fnComposeModel = g_lib.GetFunction("TMVR_ComposeModel");
		@g_fnMeasure = g_lib.GetFunction("TMVR_Measure");
		@g_fnCalibrationReset = g_lib.GetFunction("TMVR_CalibrationReset");
		@g_fnRecenter = g_lib.GetFunction("TMVR_Recenter");
		@g_fnShutdown = g_lib.GetFunction("TMVR_Shutdown");
		if (fnGetShared is null || g_fnInit is null || g_fnStart is null || g_fnComposeModel is null
			|| g_fnMeasure is null || g_fnCalibrationReset is null || g_fnRecenter is null
			|| g_fnShutdown is null) {
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

	void WriteConfig(float worldScale, int latency, bool eyeCheck)
	{
		int eyeCheckValue = eyeCheck ? 1 : 0;
		Dev::Write(g_shared + OFF_WORLD_SCALE, worldScale);
		Dev::Write(g_shared + OFF_LATENCY, latency);
		Dev::Write(g_shared + OFF_EYE_CHECK, eyeCheckValue);
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
		Dev::Write(g_shared + OFF_CAMERA_LOC, camera);
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
