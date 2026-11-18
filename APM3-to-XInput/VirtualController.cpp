#include "VirtualController.h"

#include <stdio.h>
#include <mutex>
#include <string>

#include <ViGEm/Client.h>

static std::string FormatError(const char* action, int code) {
	char buffer[256];
	const char* hint = "";
	if ((unsigned)code == (unsigned)VIGEM_ERROR_BUS_NOT_FOUND)
		hint = " (ViGEmBus isn't installed: get it from https://github.com/nefarius/ViGEmBus/releases)";
	else if ((unsigned)code == (unsigned)VIGEM_ERROR_BUS_VERSION_MISMATCH)
		hint = " (the installed ViGEmBus is too old, install the latest release)";
	else if ((unsigned)code == (unsigned)VIGEM_ERROR_NO_FREE_SLOT)
		hint = " (no free controller slots on ViGEmBus)";
	snprintf(buffer, sizeof(buffer), "%s failed with 0x%08X%s", action, (unsigned)code, hint);
	return buffer;
}

VirtualControllerError::VirtualControllerError(const char* action, int code)
	: std::runtime_error(FormatError(action, code)), code_(code) {
}

// One connection to ViGEmBus is shared by every controller and kept for the whole process
static PVIGEM_CLIENT Client() {
	static std::once_flag once;
	static PVIGEM_CLIENT client = NULL;
	static VIGEM_ERROR result = VIGEM_ERROR_NONE;
	std::call_once(once, [] {
		client = vigem_alloc();
		if (client == NULL) {
			result = VIGEM_ERROR_BUS_ACCESS_FAILED;
			return;
		}
		result = vigem_connect(client);
		if (!VIGEM_SUCCESS(result)) {
			vigem_free(client);
			client = NULL;
		}
	});
	if (client == NULL)
		throw VirtualControllerError("vigem_connect", (int)result);
	return client;
}

// GamepadState keeps the Xbox One button bits the rest of the program uses;
// ViGEm's Xbox 360 pad wants the XUSB layout instead
static XUSB_REPORT ToXusb(const GamepadState& state) {
	static const struct { Button from; USHORT to; } buttons[] = {
		{ Button::Up,    XUSB_GAMEPAD_DPAD_UP },
		{ Button::Down,  XUSB_GAMEPAD_DPAD_DOWN },
		{ Button::Left,  XUSB_GAMEPAD_DPAD_LEFT },
		{ Button::Right, XUSB_GAMEPAD_DPAD_RIGHT },
		{ Button::Menu,  XUSB_GAMEPAD_START },
		{ Button::View,  XUSB_GAMEPAD_BACK },
		{ Button::LS,    XUSB_GAMEPAD_LEFT_THUMB },
		{ Button::RS,    XUSB_GAMEPAD_RIGHT_THUMB },
		{ Button::LB,    XUSB_GAMEPAD_LEFT_SHOULDER },
		{ Button::RB,    XUSB_GAMEPAD_RIGHT_SHOULDER },
		{ Button::A,     XUSB_GAMEPAD_A },
		{ Button::B,     XUSB_GAMEPAD_B },
		{ Button::X,     XUSB_GAMEPAD_X },
		{ Button::Y,     XUSB_GAMEPAD_Y },
	};
	XUSB_REPORT report = {};
	for (const auto& b : buttons) {
		if ((USHORT)(state.buttons & b.from) != 0)
			report.wButtons |= b.to;
	}
	report.bLeftTrigger = (BYTE)(state.left_trigger >> 8);
	report.bRightTrigger = (BYTE)(state.right_trigger >> 8);
	report.sThumbLX = state.left_x;
	report.sThumbLY = state.left_y;
	report.sThumbRX = state.right_x;
	report.sThumbRY = state.right_y;
	return report;
}

VirtualController::VirtualController() {
	PVIGEM_CLIENT client = Client();

	PVIGEM_TARGET target = vigem_target_x360_alloc();
	if (target == NULL)
		throw VirtualControllerError("vigem_target_x360_alloc", (int)VIGEM_ERROR_INVALID_TARGET);

	VIGEM_ERROR result = vigem_target_add(client, target);
	if (!VIGEM_SUCCESS(result)) {
		vigem_target_free(target);
		throw VirtualControllerError("vigem_target_add", (int)result);
	}
	target_ = target;
	open_ = true;

	// Give Windows a moment to finish setting up the new device before input arrives
	Sleep(SETTLE_TIME_MS);
}

VirtualController::~VirtualController() {
	Close();
}

VirtualController::VirtualController(VirtualController&& other) noexcept
	: target_(other.target_), open_(other.open_), state_(other.state_) {
	other.target_ = nullptr;
	other.open_ = false;
}

VirtualController& VirtualController::operator=(VirtualController&& other) noexcept {
	if (this != &other) {
		Close();
		target_ = other.target_;
		open_ = other.open_;
		state_ = other.state_;
		other.target_ = nullptr;
		other.open_ = false;
	}
	return *this;
}

void VirtualController::Close() noexcept {
	if (!open_)
		return;
	open_ = false;

	PVIGEM_CLIENT client = NULL;
	try {
		client = Client();
	} catch (...) {
	}
	PVIGEM_TARGET target = (PVIGEM_TARGET)target_;
	if (client) {
		// let go of everything first so nothing stays held down in a game
		vigem_target_x360_update(client, target, XUSB_REPORT{});
		vigem_target_remove(client, target);
	}
	vigem_target_free(target);
	target_ = nullptr;
}

void VirtualController::SetState(const GamepadState& state) {
	if (!open_)
		throw VirtualControllerError("SetState on a moved-from controller", E_HANDLE);

	VIGEM_ERROR result = vigem_target_x360_update(Client(), (PVIGEM_TARGET)target_, ToXusb(state));
	if (!VIGEM_SUCCESS(result))
		throw VirtualControllerError("vigem_target_x360_update", (int)result);
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
