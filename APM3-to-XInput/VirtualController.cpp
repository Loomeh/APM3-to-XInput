#include "VirtualController.h"

#include <combaseapi.h>
#include <stdio.h>
#include <string>

#include "GipSyntheticEx.h"

// xboxgipsynthetic.dll report type for gamepad input
#define REPORT_TYPE_GAMEPAD 0
// xboxgipsynthetic.dll controller type for a standard gamepad
#define CONTROLLER_TYPE_GAMEPAD 0
// How long to wait after connecting before any input
#define SETTLE_TIME_MS 500

static std::string FormatError(const char* action, int code) {
	char buffer[128];
	snprintf(buffer, sizeof(buffer), "%s failed with 0x%08X", action, code);
	return buffer;
}

VirtualControllerError::VirtualControllerError(const char* action, int code)
	: std::runtime_error(FormatError(action, code)), code_(code) {
}

// The library must only be started once, however many controllers get created
static void StartLibrary() {
	// xboxgipsynthetic.dll uses WinRT, including from its own background threads,
	// so the COM multithreaded apartment has to stay alive for the whole process.
	// The cookie is deliberately never released.
	static CO_MTA_USAGE_COOKIE mta_cookie;
	static const HRESULT mta_result = CoIncrementMTAUsage(&mta_cookie);
	if (FAILED(mta_result))
		throw VirtualControllerError("CoIncrementMTAUsage", mta_result);

	static const int result = GipSynthEx_Startup();
	if (result != GIPSYNTHEX_STARTED_SUCCESSFULLY && result != GIPSYNTHEX_STARTED_WITHOUT_CONNECTEX)
		throw VirtualControllerError("GipSynthEx_Startup", result);
}

VirtualController::VirtualController() {
	StartLibrary();

	int rval = GipSynthEx_CreateController(CONTROLLER_TYPE_GAMEPAD, &handle_);
	if (rval != 0)
		throw VirtualControllerError("GipSynthEx_CreateController", rval);

	rval = GipSynthEx_Connect(handle_);
	if (rval != 0) {
		GipSynthEx_RemoveController(handle_);
		throw VirtualControllerError("GipSynthEx_Connect", rval);
	}
	open_ = true;

	// Sending a report before Windows has finished registering the controller
	// usually crashes the process, and there is no way to ask if it's ready yet.
	Sleep(SETTLE_TIME_MS);
}

VirtualController::~VirtualController() {
	Close();
}

VirtualController::VirtualController(VirtualController&& other) noexcept
	: handle_(other.handle_), open_(other.open_), state_(other.state_) {
	other.open_ = false;
}

VirtualController& VirtualController::operator=(VirtualController&& other) noexcept {
	if (this != &other) {
		Close();
		handle_ = other.handle_;
		open_ = other.open_;
		state_ = other.state_;
		other.open_ = false;
	}
	return *this;
}

void VirtualController::Close() noexcept {
	if (!open_)
		return;
	open_ = false;

	// let go of everything first so nothing stays held down in a game
	GamepadState neutral;
	GipSynthEx_SendReport(handle_, REPORT_TYPE_GAMEPAD, (BYTE*)&neutral, sizeof(neutral));
	GipSynthEx_Disconnect(handle_);
	GipSynthEx_RemoveController(handle_);
}

void VirtualController::SetState(const GamepadState& state) {
	if (!open_)
		throw VirtualControllerError("SetState on a moved-from controller", E_HANDLE);

	GamepadState report = state;
	int rval = GipSynthEx_SendReport(handle_, REPORT_TYPE_GAMEPAD, (BYTE*)&report, sizeof(report));
	if (rval != 0)
		throw VirtualControllerError("GipSynthEx_SendReport", rval);
	state_ = state;
}

void VirtualController::Press(Button buttons) {
	GamepadState state = state_;
	state.buttons = state.buttons | buttons;
	SetState(state);
}

void VirtualController::Release(Button buttons) {
	GamepadState state = state_;
	state.buttons = state.buttons & ~buttons;
	SetState(state);
}

void VirtualController::SetLeftTrigger(USHORT value) {
	GamepadState state = state_;
	state.left_trigger = value;
	SetState(state);
}

void VirtualController::SetRightTrigger(USHORT value) {
	GamepadState state = state_;
	state.right_trigger = value;
	SetState(state);
}

void VirtualController::SetLeftStick(SHORT x, SHORT y) {
	GamepadState state = state_;
	state.left_x = x;
	state.left_y = y;
	SetState(state);
}

void VirtualController::SetRightStick(SHORT x, SHORT y) {
	GamepadState state = state_;
	state.right_x = x;
	state.right_y = y;
	SetState(state);
}

void VirtualController::Reset() {
	SetState(GamepadState{});
}
