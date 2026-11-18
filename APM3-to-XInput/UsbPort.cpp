#include "UsbPort.h"

#include <Windows.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <devpkey.h>

#pragma comment(lib, "cfgmgr32.lib")

static std::string Narrow(const std::wstring& text) {
	char buffer[1024] = "";
	WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, buffer, sizeof(buffer) - 1, NULL, NULL);
	return buffer;
}

// A string property, or the first string of a string list property
static std::wstring Property(DEVINST node, const DEVPROPKEY& key) {
	wchar_t value[1024] = L"";
	ULONG size = sizeof(value) - sizeof(wchar_t);
	DEVPROPTYPE type;
	if (CM_Get_DevNode_PropertyW(node, &key, &type, (PBYTE)value, &size, 0) != CR_SUCCESS)
		return L"";
	return value;
}

// The location path of the device itself if it is what's plugged into a USB port: those
// end in USB(n), while a device's interfaces end in USBMI(n) and a root hub in USBROOT(n)
static std::string PortPath(DEVINST node) {
	std::string path = Narrow(Property(node, DEVPKEY_Device_LocationPaths));
	size_t last = path.rfind('#');
	return last != std::string::npos && path.compare(last + 1, 4, "USB(") == 0 ? path : "";
}

std::string UsbPortOfInterface(const char* interface_path) {
	if (interface_path == NULL)
		return "";
	wchar_t path[1024], instance_id[MAX_DEVICE_ID_LEN];
	if (MultiByteToWideChar(CP_UTF8, 0, interface_path, -1, path, ARRAYSIZE(path)) == 0)
		return "";
	ULONG size = sizeof(instance_id);
	DEVPROPTYPE type;
	DEVINST node;
	if (CM_Get_Device_Interface_PropertyW(path, &DEVPKEY_Device_InstanceId, &type, (PBYTE)instance_id, &size, 0) != CR_SUCCESS ||
		CM_Locate_DevNodeW(&node, instance_id, CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
		return "";

	// Walk up from the HID device to the USB device it is part of
	do {
		std::string port = PortPath(node);
		if (!port.empty())
			return port;
	} while (CM_Get_Parent(&node, node, 0) == CR_SUCCESS);
	return "";
}

std::vector<UsbDeviceInfo> ListUsbDevices() {
	std::vector<UsbDeviceInfo> devices;
	const ULONG filter = CM_GETIDLIST_FILTER_ENUMERATOR | CM_GETIDLIST_FILTER_PRESENT;
	std::vector<wchar_t> ids;
	CONFIGRET result;
	do {  // the list can grow between asking its size and reading it
		ULONG length = 0;
		if (CM_Get_Device_ID_List_SizeW(&length, L"USB", filter) != CR_SUCCESS)
			return devices;
		ids.assign(length, L'\0');
		result = CM_Get_Device_ID_ListW(L"USB", ids.data(), length, filter);
	} while (result == CR_BUFFER_SMALL);
	if (result != CR_SUCCESS)
		return devices;

	for (const wchar_t* id = ids.data(); *id; id += wcslen(id) + 1) {
		DEVINST node;
		if (CM_Locate_DevNodeW(&node, (DEVINSTID_W)id, CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
			continue;
		UsbDeviceInfo device;
		device.port = PortPath(node);
		if (device.port.empty())
			continue;
		device.instance_id = Narrow(id);
		std::wstring name = Property(node, DEVPKEY_Device_BusReportedDeviceDesc);
		device.name = Narrow(name.empty() ? Property(node, DEVPKEY_Device_DeviceDesc) : name);
		devices.push_back(device);
	}
	return devices;
}
