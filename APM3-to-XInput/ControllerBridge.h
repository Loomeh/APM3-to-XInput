#pragma once
// Connects one HID device, and whatever known gamepad is plugged into a USB port, to one
// virtual controller, following a section of the INI. Either source may be left out.
#include <Windows.h>
#include <SDL3/SDL_gamepad.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "Config.h"
#include "HidDevice.h"
#include "VirtualController.h"

class ControllerBridge {
public:
	// Creates the virtual controller, so it blocks like VirtualController does. The HID
	// device may be missing: it is retried from Poll() until it shows up. Throws
	// ConfigError if the device doesn't have a control the INI maps.
	explicit ControllerBridge(const ControllerConfig& config);
	~ControllerBridge();

	// Handles every report waiting, without blocking. Sends to the virtual controller
	// only when a mapped input changed, so unmapped inputs cost next to nothing.
	void Poll();

	// Takes the joystick if it is on this controller's port and the port has nothing in
	// use yet. Joysticks SDL has no gamepad mapping for are ignored.
	void JoystickAdded(SDL_JoystickID id, const std::string& port);

private:
	// A mapping tied to where its input lives in this device's reports
	struct BoundMapping {
		InputMapping mapping;
		UCHAR report_id = 0;
		long long minimum = 0, maximum = 0;  // axes and hats
		int bit_size = 0;
		bool active = false;
		USHORT trigger_amount = 65535;  // how far `active` pulls a trigger
	};

	void Connect();
	void Bind();
	void Apply(const BYTE* report, ULONG length);
	void Send();

	ControllerConfig config_;
	VirtualController pad_;
	std::unique_ptr<HidDevice> device_;
	std::vector<BoundMapping> bound_;
	std::vector<USAGE> pressed_;
	ULONGLONG next_connect_ = 0;
	bool reported_missing_ = false;
	SDL_Gamepad* gamepad_ = nullptr;
};

// Starts SDL's gamepad support and adds the mappings in gamecontrollerdb.txt, if found.
// Only needed when some controller has a Port. Throws std::runtime_error on failure.
void StartGamepads(const std::string& mappings_path);

// Polls about 670 times a second on a high resolution timer until `stop` is set.
// Sleep isn't used for this because Windows can stretch it to about 15 ms when this
// process is in the background, which it will be while a game is running.
void RunPollLoop(const std::vector<std::unique_ptr<ControllerBridge>>& bridges, const std::atomic<bool>& stop);
