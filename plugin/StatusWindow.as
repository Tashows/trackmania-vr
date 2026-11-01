// Live status and the calibration controls, so tuning doesn't need the settings menu.

uint64 g_rateTime = 0;
uint g_ratePresents = 0;
uint g_rateComposes = 0;
uint g_rateXrFrames = 0;
float g_presentsPerSec = 0;
float g_composesPerSec = 0;
float g_xrFramesPerSec = 0;

void UpdateRates()
{
	uint64 now = Time::Now;
	if (g_rateTime == 0) {
		g_rateTime = now;
		return;
	}
	if (now - g_rateTime < 1000) {
		return;
	}
	float seconds = float(now - g_rateTime) / 1000.0f;
	uint presents = Native::ReadUInt(Native::OFF_PRESENT_COUNT);
	uint composes = Native::ReadUInt(Native::OFF_COMPOSE_COUNT);
	uint xrFrames = Native::ReadUInt(Native::OFF_XR_FRAMES);
	g_presentsPerSec = float(presents - g_ratePresents) / seconds;
	g_composesPerSec = float(composes - g_rateComposes) / seconds;
	g_xrFramesPerSec = float(xrFrames - g_rateXrFrames) / seconds;
	g_ratePresents = presents;
	g_rateComposes = composes;
	g_rateXrFrames = xrFrames;
	g_rateTime = now;
}

string Flag(bool on, const string &in label)
{
	return (on ? "\\$0f0" : "\\$f44") + label + "\\$z";
}

void RenderStatusWindow()
{
	if (!S_ShowStatus) {
		return;
	}
	UI::SetNextWindowSize(460, 520, UI::Cond::FirstUseEver);
	if (UI::Begin("Trackmania VR", S_ShowStatus)) {
		if (!Native::IsReady()) {
			UI::Text("\\$f44Native DLL not loaded.");
			if (Native::g_loadError.Length > 0) {
				UI::TextWrapped(Native::g_loadError);
			}
			if (UI::Button("Retry")) {
				Native::Load();
			}
		} else {
			RenderStatus();
			UI::Separator();
			RenderCockpit();
			UI::Separator();
			if (UI::CollapsingHeader("Advanced")) {
				RenderAdvanced();
			}
		}
	}
	UI::End();
}

void RenderStatus()
{
	UpdateRates();
	uint status = Native::ReadUInt(Native::OFF_STATUS);
	UI::Text(Flag((status & Native::STATUS_HOOKS) != 0, "Present hook") + "   "
		+ Flag((status & Native::STATUS_XR_INSTANCE) != 0, "OpenXR") + "   "
		+ Flag((status & Native::STATUS_SESSION_RUNNING) != 0, "Session") + "   "
		+ Flag((status & Native::STATUS_STEREO) != 0, "Stereo"));

	UI::Text("Game frames: " + Text::Format("%.0f", g_presentsPerSec) + "/s   "
		+ "VR camera writes: " + Text::Format("%.0f", g_composesPerSec) + "/s");
	UI::Text("Headset frames: " + Text::Format("%.0f", g_xrFramesPerSec) + "/s   "
		+ "Wait: " + Text::Format("%.1f", Native::ReadFloat(Native::OFF_WAIT_FRAME_MS)) + " ms   "
		+ "Eye mismatches: " + Native::ReadUInt(Native::OFF_EYE_MISMATCH));

	int bbW = Native::ReadInt(Native::OFF_BACKBUFFER_W);
	int bbH = Native::ReadInt(Native::OFF_BACKBUFFER_H);
	int recW = Native::ReadInt(Native::OFF_RECOMMENDED_W);
	int recH = Native::ReadInt(Native::OFF_RECOMMENDED_H);
	UI::Text("Game resolution: " + bbW + "x" + bbH + "   Headset per eye: " + recW + "x" + recH);

	string lastError = Native::LastError();
	if (lastError.Length > 0) {
		UI::TextWrapped("\\$fa0Last error: " + lastError);
	}

	if (UI::Button(g_vrRunning ? "Stop VR" : "Start VR")) {
		if (g_vrRunning) {
			StopVr();
		} else {
			StartVr();
		}
	}
	UI::SameLine();
	if (UI::Button("Recenter")) {
		Native::Recenter();
	}
	UI::SameLine();
	S_StereoEnabled = UI::Checkbox("Stereo", S_StereoEnabled);
}

