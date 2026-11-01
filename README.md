# Trackmania VR

Full 6DoF VR for Trackmania (2020): an Openplanet plugin plus a native OpenXR bridge DLL.

> **AI disclosure:** this plugin is mostly AI-generated (written with Claude by Anthropic). A human
> guided the design, made the decisions and tested every change in VR.

```
plugin/   Openplanet plugin (AngelScript): loads the DLL, writes the per-eye camera, status window
native/   TrackmaniaVR.dll (C++): Present hook, OpenXR session, frame submission
build.ps1 builds the DLL and installs everything into OpenplanetNext\Plugins\TrackmaniaVR
```

## How to install as a player

1) [Install Openplanet](https://openplanet.dev/docs/tutorials/installation)
2) Download the latest release's .op file (latest is [TrackmaniavVR-v0.1.1.op](https://github.com/Tashows/trackmania-vr/releases/download/v0.1.1/TrackmaniaVR-0.1.1.op))
3) Place it in %USERPROFILE%\OpenplanetNext\Plugins (replace %USERPROFILE% with your windows local user name)
4) Start SteamVR, then launch Trackmania
5) Once the game launches, press F3 to view the openplanet menu and select signature mode -> Developer. This should automatically enable the plugin. This plugin is not reviewed/verified by Openplanet and thus has restricted capabilities: no leaderboard records, and online play will be blocked on most maps.

## How it works

1. The DLL patches DXGI's `Present`, so it sees every finished game frame and gets the game's
   own D3D11 device, which it hands to OpenXR (SteamVR).
2. **Alternate-eye rendering.** The game renders once per frame. Even frames render the left eye
   and odd frames the right eye. Each game frame, the plugin asks the DLL for that eye's head
   pose and writes it into the cockpit camera's model (`CPlugVehicleCameraInternalModel`) along
   with a matching FOV. The game builds the camera from it before rendering.
3. At `Present`, the DLL copies the frame into that eye's OpenXR swapchain. Every eye image is
   submitted with the exact pose and FOV it was rendered with. The runtime's reprojection
   covers the one-frame offset between eyes.
4. Frames where the plugin did not set a VR camera (menus, replays, loading) are shown on a flat
   virtual screen fixed in the world, 3 m in front of you by default.

The game has to render 2× the headset refresh rate: 160 fps for an 80 Hz headset. SteamVR's
motion smoothing covers you below that.

## One-time setup

**Build tools** (only needed to compile the DLL):

```bash
winget install --id Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

**Openplanet:** switch to **Developer** signature mode (Openplanet menu → Signature Mode).
Unsigned plugins and native DLLs only load there. Developer mode also turns on School Mode:
no leaderboard records, and online play is blocked on most maps. Use local/offline maps for
VR testing.

**SteamVR:** start with 80 Hz refresh rate and motion smoothing on.

**Trackmania graphics settings for VR:**

| Setting | Value | Why |
|---|---|---|
| Display mode | Windowed | Lets you pick a resolution with the headset's aspect ratio |
| Resolution | close to the "Headset per eye" size in the status window, or at least the same aspect (Index ≈ 0.9 : 1, e.g. 1440×1600) | Each game frame becomes one eye image; a 16:9 window wastes the top and bottom of your view |
| VSync / frame limit | Off / unlimited | The DLL paces frames to the headset |
| Anti-aliasing | **not** TAA/temporal | Temporal effects blend left and right eye frames → ghosting |
| Motion blur | Off | Same reason |
| Camera | Cockpit / first person (camera 3) | The other cameras just show a flat screen in the VR. |

## Build and install

```bash
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

`-Package` instead creates `build\package\TrackmaniaVR-<version>.op` for a release. It never
replaces an existing version's package (pass `-Overwrite` to rebuild one deliberately).

Close Trackmania first: Windows locks the DLL while the plugin has it loaded.

## In-headset controls

