#include "HidDevice.h"

#include <SetupAPI.h>
#include <cfgmgr32.h>
#include <hidusage.h>
#include <string.h>

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

// How many reports Windows keeps for us between polls (the default is 32)
static const ULONG INPUT_BUFFER_REPORTS = 64;

HidLayout::HidLayout(HANDLE handle) {
	if (!HidD_GetPreparsedData(handle, &preparsed))
		throw HidError("Could not read the device's report layout");
	if (HidP_GetCaps(preparsed, &caps) != HIDP_STATUS_SUCCESS) {
		HidD_FreePreparsedData(preparsed);
		throw HidError("Could not read the device's report layout");
	}

	USHORT count = caps.NumberInputButtonCaps;
	buttons.resize(count);
	if (count > 0 && HidP_GetButtonCaps(HidP_Input, buttons.data(), &count, preparsed) == HIDP_STATUS_SUCCESS)
		buttons.resize(count);
	else
		buttons.clear();

	count = caps.NumberInputValueCaps;
	values.resize(count);
	if (count > 0 && HidP_GetValueCaps(HidP_Input, values.data(), &count, preparsed) == HIDP_STATUS_SUCCESS)
		values.resize(count);
	else
		values.clear();
}

HidLayout::~HidLayout() {
	HidD_FreePreparsedData(preparsed);
}

struct HidInterface {
	std::string path;           // what CreateFile opens
	std::string instance_path;  // what the INI identifies the device by
};

static std::vector<HidInterface> ListHidInterfaces() {
	std::vector<HidInterface> interfaces;
	GUID hid_guid;
	HidD_GetHidGuid(&hid_guid);
	HDEVINFO devices = SetupDiGetClassDevsA(&hid_guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (devices == INVALID_HANDLE_VALUE)
		return interfaces;

	SP_DEVICE_INTERFACE_DATA data = { sizeof(data) };
	for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devices, NULL, &hid_guid, i, &data); i++) {
		DWORD size = 0;
		SetupDiGetDeviceInterfaceDetailA(devices, &data, NULL, 0, &size, NULL);
		if (size == 0)
			continue;
		std::vector<BYTE> storage(size);
		SP_DEVICE_INTERFACE_DETAIL_DATA_A* detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_A*)storage.data();
		detail->cbSize = sizeof(*detail);
		SP_DEVINFO_DATA info = { sizeof(info) };
		if (!SetupDiGetDeviceInterfaceDetailA(devices, &data, detail, size, NULL, &info))
			continue;
		char instance[MAX_DEVICE_ID_LEN] = "";
		if (!SetupDiGetDeviceInstanceIdA(devices, &info, instance, sizeof(instance), NULL))
			continue;
		interfaces.push_back({ detail->DevicePath, instance });
	}
	SetupDiDestroyDeviceInfoList(devices);
	return interfaces;
}

static bool IsGameController(const HIDP_CAPS& caps) {
	return caps.UsagePage == HID_USAGE_PAGE_GENERIC &&
		(caps.Usage == HID_USAGE_GENERIC_JOYSTICK || caps.Usage == HID_USAGE_GENERIC_GAMEPAD ||
			caps.Usage == HID_USAGE_GENERIC_MULTI_AXIS_CONTROLLER);
}

static const char* AxisName(USAGE usage) {
	switch (usage) {
	case HID_USAGE_GENERIC_X: return "X";
	case HID_USAGE_GENERIC_Y: return "Y";
	case HID_USAGE_GENERIC_Z: return "Z";
	case HID_USAGE_GENERIC_RX: return "Rx";
	case HID_USAGE_GENERIC_RY: return "Ry";
	case HID_USAGE_GENERIC_RZ: return "Rz";
	case HID_USAGE_GENERIC_SLIDER: return "Slider";
	case HID_USAGE_GENERIC_DIAL: return "Dial";
	default: return NULL;
	}
}