void RenderCockpit()
{
	if (g_model is null || !g_modelWritten) {
		UI::TextWrapped("\\$fa0Head tracking needs the cockpit camera: in a map, press 3 until you're inside the car.");
	} else {
		UI::Text("\\$0f0Head tracking active\\$z (cockpit camera)");
	}

	UI::Text("Camera relative to the car (asked for -> rendered):");
	for (int i = 0; i < 6; i++) {
		string unit = i < 3 ? " deg" : " m";
		string format = i < 3 ? "%.1f" : "%.3f";
		UI::Text("  " + CHANNEL_NAMES[i] + ": " + Text::Format(format, Native::RequestedRel(i)) + unit
			+ " -> " + Text::Format(format, Native::MeasuredRel(i)) + unit);
	}

	if (!g_calibrating) {
		if (UI::Button("Calibrate head tracking")) {
			StartCalibration();
		}
		if (g_calibrationResult.Length > 0) {
			UI::TextWrapped(g_calibrationResult);
		}
		return;
	}
	RenderCalibrationGuide();
	if (!CalibrationSettling()) {
		if (UI::Button("Feels natural")) {
			AnswerCalibration(true);
		}
		UI::SameLine();
		if (UI::Button("Something's off")) {
			AnswerCalibration(false);
		}
		UI::SameLine();
	}
	if (UI::Button("Exit without changes")) {
		CancelCalibration();
	}
}

void RenderAdvanced()
{
	S_EyeCheck = UI::Checkbox("Eye check (right eye upside down)", S_EyeCheck);
	S_StereoOnlyWhileDriving = UI::Checkbox("Stereo only while driving (menus/replays on the virtual screen)", S_StereoOnlyWhileDriving);
	S_ScreenDistance = UI::SliderFloat("Virtual screen distance (m)", S_ScreenDistance, 1.0f, 8.0f);
	S_ScreenWidth = UI::SliderFloat("Virtual screen width (m)", S_ScreenWidth, 1.0f, 10.0f);
	S_DisableHeadMotion = UI::Checkbox("Turn off the game's head motion", S_DisableHeadMotion);
	S_ModelDegrees = UI::Checkbox("Cockpit angles are degrees", S_ModelDegrees);
	S_ModelNegPitch = UI::Checkbox("Invert pitch", S_ModelNegPitch);
	UI::SameLine();
	S_ModelNegYaw = UI::Checkbox("Invert yaw", S_ModelNegYaw);
	UI::SameLine();
	S_ModelNegRoll = UI::Checkbox("Invert roll", S_ModelNegRoll);
	S_ModelNegPosX = UI::Checkbox("Invert X", S_ModelNegPosX);
	UI::SameLine();
	S_ModelNegPosY = UI::Checkbox("Invert Y", S_ModelNegPosY);
	UI::SameLine();
	S_ModelNegPosZ = UI::Checkbox("Invert Z", S_ModelNegPosZ);
	int order = Math::Clamp(S_RotationOrder, 0, 5);
	if (UI::BeginCombo("Rotation order", ROTATION_ORDER_NAMES[order])) {
		for (int i = 0; i < 6; i++) {
			if (UI::Selectable(ROTATION_ORDER_NAMES[i], i == order)) {
				S_RotationOrder = i;
			}
		}
		UI::EndCombo();
	}
	UI::Separator();
	S_Latency = UI::SliderInt("Frame latency", S_Latency, 0, 3);
	S_WorldScale = UI::SliderFloat("World scale", S_WorldScale, 0.25f, 4.0f);
}
