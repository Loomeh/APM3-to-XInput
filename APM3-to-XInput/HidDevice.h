#pragma once
// Finds and reads HID game controllers, identified by their device instance path
// (Device Manager > Details > Device instance path), for example
//   HID\VID_0CA3&PID_0021\7&2A3B4C5D&0&0000
//
// Windows includes the USB port in the path of a device with no serial number, so
// moving the device to a different port can change it. Run --list again if it does.
#include <Windows.h>
#include <hidsdi.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

class HidError : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};

// The controls a device reports, as described by the device itself
class HidLayout {
public:
	explicit HidLayout(HANDLE handle);
	~HidLayout();
	HidLayout(const HidLayout&) = delete;
	HidLayout& operator=(const HidLayout&) = delete;

	PHIDP_PREPARSED_DATA preparsed = NULL;
	HIDP_CAPS caps = {};
	std::vector<HIDP_BUTTON_CAPS> buttons;
	std::vector<HIDP_VALUE_CAPS> values;
};

struct HidControllerInfo {
	std::string instance_path;
	std::string product;
	std::string controls;  // readable summary, e.g. "buttons 1-10, axes X Y, hat"
};

// Every connected device Windows treats as a game controller
std::vector<HidControllerInfo> ListGameControllers();

class HidDevice {
public:
	// Throws HidError if the device is not connected or can't be opened
	explicit HidDevice(const std::string& instance_path);
	~HidDevice();
	HidDevice(const HidDevice&) = delete;
	HidDevice& operator=(const HidDevice&) = delete;

	// Gets the next report waiting, without blocking. Returns false if there are none
	// right now. The report stays valid until the next call, and starts with its report
	// ID. Throws HidError if the device has gone away.
	bool NextReport(const BYTE** report, ULONG* length);

	const HidLayout& Layout() const { return *layout_; }

private:
	HANDLE handle_ = INVALID_HANDLE_VALUE;
	std::unique_ptr<HidLayout> layout_;
	OVERLAPPED overlapped_ = {};
	bool pending_ = false;
	std::vector<BYTE> buffer_;
};
