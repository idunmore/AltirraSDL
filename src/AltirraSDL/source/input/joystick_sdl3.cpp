//	Altirra SDL3 frontend - joystick/gamepad support
//
//	Implements IATJoystickManager using SDL3's gamepad API.  This is the
//	SDL3 equivalent of the Windows XInput/DirectInput implementation in
//	src/Altirra/source/joystick.cpp.
//
//	The simulator calls Poll() once per VBlank via AnticOnVBlank().
//	Poll() reads the current state of all connected gamepads and reports
//	button/axis changes to ATInputManager, which then routes them through
//	the input mapping system to emulated Atari controllers.
//
//	Button and axis numbering matches the XInput convention used by the
//	Windows version, so Altirra's default and user-created input maps
//	work identically on both platforms.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <vd2/system/math.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include "joystick_sdl3.h"
#include "inputmanager.h"
#include "logging.h"

namespace {

// =========================================================================
// SDL3 gamepad button → Altirra button index (XInput order)
//
// XInput:  A=0, B=1, X=2, Y=3, LB=4, RB=5, Back=6, Start=7,
//          LThumb=8, RThumb=9, Guide=10
// SDL3:    South=0, East=1, West=2, North=3, Back=4, Guide=5,
//          Start=6, LStick=7, RStick=8, LShoulder=9, RShoulder=10
// =========================================================================

static const int kSDLButtonToAltirra[] = {
	0,	// SDL_GAMEPAD_BUTTON_SOUTH  → A (button 0)
	1,	// SDL_GAMEPAD_BUTTON_EAST   → B (button 1)
	2,	// SDL_GAMEPAD_BUTTON_WEST   → X (button 2)
	3,	// SDL_GAMEPAD_BUTTON_NORTH  → Y (button 3)
	6,	// SDL_GAMEPAD_BUTTON_BACK   → Back (button 6)
	10,	// SDL_GAMEPAD_BUTTON_GUIDE  → Guide (button 10)
	7,	// SDL_GAMEPAD_BUTTON_START  → Start (button 7)
	8,	// SDL_GAMEPAD_BUTTON_LEFT_STICK  → LThumb (button 8)
	9,	// SDL_GAMEPAD_BUTTON_RIGHT_STICK → RThumb (button 9)
	4,	// SDL_GAMEPAD_BUTTON_LEFT_SHOULDER  → LB (button 4)
	5,	// SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER → RB (button 5)
};

static const int kNumMappedButtons = sizeof(kSDLButtonToAltirra) / sizeof(kSDLButtonToAltirra[0]);

// Convert analog stick X/Y to a 4-bit direction mask.
// Matches ATController::ConvertAnalogToDirectionMask in joystick.cpp.
// Returns: bit 0 = left, 1 = right, 2 = up, 3 = down.
static uint32 ConvertAnalogToDirectionMask(sint32 x, sint32 y, sint32 deadZone) {
	const float kTan22_5d = 0.4142135623730950488016887242097f;
	float dxf = fabsf((float)x);
	float dyf = fabsf((float)y);
	uint32 mask = 0;

	if (dxf * dxf + dyf * dyf < (float)deadZone * (float)deadZone)
		return 0;

	if (dxf > dyf * kTan22_5d) {
		if (x < 0) mask |= (1 << 0);
		if (x > 0) mask |= (1 << 1);
	}

	if (dyf > dxf * kTan22_5d) {
		if (y > 0) mask |= (1 << 2);
		if (y < 0) mask |= (1 << 3);
	}

	return mask;
}

} // anonymous namespace

// =========================================================================
// Per-gamepad controller state
// =========================================================================

struct ATControllerSDL3 {
	SDL_Gamepad *mpGamepad = nullptr;
	SDL_JoystickID mInstanceID = 0;
	int mUnit = -1;			// ATInputManager unit ID
	ATInputUnitIdentifier mId {};

