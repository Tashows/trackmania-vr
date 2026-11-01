// A small status panel drawn into the game frame near the center of the view, so it can be read
// in the headset. Driven by hotkeys (no mouse needed while wearing the headset).

const float VR_PANEL_FONT_SIZE = 22.0f;

void RenderVrPanel()
{
	if (!Native::IsReady() || !g_vrRunning || !S_ShowUiInVr) {
		return;
	}
	// Slightly below the center of the frame, which is the middle of the headset's view.
	float scale = UI::GetScale();
	UI::SetNextWindowPos(int(Display::GetWidth() * 0.5f / scale), int(Display::GetHeight() * 0.62f / scale),
		UI::Cond::Always, 0.5f, 0.5f);
	UI::SetNextWindowBgAlpha(0.75f);
	int flags = UI::WindowFlags::NoDecoration | UI::WindowFlags::AlwaysAutoResize | UI::WindowFlags::NoInputs
		| UI::WindowFlags::NoSavedSettings | UI::WindowFlags::NoFocusOnAppearing | UI::WindowFlags::NoDocking;
	UI::PushFontSize(VR_PANEL_FONT_SIZE);
	if (UI::Begin("Trackmania VR panel", flags)) {
		RenderVrPanelContents();
	}
	UI::End();
	UI::PopFontSize();
}

// VirtualKey values are Windows virtual-key codes: 0-9 and A-Z are their ASCII codes, F1-F24 are
// 0x70-0x87.
string KeyName(VirtualKey key)
{
	int code = int(key);
	if (code >= 0x70 && code <= 0x87) {
		return "F" + (code - 0x70 + 1);
	}
	if (code >= 0x41 && code <= 0x5A) {
		return "ABCDEFGHIJKLMNOPQRSTUVWXYZ".SubStr(code - 0x41, 1);
	}
	if (code >= 0x30 && code <= 0x39) {
		return "" + (code - 0x30);
	}
	return "key " + code;
}

string OnOff(bool on)
{
	return on ? "\\$0f0ON\\$z" : "\\$888OFF\\$z";
}

void RenderVrPanelContents()
{
	// While calibrating, show only what the calibration needs.
	if (g_calibrating) {
		RenderCalibrationGuide();
		return;
	}

	UI::Text("\\$f80Trackmania VR\\$z      " + KeyName(S_PanelKey) + ": hide all UI in VR");

	if (S_CameraMethod == CameraMethod::CockpitModel) {
		if (g_model !is null && g_modelWritten) {
			UI::Text("Head tracking: " + OnOff(true));
		} else {
			UI::Text("Head tracking: \\$fa0press 3 for the cockpit camera\\$z");
		}
	}
	UI::Text(KeyName(S_RecenterKey) + ": recenter    " + KeyName(S_ToggleStereoKey) + ": stereo " + OnOff(S_StereoEnabled)
		+ "    " + KeyName(S_EyeCheckKey) + ": eye check " + OnOff(S_EyeCheck));
	UI::Text(KeyName(S_GameHudKey) + ": game HUD while driving " + OnOff(!S_HideGameHud));
	if (S_EyeCheck) {
		UI::Text("\\$fa0Eye check: only your RIGHT eye should see the world upside down.\\$z");
	}

	UI::Separator();
	UI::Text(KeyName(S_CalibrateKey) + ": calibrate head tracking (guided)");
	if (g_calibrationResult.Length > 0) {
		UI::Text("Last calibration: " + g_calibrationResult);
	}

	RenderResolutionInfo();
}

// The game window's shape decides how much of each rendered frame the headset can use. Everything
// follows the current resolution live; this only suggests the least wasteful shape.
void RenderResolutionInfo()
{
	float headsetH = Native::ReadFloat(Native::OFF_HEADSET_FOV_H);
	float headsetV = Native::ReadFloat(Native::OFF_HEADSET_FOV_V);
	float renderedH = Native::ReadFloat(Native::OFF_RENDERED_FOV_H);
	float renderedV = Native::ReadFloat(Native::OFF_RENDERED_FOV_V);
	int width = Native::ReadInt(Native::OFF_BACKBUFFER_W);
	int height = Native::ReadInt(Native::OFF_BACKBUFFER_H);
	if (headsetH <= 0 || width <= 0 || height <= 0) {
		return;
	}
	UI::Separator();
	float idealAspect = Math::Tan(Math::ToRad(headsetH) / 2) / Math::Tan(Math::ToRad(headsetV) / 2);
	float aspect = float(width) / float(height);
	UI::Text("Game " + width + "x" + height + " (" + Text::Format("%.2f", aspect) + ":1), rendering "
		+ Text::Format("%.0f", renderedH) + " x " + Text::Format("%.0f", renderedV) + " deg");
	string advice = "Headset sees " + Text::Format("%.0f", headsetH) + " x " + Text::Format("%.0f", headsetV)
		+ " deg; least waste at " + Text::Format("%.2f", idealAspect) + ":1 (e.g. "
		+ int(height * idealAspect) + "x" + height + ")";
	UI::Text(Math::Abs(aspect - idealAspect) < 0.1f ? "\\$0f0" + advice + "\\$z" : advice);
}

// Hotkeys usable while wearing the headset. Returns true when the key was handled.
bool HandleVrKey(VirtualKey key)
{
	if (HandleCalibrationKey(key)) {
		return true;
	}
	if (key == S_PanelKey) {
		S_ShowUiInVr = !S_ShowUiInVr;
		return true;
	}
	if (key == S_GameHudKey) {
		S_HideGameHud = !S_HideGameHud;
		return true;
	}
	if (key == S_EyeCheckKey) {
		S_EyeCheck = !S_EyeCheck;
		return true;
	}
	if (key == S_CalibrateKey) {
		StartCalibration();
		return true;
	}
	return false;
}
