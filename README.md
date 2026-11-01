# Trackmania VR

Full 6DoF VR for Trackmania (2020): an Openplanet plugin plus a native OpenXR bridge DLL.

```
plugin/   Openplanet plugin (AngelScript): loads the DLL, writes the per-eye camera, status window
native/   TrackmaniaVR.dll (C++): Present hook, OpenXR session, frame submission
build.ps1 builds the DLL and installs everything into OpenplanetNext\Plugins\TrackmaniaVR
```

## How it works

1. The DLL patches DXGI's `Present`, so it sees every finished game frame and gets the game's
   own D3D11 device, which it hands to OpenXR (SteamVR).
2. **Alternate-eye rendering.** The game renders once per frame. Even frames render the left eye
   and odd frames the right eye. Each game frame, the plugin reads the game camera
   (`CHmsCamera.Location`) and asks the DLL for that eye's camera: head pose + eye offset on top
   of the game camera. It then writes that back along with a matching FOV.
3. At `Present`, the DLL copies the frame into that eye's OpenXR swapchain. Every eye image is
   submitted with the exact pose and FOV it was rendered with. The runtime's reprojection
   covers the one-frame offset between eyes.
4. Frames where the plugin did not set a VR camera (menus, loading) are shown on a flat virtual
   screen 2 m in front of you.

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
it before rendering. Calibration compares the camera we ask for with the one the game rendered;
don't flip the calibration checkboxes by hand while calibrating.

## Testing (in this order)

Logs to check after each step:
- `%USERPROFILE%\OpenplanetNext\Plugins\TrackmaniaVR\TrackmaniaVR.log`: native side, with a
  stats line every 5 seconds
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
| Nothing is upside down, and turning your head doesn't move the view | The game overwrites our camera before rendering | Try the other **Write point** values (Update → RenderEarly → Render), then **Camera field** = NextLocation |

Also note the **Camera kept by game** percentage. It tells me how the game treats the camera
field we write.

### 4. Head-tracking direction
With Eye check off, look left/right/up/down and lean around:
- View turns the wrong way left/right → toggle **Mirror X**.
- Up/down inverted → toggle **Mirror Y**.
- Leaning forward moves you backward → toggle **Mirror Z**.
- Looking around feels right when the car is level but goes wrong on slopes or walls → toggle
  **Column axes**.

### 5. Scale and FOV
- Turn your head quickly while looking at a fixed object. If the world "swims" (moves with or
  against your head), adjust **FOV correction** until it stays still.
- Car feels too small or too large → adjust **World scale**.

## Known limitations (v0.1)

- The game HUD is drawn identically into both eyes, so it looks as if it's at infinite
  distance. Hide it while driving if it bothers you.
- Openplanet overlays are not visible in the headset; use the monitor for the status window.
- VR Controllers aren't used. Drive with a keyboard, gamepad or wheel.
- If SteamVR closes, the session is dropped and retried every 3 seconds.
- Once loaded, the DLL stays in memory until the game exits; reloading the plugin only pauses
  and restarts it. This is because Openplanet's own Present hook may chain to ours. Rebuilding
  therefore requires closing the game.
- Comfort options (horizon lock, vignette) are not in yet.
