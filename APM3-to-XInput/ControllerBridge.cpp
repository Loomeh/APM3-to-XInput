#include "ControllerBridge.h"

#include <hidusage.h>
#include <stdio.h>
#include <string.h>
#include <timeapi.h>

#include <SDL3/SDL.h>

#include <algorithm>

#include "UsbPort.h"

#pragma comment(lib, "winmm.lib")

static const ULONGLONG RECONNECT_INTERVAL_MS = 1000;
static const LONGLONG POLL_INTERVAL_100NS = 10000;

// SDL names buttons by position, so a Nintendo B (the bottom one) becomes A
static const struct {
	SDL_GamepadButton from;
	Button to;
} gamepad_buttons[] = {
	{ SDL_GAMEPAD_BUTTON_SOUTH, Button::A }, { SDL_GAMEPAD_BUTTON_EAST, Button::B },
	{ SDL_GAMEPAD_BUTTON_WEST, Button::X }, { SDL_GAMEPAD_BUTTON_NORTH, Button::Y },
	{ SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, Button::LB }, { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, Button::RB },
	{ SDL_GAMEPAD_BUTTON_LEFT_STICK, Button::LS }, { SDL_GAMEPAD_BUTTON_RIGHT_STICK, Button::RS },
	{ SDL_GAMEPAD_BUTTON_DPAD_UP, Button::Up }, { SDL_GAMEPAD_BUTTON_DPAD_DOWN, Button::Down },
	{ SDL_GAMEPAD_BUTTON_DPAD_LEFT, Button::Left }, { SDL_GAMEPAD_BUTTON_DPAD_RIGHT, Button::Right },
	{ SDL_GAMEPAD_BUTTON_START, Button::Menu }, { SDL_GAMEPAD_BUTTON_BACK, Button::View },
};

ControllerBridge::ControllerBridge(const ControllerConfig& config) : config_(config) {
	if (!config_.device.empty())
		Connect();
}

ControllerBridge::~ControllerBridge() {
	if (gamepad_ != nullptr)
		SDL_CloseGamepad(gamepad_);
}

// Finds which report carries each mapped input, and fails if the device doesn't have it
void ControllerBridge::Bind() {
	const HidLayout& layout = device_->Layout();
	bound_.clear();

	for (const InputMapping& mapping : config_.mappings) {
		BoundMapping bound;
		bound.mapping = mapping;
		bool found = false;
		USAGE usage = mapping.source.usage;

		if (mapping.source.kind == SourceKind::Button) {
			for (const HIDP_BUTTON_CAPS& caps : layout.buttons) {
				USAGE first = caps.IsRange ? caps.Range.UsageMin : caps.NotRange.Usage;
				USAGE last = caps.IsRange ? caps.Range.UsageMax : caps.NotRange.Usage;
				if (!found && caps.UsagePage == HID_USAGE_PAGE_BUTTON && usage >= first && usage <= last) {
					bound.report_id = caps.ReportID;
					found = true;
				}
			}
		} else {
			for (const HIDP_VALUE_CAPS& caps : layout.values) {
				USAGE first = caps.IsRange ? caps.Range.UsageMin : caps.NotRange.Usage;
				USAGE last = caps.IsRange ? caps.Range.UsageMax : caps.NotRange.Usage;
				if (!found && caps.UsagePage == HID_USAGE_PAGE_GENERIC && usage >= first && usage <= last) {
					bound.report_id = caps.ReportID;
					bound.bit_size = caps.BitSize;
					bound.minimum = caps.LogicalMin;
					bound.maximum = caps.LogicalMax;
					// Some devices declare an unsigned range that reads as negative
					if (bound.maximum <= bound.minimum) {
						bound.minimum = 0;
						bound.maximum = (1ll << caps.BitSize) - 1;
					}
					found = true;
				}
			}
		}

		if (!found) {
			throw ConfigError("[" + config_.name + "] " + mapping.target + "=" + mapping.source.text +
				": this device has no such input");
		}
		bound_.push_back(bound);
	}

	pressed_.resize(HidP_MaxUsageListLength(HidP_Input, HID_USAGE_PAGE_BUTTON, layout.preparsed));
}

void ControllerBridge::Connect() {
	next_connect_ = GetTickCount64() + RECONNECT_INTERVAL_MS;
	try {
		device_ = std::make_unique<HidDevice>(config_.device);
	} catch (const HidError& error) {
		if (!reported_missing_)
			printf("[%s] %s. Waiting for it.\n", config_.name.c_str(), error.what());
		reported_missing_ = true;
		return;
	}
	try {
		Bind();
	} catch (...) {
		device_.reset();
		throw;
	}
	reported_missing_ = false;
	printf("[%s] Reading %s\n", config_.name.c_str(), config_.device.c_str());
}

