// Guided head-tracking calibration. For each movement the player answers whether it feels
// natural; "something's off" flips that axis and asks again. The player's eyes decide: the
// camera measurement depends on assumptions about the game's axes, so it only goes to the log.

bool g_calibrating = false;
string g_calibrationResult;

const array<string> CHANNEL_NAMES = { "Pitch", "Yaw", "Roll", "Left/right", "Up/down", "Forward/back" };

// Long instructions are split over two lines: the panel moves with your head, so a wide one is
// hard to read end to end.
const array<string> CALIBRATION_STEPS = {
	"Turn your head LEFT and RIGHT.",
	"Look UP and DOWN.",
	"Tilt your head toward each SHOULDER.",
	"Keep looking ahead.\nLean your body LEFT and RIGHT.",
	"Keep looking ahead.\nLean FORWARD and BACK.",
	"Sit up TALL, then slouch DOWN.",
	"Turn your head to the LEFT and keep it there.\nNow tilt your head toward each shoulder.",
	"Look DOWN and keep looking down.\nNow turn your head LEFT and RIGHT."
};
// What each step checks: a native channel (CHANNEL_NAMES index), or the rotation order for
// combined movements (STEP_ORDER_*), which single movements can't reveal.
const int STEP_ORDER_YAW_ROLL = -1;
const int STEP_ORDER_YAW_PITCH = -2;
const array<int> CALIBRATION_CHANNELS = { 1, 0, 2, 3, 5, 4, STEP_ORDER_YAW_ROLL, STEP_ORDER_YAW_PITCH };
const int CALIBRATION_SETTLE_FRAMES = 60;

int g_calibrationStep = 0;
int g_calibrationSettleFrames = 0;
int g_calibrationFlips = 0;       // times the current step's setting was changed
int g_calibrationStartFlags = 0;  // restored when exiting without finishing
array<string> g_calibrationNotes;

// Rotation order steps: orders tried in this step, the order the step started with, and the
// yaw-vs-roll nesting confirmed by the first order step (the second one keeps it).
array<bool> g_orderTried(6);
int g_orderStepStart = 0;
bool g_yawOutsideRoll = true;

string StepName(int channel)
{
	if (channel == STEP_ORDER_YAW_ROLL) return "turn + tilt";
	if (channel == STEP_ORDER_YAW_PITCH) return "look down + turn";
	return CHANNEL_NAMES[channel];
}

void BeginCalibrationStep()
{
	g_calibrationFlips = 0;
	for (uint i = 0; i < g_orderTried.Length; i++) {
		g_orderTried[i] = false;
	}
	g_orderTried[Math::Clamp(S_RotationOrder, 0, 5)] = true;
	g_orderStepStart = S_RotationOrder;
	Native::CalibrationReset();
}

// Next untried rotation order for the current order step, or -1.
int NextOrderCandidate(int channel)
{
	for (int k = 1; k <= 6; k++) {
		int order = (S_RotationOrder + k) % 6;
		if (g_orderTried[order]) continue;
		if (channel == STEP_ORDER_YAW_PITCH && ROTATION_YAW_OUTSIDE_ROLL[order] != g_yawOutsideRoll) continue;
		return order;
	}
	return -1;
}

void StartCalibration()
{
	Native::Recenter();
	S_ShowUiInVr = true; // the instructions are shown in the headset
	g_calibrating = true;
	g_calibrationResult = "";
	g_calibrationStep = 0;
	g_calibrationStartFlags = ModelFlags();
	g_calibrationNotes.RemoveRange(0, g_calibrationNotes.Length);
	g_calibrationSettleFrames = CALIBRATION_SETTLE_FRAMES;
	BeginCalibrationStep();
}

// Exits without keeping any changes made during this calibration.
void CancelCalibration()
{
	SetModelFlags(g_calibrationStartFlags);
	g_calibrating = false;
	g_calibrationResult = "exited without changes";
	print("Calibration exited without changes; model flags " + ModelFlags());
}

bool CalibrationSettling()
{
	return g_calibrating && g_calibrationSettleFrames > 0;
}

string CalibrationInstruction()
{
	if (CalibrationSettling()) {
		return "Sit normally and look straight ahead...";
	}
	return CALIBRATION_STEPS[g_calibrationStep];
}

// Called every frame while the cockpit camera is active.
void UpdateCalibration()
{
	if (g_calibrating && g_calibrationSettleFrames > 0 && --g_calibrationSettleFrames == 0) {
		Native::CalibrationReset();
	}
}

void FlipAxis(int channel)
{
	switch (channel) {
		case 0: S_ModelNegPitch = !S_ModelNegPitch; break;
		case 1: S_ModelNegYaw = !S_ModelNegYaw; break;
		case 2: S_ModelNegRoll = !S_ModelNegRoll; break;
		case 3: S_ModelNegPosX = !S_ModelNegPosX; break;
		case 4: S_ModelNegPosY = !S_ModelNegPosY; break;
		case 5: S_ModelNegPosZ = !S_ModelNegPosZ; break;
	}
}

