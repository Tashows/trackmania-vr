// Hides the game's own HUD (records, timer, ...) while driving in VR. The game draws it flat into
// the frame, so in the headset it is stuck to your view and its edges are hard to see. It is only
// hidden while driving, so the pause menu and end-of-round screens stay usable.

bool g_gameHudHidden = false;  // true while we are the ones hiding it

CControlContainer@ GameHudRoot()
{
	auto playground = GetApp().CurrentPlayground;
	if (playground is null || playground.Interface is null) {
		return null;
	}
	return playground.Interface.InterfaceRoot;
}

void UpdateGameHud()
{
	bool hide = g_vrRunning && S_HideGameHud && IsDriving();
	auto root = GameHudRoot();
	if (root is null) {
		g_gameHudHidden = false;
		return;
	}
	if (hide) {
		if (root.IsVisible) {
			root.IsVisible = false;
			if (!g_gameHudHidden) {
				print("Game HUD hidden (game reports UI visible: " + (UI::IsGameUIVisible() ? "yes" : "no") + ")");
			}
		}
		g_gameHudHidden = true;
	} else {
		RestoreGameHud();
	}
}

void RestoreGameHud()
{
	if (!g_gameHudHidden) {
		return;
	}
	auto root = GameHudRoot();
	if (root !is null) {
		root.IsVisible = true;
	}
	g_gameHudHidden = false;
}