static std::string DescribeControls(const HidLayout& layout) {
	std::string buttons, axes;
	bool hat = false;
	for (const HIDP_BUTTON_CAPS& caps : layout.buttons) {
		if (caps.UsagePage != HID_USAGE_PAGE_BUTTON)
			continue;
		USAGE first = caps.IsRange ? caps.Range.UsageMin : caps.NotRange.Usage;
		USAGE last = caps.IsRange ? caps.Range.UsageMax : caps.NotRange.Usage;
		if (!buttons.empty())
			buttons += ",";
		buttons += std::to_string(first);
		if (last != first)
			buttons += "-" + std::to_string(last);
	}
	for (const HIDP_VALUE_CAPS& caps : layout.values) {
		if (caps.UsagePage != HID_USAGE_PAGE_GENERIC)
			continue;
		USAGE first = caps.IsRange ? caps.Range.UsageMin : caps.NotRange.Usage;
		USAGE last = caps.IsRange ? caps.Range.UsageMax : caps.NotRange.Usage;
		for (USAGE usage = first; usage <= last && usage != 0; usage++) {
			if (usage == HID_USAGE_GENERIC_HATSWITCH)
				hat = true;
			else if (const char* name = AxisName(usage))
				axes += std::string(axes.empty() ? "" : " ") + name;
		}
	}

	std::string result;
	if (!buttons.empty())
		result += "buttons " + buttons;
	if (!axes.empty())
		result += std::string(result.empty() ? "" : ", ") + "axes " + axes;
	if (hat)
		result += std::string(result.empty() ? "" : ", ") + "hat";
	return result.empty() ? "no controls found" : result;
}

std::vector<HidControllerInfo> ListGameControllers() {
	std::vector<HidControllerInfo> controllers;
	for (const HidInterface& hid : ListHidInterfaces()) {
		// Asking for no access is enough to read the layout and name of any HID device
		HANDLE handle = CreateFileA(hid.path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
		if (handle == INVALID_HANDLE_VALUE)
			continue;
		try {
			HidLayout layout(handle);
			if (IsGameController(layout.caps)) {
				HidControllerInfo info;
				info.instance_path = hid.instance_path;
				info.controls = DescribeControls(layout);
				wchar_t product[127] = L"";
				char narrow[256] = "";
				if (HidD_GetProductString(handle, product, sizeof(product)))
					WideCharToMultiByte(CP_UTF8, 0, product, -1, narrow, sizeof(narrow), NULL, NULL);
				info.product = narrow[0] ? narrow : "(no name)";
				controllers.push_back(info);
			}
		} catch (const HidError&) {
			// Not something we can read, so not something we can use
		}
		CloseHandle(handle);
	}
	return controllers;
}

HidDevice::HidDevice(const std::string& instance_path) {
	std::string path;
	for (const HidInterface& hid : ListHidInterfaces()) {
		if (_stricmp(hid.instance_path.c_str(), instance_path.c_str()) == 0)
			path = hid.path;
	}
	if (path.empty())
		throw HidError("Device not connected: " + instance_path);

	handle_ = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	if (handle_ == INVALID_HANDLE_VALUE)
		throw HidError("Could not open " + instance_path + " (error " + std::to_string(GetLastError()) + ")");

	overlapped_.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
	try {
		if (overlapped_.hEvent == NULL)
			throw HidError("Could not create an event for reading the device");
		layout_ = std::make_unique<HidLayout>(handle_);
		if (layout_->caps.InputReportByteLength == 0)
			throw HidError(instance_path + " does not send any input");
	} catch (...) {
		if (overlapped_.hEvent)
			CloseHandle(overlapped_.hEvent);
		CloseHandle(handle_);
		throw;
	}
	HidD_SetNumInputBuffers(handle_, INPUT_BUFFER_REPORTS);
	buffer_.resize(layout_->caps.InputReportByteLength);
}

HidDevice::~HidDevice() {
	if (pending_) {
		// The read must be finished before the buffer and OVERLAPPED go away
		DWORD transferred;
		CancelIoEx(handle_, &overlapped_);
		GetOverlappedResult(handle_, &overlapped_, &transferred, TRUE);
	}
	CloseHandle(overlapped_.hEvent);
	CloseHandle(handle_);
}

bool HidDevice::NextReport(const BYTE** report, ULONG* length) {
	if (!pending_) {
		if (!ReadFile(handle_, buffer_.data(), (DWORD)buffer_.size(), NULL, &overlapped_) && GetLastError() != ERROR_IO_PENDING)
			throw HidError("Device read failed (error " + std::to_string(GetLastError()) + ")");
		pending_ = true;
	}

	DWORD transferred;
	if (!GetOverlappedResult(handle_, &overlapped_, &transferred, FALSE)) {
		DWORD error = GetLastError();
		if (error == ERROR_IO_INCOMPLETE)
			return false;
		pending_ = false;
		throw HidError("Device read failed (error " + std::to_string(error) + ")");
	}
	pending_ = false;
	*report = buffer_.data();
	*length = transferred;
	return true;
}