// feelsNatural: the player's answer for the current step.
void AnswerCalibration(bool feelsNatural)
{
	if (!g_calibrating || CalibrationSettling()) {
		return;
	}
	int channel = CALIBRATION_CHANNELS[g_calibrationStep];
	if (channel < 0) {
		if (!AnswerOrderStep(feelsNatural, channel)) {
			return; // trying another rotation order
		}
	} else if (!feelsNatural) {
		FlipAxis(channel);
		g_calibrationFlips++;
		if (g_calibrationFlips < 2) {
			Native::CalibrationReset();
			return; // ask again with the axis flipped
		}
		// Neither direction felt right: put it back and note it.
		FlipAxis(channel);
		g_calibrationNotes.InsertLast(CHANNEL_NAMES[channel] + " felt off either way");
	} else if (g_calibrationFlips > 0) {
		g_calibrationNotes.InsertLast(CHANNEL_NAMES[channel] + " inverted");
	}
	print("Calibration: " + StepName(channel) + (feelsNatural ? " OK" : " unresolved") + " after "
		+ g_calibrationFlips + " change(s); model flags " + ModelFlags());

	g_calibrationStep++;
	BeginCalibrationStep();
	if (g_calibrationStep >= int(CALIBRATION_STEPS.Length)) {
		g_calibrating = false;
		g_calibrationResult = g_calibrationNotes.Length == 0 ? "everything was already correct" : "";
		for (uint i = 0; i < g_calibrationNotes.Length; i++) {
			g_calibrationResult += (i > 0 ? ", " : "") + g_calibrationNotes[i];
		}
		print("Calibration finished: " + g_calibrationResult + ". Model flags " + ModelFlags());
	}
}

// Returns true when the step is done, false when another rotation order is being tried.
bool AnswerOrderStep(bool feelsNatural, int channel)
{
	if (!feelsNatural) {
		int next = NextOrderCandidate(channel);
		if (next >= 0) {
			S_RotationOrder = next;
			g_orderTried[next] = true;
			g_calibrationFlips++;
			return false;
		}
		// Nothing felt right: go back to the order this step started with.
		S_RotationOrder = g_orderStepStart;
		g_calibrationNotes.InsertLast(StepName(channel) + " felt off with every rotation order");
	} else if (g_calibrationFlips > 0) {
		g_calibrationNotes.InsertLast("rotation order " + ROTATION_ORDER_NAMES[S_RotationOrder]);
	}
	if (channel == STEP_ORDER_YAW_ROLL) {
		g_yawOutsideRoll = ROTATION_YAW_OUTSIDE_ROLL[S_RotationOrder];
	}
	return true;
}

void RenderCalibrationGuide()
{
	string exitText = KeyName(S_CalibrateKey) + ": exit without changes";
	if (CalibrationSettling()) {
		UI::Text("\\$0ffCalibration\\$z - get ready");
		UI::PushFontSize(VR_PANEL_FONT_SIZE * 1.4f);
		UI::Text(CalibrationInstruction());
		UI::PopFontSize();
		UI::Text(exitText);
		return;
	}
	int channel = CALIBRATION_CHANNELS[g_calibrationStep];
	UI::Text("\\$0ffCalibration\\$z - step " + (g_calibrationStep + 1) + " of " + CALIBRATION_STEPS.Length
		+ " (" + StepName(channel) + ")");
	UI::PushFontSize(VR_PANEL_FONT_SIZE * 1.4f);
	UI::Text(CalibrationInstruction());
	if (g_calibrationFlips > 0 && channel < 0) {
		UI::Text("\\$fa0I changed how turning and tilting combine (try " + (g_calibrationFlips + 1) + ").\nHow about now?\\$z");
	} else if (g_calibrationFlips > 0) {
		UI::Text("\\$fa0I reversed " + CHANNEL_NAMES[channel] + ". How about now?\\$z");
	} else {
		UI::Text("Does that feel natural,\nor is something off?");
	}
	UI::Text("\\$0f0" + KeyName(S_CalibrationYesKey) + "\\$z  feels natural        \\$f80"
		+ KeyName(S_CalibrationNoKey) + "\\$z  something's off");
	UI::PopFontSize();
	UI::Text(exitText);
}

// Keys while calibrating. Returns true when handled (and kept from the game).
bool HandleCalibrationKey(VirtualKey key)
{
	if (!g_calibrating) {
		return false;
	}
	if (key == S_CalibrationYesKey) {
		AnswerCalibration(true);
		return true;
	}
	if (key == S_CalibrationNoKey) {
		AnswerCalibration(false);
		return true;
	}
	if (key == S_CalibrateKey) {
		CancelCalibration();
		return true;
	}
	return false;
}