static bool AxisActive(long long value, long long minimum, long long maximum, Direction direction) {
	long long center = minimum + (maximum - minimum) / 2;
	long long threshold = (maximum - minimum) / 4;
	return direction == Direction::Negative ? value < center - threshold : value > center + threshold;
}

// How far along its range an axis is, from 0 at its minimum to 65535 at its maximum
static USHORT AxisAmount(long long value, long long minimum, long long maximum) {
	value = std::clamp(value, minimum, maximum);
	return (USHORT)((value - minimum) * 65535 / (maximum - minimum));
}

static bool HatActive(long long value, long long minimum, long long maximum, Direction direction) {
	long long positions = maximum - minimum + 1;
	if (value < minimum || value > maximum || positions <= 0)
		return false; // centered: hats report a value outside their range
	int position = (int)((value - minimum) * 8 / positions); // 0 is up, going clockwise in eighths
	switch (direction) {
	case Direction::Up:    return position == 7 || position == 0 || position == 1;
	case Direction::Right: return position >= 1 && position <= 3;
	case Direction::Down:  return position >= 3 && position <= 5;
	case Direction::Left:  return position >= 5 && position <= 7;
	default:               return false;
	}
}

void ControllerBridge::Apply(const BYTE* report, ULONG length) {
	if (length == 0)
		return;
	const HidLayout& layout = device_->Layout();
	const UCHAR report_id = report[0];

	// Inputs in other reports keep their last value, so buttons split across reports work
	ULONG pressed_count = (ULONG)pressed_.size();
	bool have_buttons = false;
	for (BoundMapping& bound : bound_) {
		if (bound.report_id != report_id)
			continue;
		const InputSource& source = bound.mapping.source;

		if (source.kind == SourceKind::Button) {
			if (!have_buttons) {
				if (HidP_GetUsages(HidP_Input, HID_USAGE_PAGE_BUTTON, 0, pressed_.data(), &pressed_count, layout.preparsed, (PCHAR)report, length) != HIDP_STATUS_SUCCESS)
					continue;
				have_buttons = true;
			}
			bound.active = std::find(pressed_.begin(), pressed_.begin() + pressed_count, source.usage) != pressed_.begin() + pressed_count;
			continue;
		}

		ULONG raw;
		if (HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_GENERIC, 0, source.usage, &raw, layout.preparsed, (PCHAR)report, length) != HIDP_STATUS_SUCCESS)
			continue;
		long long value = raw;
		if (bound.minimum < 0 && bound.bit_size < 64 && (raw & (1ull << (bound.bit_size - 1))))
			value -= 1ll << bound.bit_size;
		if (source.kind == SourceKind::AnalogAxis) {
			bound.trigger_amount = AxisAmount(value, bound.minimum, bound.maximum);
			bound.active = bound.trigger_amount > 0;
		} else if (source.kind == SourceKind::Hat) {
			bound.active = HatActive(value, bound.minimum, bound.maximum, source.direction);
		} else {
			bound.active = AxisActive(value, bound.minimum, bound.maximum, source.direction);
		}
	}
	Send();
}

// SDL's sticks have down as positive, XInput's have up
static SHORT FlipY(Sint16 value) {
	return (SHORT)(std::min)(-(int)value, 32767);
}

// SDL's triggers go from 0 to 32767
static USHORT TriggerAmount(Sint16 value) {
	return (USHORT)((std::max)(0, (int)value) * 65535 / 32767);
}

void ControllerBridge::Send() {
	GamepadState state;
	for (const BoundMapping& bound : bound_) {
		if (!bound.active)
			continue;
		state.buttons = state.buttons | bound.mapping.button;
		if (bound.mapping.left_trigger)
			state.left_trigger = bound.trigger_amount;
		if (bound.mapping.right_trigger)
			state.right_trigger = bound.trigger_amount;
	}

	// Both sources are mixed: buttons held on either count, the most pulled trigger wins
	if (gamepad_ != nullptr) {
		for (const auto& button : gamepad_buttons) {
			if (SDL_GetGamepadButton(gamepad_, button.from))
				state.buttons = state.buttons | button.to;
		}
		state.left_trigger = (std::max)(state.left_trigger, TriggerAmount(SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)));
		state.right_trigger = (std::max)(state.right_trigger, TriggerAmount(SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)));
		state.left_x = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFTX);
		state.left_y = FlipY(SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFTY));
		state.right_x = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_RIGHTX);
		state.right_y = FlipY(SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_RIGHTY));
	}

	if (memcmp(&state, &pad_.GetState(), sizeof(state)) != 0)
		pad_.SetState(state);
}