	// Previous frame state for delta detection
	uint32 mLastButtons = 0;		// 11 button bits (XInput order)
	uint32 mLastAxisButtons = 0;	// 16 axis-button bits
	sint32 mLastAxisVals[6] {};
	sint32 mLastDeadAxisVals[6] {};
};

// =========================================================================
// ATJoystickManagerSDL3Impl
// =========================================================================

class ATJoystickManagerSDL3Impl final : public ATJoystickManagerSDL3 {
public:
	~ATJoystickManagerSDL3Impl() override;

	bool Init(void *hwnd, ATInputManager *inputMan) override;
	void Shutdown() override;

	ATJoystickTransforms GetTransforms() const override { return mTransforms; }
	void SetTransforms(const ATJoystickTransforms& transforms) override { mTransforms = transforms; }
	void SetCaptureMode(bool capture) override { mbCaptureMode = capture; }
	void SetOnActivity(const vdfunction<void()>& fn) override { mOnActivity = fn; }
	void RescanForDevices() override;

	PollResult Poll() override;
	bool PollForCapture(int& unit, uint32& inputCode, uint32& inputCode2) override;
	const ATJoystickState *PollForCapture(uint32& n) override;
	uint32 GetJoystickPortStates() const override;

	void CloseGamepad(SDL_JoystickID id) override;

private:
	void OpenGamepad(SDL_JoystickID id);
	ATControllerSDL3 *FindController(SDL_JoystickID id);

	void PollController(ATControllerSDL3& ctrl, bool& activity);
	void ConvertStick(sint32 dst[2], sint32 x, sint32 y);

	ATInputManager *mpInputManager = nullptr;
	ATJoystickTransforms mTransforms {};
	vdfunction<void()> mOnActivity;
	bool mbCaptureMode = false;

	vdfastvector<ATControllerSDL3 *> mControllers;
	vdfastvector<ATJoystickState> mCaptureStates;
};

ATJoystickManagerSDL3Impl::~ATJoystickManagerSDL3Impl() {
	Shutdown();
}

bool ATJoystickManagerSDL3Impl::Init(void *, ATInputManager *inputMan) {
	mpInputManager = inputMan;

	// Set default transforms matching the Windows defaults
	mTransforms.mStickAnalogDeadZone = (sint32)(0.15f * 65536);
	mTransforms.mStickDigitalDeadZone = (sint32)(0.45f * 65536);
	mTransforms.mStickAnalogPower = 1.0f;
	mTransforms.mTriggerAnalogDeadZone = (sint32)(0.05f * 65536);
	mTransforms.mTriggerDigitalDeadZone = (sint32)(0.20f * 65536);
	mTransforms.mTriggerAnalogPower = 1.0f;

	RescanForDevices();

	// Diagnostic: log how many gamepads SDL3 actually saw at startup.
	// On macOS, a missing .app bundle / NSGameControllerUsageDescription
	// causes GameController.framework to silently report zero devices,
	// and this line is the first place a user can confirm that.
	LOG_INFO("Joystick", "SDL3 gamepad enumeration: %u device(s) detected at startup",
		(unsigned)mControllers.size());
	if (mControllers.empty()) {
		LOG_INFO("Joystick",
			"  (No gamepads found.  On macOS this usually means the binary is "
			"not running from a signed .app bundle with "
			"NSGameControllerUsageDescription in Info.plist.)");
	}

	return true;
}

void ATJoystickManagerSDL3Impl::Shutdown() {
	for (auto *ctrl : mControllers) {
		if (ctrl->mUnit >= 0 && mpInputManager)
			mpInputManager->UnregisterInputUnit(ctrl->mUnit);
		if (ctrl->mpGamepad)
			SDL_CloseGamepad(ctrl->mpGamepad);
		delete ctrl;
	}
	mControllers.clear();
	mpInputManager = nullptr;
}