| Key | Action |
|---|---|
| F6 | Hide/show the game's own HUD (records, timer) while driving |
| F7 | Show/hide all UI in VR (Openplanet overlay + VR panel with status, calibration, resolution) |
| F8 | Recenter |
| F9 | Stereo on/off (off = flat virtual screen) |
| F10 | Start guided calibration; during calibration, exit without changes |
| F / J | During calibration: the movement feels natural / something's off (flips that axis and asks again) |
| F11 | Eye check: only the right eye should be upside down. If it's not you have to offset frames by 1 in plugin's advanced settings (frame latency) |

Stereo is only used while you're driving. The pause menu, intro, end-of-round replay and podium
are shown on a flat virtual screen fixed in the world (distance and size under Advanced).

The Openplanet overlay (F3) is also visible in the headset.

Head tracking uses the cockpit camera (press 3 in a map). It writes the head pose into the
cockpit camera's model (`CPlugVehicleCameraInternalModel`), and the game builds the camera from
it before rendering. Don't change the calibration checkboxes by hand while the guided
calibration is running.

## Testing (in this order)

Logs to check after each step:
- `TrackmaniaVR.log`: native side, with a stats line every 5 seconds. It's next to
  `TrackmaniaVR.dll`: in `%USERPROFILE%\OpenplanetNext\Plugins\TrackmaniaVR\` for an unpacked
  plugin, or `%USERPROFILE%\OpenplanetNext\PluginStorage\<plugin>\bin\<version>\` for an `.op`
  (the plugin copies its binaries there, since Windows can't load a DLL from inside a zip)
- `%USERPROFILE%\OpenplanetNext\Openplanet.log`: plugin side, including script errors

### 1. Plugin and DLL load
Start SteamVR, then Trackmania. Press F3 for the Openplanet overlay; the **Trackmania VR** status
window should show a green **Present hook** and "Game frames" above 0/s. VR does not start on
its own: press **Start VR**. Then **OpenXR** and **Session** should turn green.
- "Native DLL not loaded" → the reason is printed under it (and in Openplanet.log).
- Present hook green but "Game frames: 0/s" → the hook doesn't reach the game's swapchain.
  That's a native fix on my side; send me the log.
- If the game crashes, the DLL writes the crashing call stack into TrackmaniaVR.log.

### 2. Flat screen in the headset
In the main menu, the headset should show the game on a floating screen. Press F8 to recenter it.
This proves the D3D11 → OpenXR path: device sharing, swapchain format, frame pacing.
Colors washed out or too dark → tell me; it's a swapchain format issue.

### 3. Stereo + calibration (the important one)
Load a map offline, switch to the cockpit camera, and turn on **Eye check** in the status
window. This renders the right eye upside down. Close one eye at a time:

| What you see | Meaning | Do this |
|---|---|---|
| Only the **right** eye is upside down | Timing is correct | Turn Eye check off and continue to step 4 |
| Only the **left** eye is upside down | Off by one frame | Change **Frame latency** (0 ↔ 1) |
| Both eyes flicker between upside down and normal | Off by a frame, and the game is running slower than the headset | Change Frame latency |
| Nothing is upside down, and turning your head doesn't move the view | You're not in the cockpit camera | Press 3 until you're inside the car |

### 4. Head-tracking direction
With Eye check off, press **F10** for the guided calibration. It asks you to make one movement at
a time; answer **F** if it feels natural or **J** if something's off (that axis is flipped and
you're asked again). F10 again exits without changes.

### 5. Scale
Car feels too small or too large → adjust **World scale** (Advanced).

## Known limitations (v0.1)

- The game HUD is drawn identically into both eyes, so it looks as if it's at infinite
  distance. Hide it while driving (F6) if it bothers you.
- VR Controllers aren't used. Drive with a keyboard, gamepad or wheel.
- If SteamVR closes, the session is dropped and retried every 3 seconds.
- Once loaded, the DLL stays in memory until the game exits; reloading the plugin only pauses
  and restarts it. This is because Openplanet's own Present hook may chain to ours. Rebuilding
  therefore requires closing the game.
- Comfort options (horizon lock, vignette) are not in yet.

## License

MIT (see [LICENSE](LICENSE)). The bundled OpenXR loader and other third-party components are
listed in [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).