void ControllerBridge::JoystickAdded(SDL_JoystickID id, const std::string& port) {
	if (gamepad_ != nullptr || config_.port.empty() || _stricmp(port.c_str(), config_.port.c_str()) != 0)
		return;
	const char* name = SDL_GetJoystickNameForID(id);
	if (name == NULL)
		name = "(no name)";
	if (!SDL_IsGamepad(id)) {
		printf("[%s] Ignoring %s on %s, it isn't a known controller\n", config_.name.c_str(), name, config_.port.c_str());
		return;
	}
	gamepad_ = SDL_OpenGamepad(id);
	if (gamepad_ == nullptr)
		printf("[%s] Could not open %s: %s\n", config_.name.c_str(), name, SDL_GetError());
	else
		printf("[%s] Reading %s on %s\n", config_.name.c_str(), name, config_.port.c_str());
}

void ControllerBridge::Poll() {
	if (gamepad_ != nullptr && !SDL_GamepadConnected(gamepad_)) {
		printf("[%s] Gamepad unplugged from %s\n", config_.name.c_str(), config_.port.c_str());
		SDL_CloseGamepad(gamepad_);
		gamepad_ = nullptr;
	}

	if (device_ == nullptr && !config_.device.empty() && GetTickCount64() >= next_connect_)
		Connect();
	if (device_ != nullptr) {
		try {
			const BYTE* report;
			ULONG length;
			while (device_->NextReport(&report, &length))
				Apply(report, length);
		} catch (const HidError& error) {
			printf("[%s] %s. Releasing its inputs until it comes back.\n", config_.name.c_str(), error.what());
			device_.reset();
			bound_.clear();
			next_connect_ = GetTickCount64() + RECONNECT_INTERVAL_MS;
			reported_missing_ = true;
		}
	}
	Send();
}

void StartGamepads(const std::string& mappings_path) {
	SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");  // otherwise Ctrl+C never reaches main's handler
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");  // a game will have the focus
	// Xbox controllers report both triggers on one shared HID axis. Raw input gives them a
	// device path to find their port by, and takes separate triggers from XInput. Other
	// controllers, PlayStation ones included, have separate trigger axes of their own.
	SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "1");
	if (!SDL_Init(SDL_INIT_GAMEPAD))
		throw std::runtime_error(std::string("Could not start SDL: ") + SDL_GetError());
	if (SDL_AddGamepadMappingsFromFile(mappings_path.c_str()) < 0)
		printf("WARNING: could not read %s (%s), only controllers SDL knows by itself will be used\n", mappings_path.c_str(), SDL_GetError());
}

void RunPollLoop(const std::vector<std::unique_ptr<ControllerBridge>>& bridges, const std::atomic<bool>& stop) {
	// Windows otherwise decides for itself whether a background process keeps its timer accuracy
	PROCESS_POWER_THROTTLING_STATE throttling = {};
	throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
	throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
	SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &throttling, sizeof(throttling));

	HANDLE timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	if (timer == NULL) {
		printf("WARNING: no high resolution timer on this version of Windows, input may be less responsive\n");
		timeBeginPeriod(1);
	}

	const bool gamepads = SDL_WasInit(SDL_INIT_GAMEPAD) != 0;
	while (!stop) {
		// Also updates SDL's gamepad state, which Poll reads
		SDL_Event event;
		while (gamepads && SDL_PollEvent(&event)) {
			if (event.type != SDL_EVENT_JOYSTICK_ADDED)
				continue;
			std::string port = UsbPortOfInterface(SDL_GetJoystickPathForID(event.jdevice.which));
			for (const std::unique_ptr<ControllerBridge>& bridge : bridges)
				bridge->JoystickAdded(event.jdevice.which, port);
		}
		for (const std::unique_ptr<ControllerBridge>& bridge : bridges)
			bridge->Poll();
		if (timer) {
			LARGE_INTEGER due;
			due.QuadPart = -POLL_INTERVAL_100NS;
			SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE);
			WaitForSingleObject(timer, INFINITE);
		} else {
			Sleep(1);
		}
	}

	if (timer)
		CloseHandle(timer);
	else
		timeEndPeriod(1);
}