void ATJoystickManagerSDL3Impl::RescanForDevices() {
	// Check for newly connected gamepads
	int count = 0;
	SDL_JoystickID *ids = SDL_GetGamepads(&count);
	if (!ids)
		return;

	// Mark existing controllers
	for (auto *ctrl : mControllers)
		ctrl->mUnit |= 0; // no-op, just iterate

	// Open any gamepads we don't already track
	for (int i = 0; i < count; ++i) {
		if (!FindController(ids[i]))
			OpenGamepad(ids[i]);
	}

	SDL_free(ids);
}

void ATJoystickManagerSDL3Impl::OpenGamepad(SDL_JoystickID id) {
	SDL_Gamepad *gp = SDL_OpenGamepad(id);
	if (!gp)
		return;

	auto *ctrl = new ATControllerSDL3();
	ctrl->mpGamepad = gp;
	ctrl->mInstanceID = id;

	// Build a stable identifier from the joystick GUID
	SDL_GUID guid = SDL_GetGamepadGUIDForID(id);
	static_assert(sizeof(guid.data) >= sizeof(ctrl->mId.buf), "GUID too small");
	memcpy(ctrl->mId.buf, guid.data, sizeof(ctrl->mId.buf));

	// Register with ATInputManager
	const char *name = SDL_GetGamepadName(gp);
	VDStringW wname;
	if (name)
		wname.sprintf(L"SDL Gamepad: %hs", name);
	else
		wname = L"SDL Gamepad";

	ctrl->mUnit = mpInputManager->RegisterInputUnit(ctrl->mId, wname.c_str(), nullptr);

	mControllers.push_back(ctrl);

	LOG_INFO("Joystick", "Gamepad connected: %s (unit %d)", name ? name : "unknown", ctrl->mUnit);
}

void ATJoystickManagerSDL3Impl::CloseGamepad(SDL_JoystickID id) {
	for (auto it = mControllers.begin(); it != mControllers.end(); ++it) {
		if ((*it)->mInstanceID == id) {
			auto *ctrl = *it;
			LOG_INFO("Joystick", "Gamepad disconnected: unit %d", ctrl->mUnit);

			if (ctrl->mUnit >= 0 && mpInputManager)
				mpInputManager->UnregisterInputUnit(ctrl->mUnit);
			if (ctrl->mpGamepad)
				SDL_CloseGamepad(ctrl->mpGamepad);
			delete ctrl;
			mControllers.erase(it);
			return;
		}
	}
}

ATControllerSDL3 *ATJoystickManagerSDL3Impl::FindController(SDL_JoystickID id) {
	for (auto *ctrl : mControllers) {
		if (ctrl->mInstanceID == id)
			return ctrl;
	}
	return nullptr;
}

IATJoystickManager::PollResult ATJoystickManagerSDL3Impl::Poll() {
	if (mControllers.empty())
		return kPollResult_NoControllers;

	bool activity = false;
	for (auto *ctrl : mControllers)
		PollController(*ctrl, activity);

	if (activity && mOnActivity)
		mOnActivity();

	return activity ? kPollResult_OK : kPollResult_NoActivity;
}

