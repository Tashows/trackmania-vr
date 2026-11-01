bool g_asyncRenderChanged = false;
bool g_vrRunning = false;

void Main()
{
	if (Native::Load() && S_AutoStartVr) {
		StartVr();
	}
}

// Starting happens in Update() rather than from the UI callback that requests it, which runs in
// the middle of Openplanet's own frame rendering.
bool g_startRequested = false;

void StartVr()
{
	g_startRequested = true;
}

void StopVr()
{
	g_startRequested = false;
	g_vrRunning = false;
}

void OnEnabled()
{
	Native::Load();
}

void OnDisabled()
{
	RestoreAsyncRender();
	ForgetCockpitModel();
	RestoreGameHud();
	Native::Unload();
}

void OnDestroyed()
{
	RestoreAsyncRender();
	ForgetCockpitModel();
	RestoreGameHud();
	Native::Unload();
}

void Update(float dt)
{
	if (!Native::IsReady()) {
		return;
	}
	if (g_startRequested) {
		g_startRequested = false;
		g_vrRunning = Native::Start();
	}
	Native::SetXrEnabled(g_vrRunning);
	Native::SetUiInVr(S_ShowUiInVr);
	Native::SetScreen(S_ScreenDistance, S_ScreenWidth);
	if (!g_vrRunning) {
		RestoreAsyncRender();
		RestoreCockpitModel();
		RestoreGameHud();
		return;
	}
	UpdateAsyncRender();
	UpdateGameHud();
}

// RenderEarly runs inside Openplanet's frame, so the camera write lands in a known frame.
void RenderEarly()
{
	ApplyVrCamera();
}

void Render()
{
	RenderVrPanel();
}

void RenderInterface()
{
	RenderStatusWindow();
}

void RenderMenu()
{
	if (UI::MenuItem("\\$f80" + Icons::Eye + "\\$z Trackmania VR", "", S_ShowStatus)) {
		S_ShowStatus = !S_ShowStatus;
	}
}

UI::InputBlocking OnKeyPress(bool down, VirtualKey key)
{
	if (!down || !Native::IsReady()) {
		return UI::InputBlocking::DoNothing;
	}
	if (key == S_RecenterKey) {
		Native::Recenter();
		return UI::InputBlocking::Block;
	}
	if (key == S_ToggleStereoKey) {
		S_StereoEnabled = !S_StereoEnabled;
		return UI::InputBlocking::Block;
	}
	if (g_vrRunning && HandleVrKey(key)) {
		return UI::InputBlocking::Block;
	}
	return UI::InputBlocking::DoNothing;
}

// True while the player is actually driving: not in the pause menu, intro, end-of-round replay,
// podium and so on. Those are shown on the flat virtual screen instead.
bool IsDriving()
{
	auto app = GetApp();
	auto playground = app.CurrentPlayground;
	if (playground is null || playground.GameTerminals.Length == 0) {
		return false;
	}
	if (playground.GameTerminals[0].UISequence_Current != SGamePlaygroundUIConfig::EUISequence::Playing) {
		return false;
	}
	auto network = cast<CGameCtnNetwork>(app.Network);
	if (network !is null && network.PlaygroundClientScriptAPI !is null
		&& network.PlaygroundClientScriptAPI.IsInGameMenuDisplayed) {
		return false;
	}
	return true;
}

// The camera the game renders the 3D scene with (same search as Openplanet's Camera plugin).
CHmsCamera@ FindGameCamera()
{
	auto viewport = GetApp().Viewport;
	if (viewport is null) {
		return null;
	}
	for (int i = int(viewport.Cameras.Length) - 1; i >= 0; i--) {
		auto camera = viewport.Cameras[i];
		if (camera is null || camera.m_IsOverlay3d) {
			continue;
		}
		return camera;
	}
	return null;
}

void ApplyVrCamera()
{
	if (!Native::IsReady() || !g_vrRunning) {
		return;
	}
	Native::WriteConfig(S_WorldScale, S_Latency, S_EyeCheck);

	// Frames we leave alone are shown on the flat virtual screen (outside a map the cockpit
	// camera isn't active either).
	bool wantStereo = S_StereoEnabled && !(S_StereoOnlyWhileDriving && !IsDriving());
	if (!wantStereo) {
		RestoreCockpitModel();
		return;
	}
	ApplyVrCockpit();
}

void UpdateAsyncRender()
{
	auto viewport = cast<CVisionViewport>(GetApp().Viewport);
	if (viewport is null) {
		return;
	}
	if (S_DisableAsyncRender && viewport.AsyncRender) {
		viewport.AsyncRender = false;
		g_asyncRenderChanged = true;
	} else if (!S_DisableAsyncRender && g_asyncRenderChanged) {
		RestoreAsyncRender();
	}
}

void RestoreAsyncRender()
{
	if (!g_asyncRenderChanged) {
		return;
	}
	auto viewport = cast<CVisionViewport>(GetApp().Viewport);
	if (viewport !is null) {
		viewport.AsyncRender = true;
	}
	g_asyncRenderChanged = false;
}