void ATJoystickManagerSDL3Impl::PollController(ATControllerSDL3& ctrl, bool& activity) {
	if (!ctrl.mpGamepad || ctrl.mUnit < 0)
		return;

	// --- Read button state ---
	// Map SDL3 buttons to XInput-order button bits
	uint32 buttonStates = 0;
	for (int i = 0; i < kNumMappedButtons; ++i) {
		if (SDL_GetGamepadButton(ctrl.mpGamepad, (SDL_GamepadButton)i))
			buttonStates |= (1 << kSDLButtonToAltirra[i]);
	}

	// --- Read axis values ---
	// SDL3 thumbstick range: -32768 to 32767
	// Altirra axis range: -65536 to 65536 (XInput multiplies by 2)
	sint32 axisVals[6] {};
	sint32 deadVals[6] {};

	sint32 lx = SDL_GetGamepadAxis(ctrl.mpGamepad, SDL_GAMEPAD_AXIS_LEFTX);
	sint32 ly = SDL_GetGamepadAxis(ctrl.mpGamepad, SDL_GAMEPAD_AXIS_LEFTY);
	sint32 rx = SDL_GetGamepadAxis(ctrl.mpGamepad, SDL_GAMEPAD_AXIS_RIGHTX);
	sint32 ry = SDL_GetGamepadAxis(ctrl.mpGamepad, SDL_GAMEPAD_AXIS_RIGHTY);

	axisVals[0] = lx * 2;		// left stick X
	axisVals[1] = ly * 2;		// left stick Y (SDL3 Y+ = down, same as XInput after negate)
	axisVals[3] = rx * 2;		// right stick X
	axisVals[4] = ry * 2;		// right stick Y

	// Apply deadzone to sticks
	ConvertStick(deadVals, lx * 2, ly * 2);
	ConvertStick(deadVals + 3, rx * 2, ry * 2);

	// Note: SDL3 Y-axis is already "down = positive" which matches what
	// Altirra expects after XInput's negate. But XInput negates Y (line 481-483
	// in joystick.cpp), and Altirra's input mapping expects that. Let's match:
	// XInput: axisVals[1] = sThumbLY * -2 (invert Y so up = negative)
	// SDL3: LEFTY is already positive-down, so we DON'T negate.
	// But wait — XInput's sThumbLY is positive-up, so *-2 makes it positive-down.
	// SDL3's LEFTY is positive-down already. So both end up positive-down.
	// The deadified values also need the same treatment.
	// XInput code: mDeadifiedAxisVals[1] = -mDeadifiedAxisVals[1] (line 485)
	// That's because ConvertStick output matches the raw sign, and the raw was
	// positive-up, so negating makes it positive-down.
	// For SDL3 the raw is already positive-down, so we don't negate deadified.

	// Triggers: SDL3 range 0-32767, Altirra expects 0-65536
	for (int i = 0; i < 2; ++i) {
		SDL_GamepadAxis trigAxis = i ? SDL_GAMEPAD_AXIS_RIGHT_TRIGGER : SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
		sint32 rawVal = SDL_GetGamepadAxis(ctrl.mpGamepad, trigAxis);
		float fVal = (float)rawVal / 32767.0f;
		sint32 axisVal = (sint32)(fVal * 65536.0f);
		sint32 adjVal = 0;

		float trigThreshold = (float)mTransforms.mTriggerAnalogDeadZone / 65536.0f;
		if (fVal > trigThreshold) {
			float deadVal = (fVal - trigThreshold) / (1.0f - trigThreshold);
			adjVal = (sint32)(65536.0f * powf(deadVal, mTransforms.mTriggerAnalogPower));
		}

		if (i) {
			axisVals[5] = axisVal;
			deadVals[5] = adjVal;
		} else {
			axisVals[2] = axisVal;
			deadVals[2] = adjVal;
		}
	}

	// --- Compute axis buttons (digital from analog) ---
	// Matches joystick.cpp line 511-530
	//
	// ConvertAnalogToDirectionMask expects positive-up Y (Windows convention).
	// SDL3 Y-axis is positive-down, so we negate Y here.
	uint32 axisButtonStates = 0;
	axisButtonStates |= ConvertAnalogToDirectionMask(lx * 2, -ly * 2,
		mTransforms.mStickDigitalDeadZone / 2);

	// Left trigger pressed = bit 5
	// SDL3 triggers range 0-32767; threshold is in 0-65536 space, so >> 1
	if (SDL_GetGamepadAxis(ctrl.mpGamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) >
		(mTransforms.mTriggerDigitalDeadZone >> 1))
		axisButtonStates |= (1 << 5);

	// Right stick = bits 6-9 (negate Y for same reason)
	axisButtonStates |= ConvertAnalogToDirectionMask(rx * 2, -ry * 2,
		mTransforms.mStickDigitalDeadZone / 2) << 6;

	// Right trigger pressed = bit 11
	if (SDL_GetGamepadAxis(ctrl.mpGamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) >
		(mTransforms.mTriggerDigitalDeadZone >> 1))
		axisButtonStates |= (1 << 11);

	// D-pad as axis buttons: bits 12-15 (left, right, up, down)
	if (SDL_GetGamepadButton(ctrl.mpGamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT))
		axisButtonStates |= (1 << 12);
	if (SDL_GetGamepadButton(ctrl.mpGamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT))
		axisButtonStates |= (1 << 13);
	if (SDL_GetGamepadButton(ctrl.mpGamepad, SDL_GAMEPAD_BUTTON_DPAD_UP))
		axisButtonStates |= (1 << 14);
	if (SDL_GetGamepadButton(ctrl.mpGamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN))
		axisButtonStates |= (1 << 15);

	// --- Report changes to ATInputManager ---

	// Axis button deltas
	const uint32 axisButtonDelta = axisButtonStates ^ ctrl.mLastAxisButtons;
	for (uint32 i = 0; i < 16; ++i) {
		if (axisButtonDelta & (1 << i)) {
			if (axisButtonStates & (1 << i))
				mpInputManager->OnButtonDown(ctrl.mUnit, kATInputCode_JoyStick1Left + i);
			else
				mpInputManager->OnButtonUp(ctrl.mUnit, kATInputCode_JoyStick1Left + i);
		}
	}

	// Button deltas
	const uint32 buttonDelta = buttonStates ^ ctrl.mLastButtons;
	for (int i = 0; i < 11; ++i) {
		if (buttonDelta & (1 << i)) {
			if (buttonStates & (1 << i))
				mpInputManager->OnButtonDown(ctrl.mUnit, kATInputCode_JoyButton0 + i);
			else
				mpInputManager->OnButtonUp(ctrl.mUnit, kATInputCode_JoyButton0 + i);
		}
	}

	if (axisButtonDelta || buttonDelta)
		activity = true;

	// Axis value changes
	for (int i = 0; i < 6; ++i) {
		if (axisVals[i] != ctrl.mLastAxisVals[i])
			mpInputManager->OnAxisInput(ctrl.mUnit, kATInputCode_JoyHoriz1 + i,
				axisVals[i], deadVals[i]);
	}

	// Save state for next poll
	ctrl.mLastButtons = buttonStates;
	ctrl.mLastAxisButtons = axisButtonStates;
	memcpy(ctrl.mLastAxisVals, axisVals, sizeof(axisVals));
	memcpy(ctrl.mLastDeadAxisVals, deadVals, sizeof(deadVals));
}

void ATJoystickManagerSDL3Impl::ConvertStick(sint32 dst[2], sint32 x, sint32 y) {
	// Matches ATControllerXInput::ConvertStick — apply analog deadzone
	float fx = (float)x;
	float fy = (float)y;
	const float mag = sqrtf(fx * fx + fy * fy);
	sint32 rx = 0, ry = 0;

	if (mag > mTransforms.mStickAnalogDeadZone) {
		float scale = (mag - mTransforms.mStickAnalogDeadZone) /
			(mag * (65536.0f - mTransforms.mStickAnalogDeadZone));

		scale *= powf(mag * scale, mTransforms.mStickAnalogPower - 1.0f);

		fx *= scale;
		fy *= scale;

		rx = (sint32)(fx * 65536.0f);
		ry = (sint32)(fy * 65536.0f);

		if (rx < -65536) rx = -65536; else if (rx > 65536) rx = 65536;
		if (ry < -65536) ry = -65536; else if (ry > 65536) ry = 65536;
	}

	dst[0] = rx;
	dst[1] = ry;
}

bool ATJoystickManagerSDL3Impl::PollForCapture(int& unit, uint32& inputCode, uint32& inputCode2) {
	for (auto *ctrl : mControllers) {
		if (!ctrl->mpGamepad || ctrl->mUnit < 0)
			continue;

		// Check buttons
		uint32 buttonStates = 0;
		for (int i = 0; i < kNumMappedButtons; ++i) {
			if (SDL_GetGamepadButton(ctrl->mpGamepad, (SDL_GamepadButton)i))
				buttonStates |= (1 << kSDLButtonToAltirra[i]);
		}

		uint32 newButtons = buttonStates & ~ctrl->mLastButtons;
		ctrl->mLastButtons = buttonStates;

		if (newButtons) {
			unit = ctrl->mUnit;
			// Find lowest set bit
			for (int i = 0; i < 11; ++i) {
				if (newButtons & (1 << i)) {
					inputCode = kATInputCode_JoyButton0 + i;
					inputCode2 = 0;
					return true;
				}
			}
		}

		// Check axis buttons (sticks/dpad)
		// Simplified: check if any new digital direction appeared
		uint32 axisButtonStates = 0;
		sint32 lx = SDL_GetGamepadAxis(ctrl->mpGamepad, SDL_GAMEPAD_AXIS_LEFTX) * 2;
		sint32 ly = SDL_GetGamepadAxis(ctrl->mpGamepad, SDL_GAMEPAD_AXIS_LEFTY) * 2;
		axisButtonStates |= ConvertAnalogToDirectionMask(lx, -ly,
			mTransforms.mStickDigitalDeadZone / 2);

		sint32 rx2 = SDL_GetGamepadAxis(ctrl->mpGamepad, SDL_GAMEPAD_AXIS_RIGHTX) * 2;
		sint32 ry2 = SDL_GetGamepadAxis(ctrl->mpGamepad, SDL_GAMEPAD_AXIS_RIGHTY) * 2;
		axisButtonStates |= ConvertAnalogToDirectionMask(rx2, -ry2,
			mTransforms.mStickDigitalDeadZone / 2) << 6;

		if (SDL_GetGamepadButton(ctrl->mpGamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT))  axisButtonStates |= (1 << 12);
		if (SDL_GetGamepadButton(ctrl->mpGamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) axisButtonStates |= (1 << 13);
		if (SDL_GetGamepadButton(ctrl->mpGamepad, SDL_GAMEPAD_BUTTON_DPAD_UP))    axisButtonStates |= (1 << 14);
		if (SDL_GetGamepadButton(ctrl->mpGamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN))  axisButtonStates |= (1 << 15);

		uint32 newAxisButtons = axisButtonStates & ~ctrl->mLastAxisButtons;
		ctrl->mLastAxisButtons = axisButtonStates;

		if (newAxisButtons) {
			unit = ctrl->mUnit;
			for (int i = 0; i < 16; ++i) {
				if (newAxisButtons & (1 << i)) {
					inputCode = kATInputCode_JoyStick1Left + i;
					inputCode2 = kATInputCode_JoyHoriz1 + (i >> 1);
					return true;
				}
			}
		}
	}

	return false;
}

const ATJoystickState *ATJoystickManagerSDL3Impl::PollForCapture(uint32& n) {
	mCaptureStates.clear();

	for (auto *ctrl : mControllers) {
		if (!ctrl->mpGamepad || ctrl->mUnit < 0)
			continue;

		ATJoystickState state {};
		state.mUnit = ctrl->mUnit;
		state.mButtons = ctrl->mLastButtons;
		state.mAxisButtons = ctrl->mLastAxisButtons;
		memcpy(state.mAxisVals, ctrl->mLastAxisVals, sizeof(state.mAxisVals));
		memcpy(state.mDeadifiedAxisVals, ctrl->mLastDeadAxisVals, sizeof(state.mDeadifiedAxisVals));

		mCaptureStates.push_back(state);
	}

	n = (uint32)mCaptureStates.size();
	return mCaptureStates.empty() ? nullptr : mCaptureStates.data();
}

uint32 ATJoystickManagerSDL3Impl::GetJoystickPortStates() const {
	// Combine all controller axis button states for port readback
	uint32 states = 0;
	for (auto *ctrl : mControllers)
		states |= ctrl->mLastAxisButtons;
	return states;
}

// =========================================================================
// Factory function — replaces the stub in joystick_stubs.cpp
// =========================================================================

IATJoystickManager *ATCreateJoystickManager() {
	return new ATJoystickManagerSDL3Impl();
}
