#pragma once
// Identifies USB ports by the location path Windows gives the device plugged into them,
// for example PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(3). It names the physical port, so it
// stays the same whatever is plugged in and across reboots.
#include <string>
#include <vector>

struct UsbDeviceInfo {
	std::string instance_id;
	std::string name;
	std::string port;
};

// The port of the USB device that a device interface (such as an SDL joystick path)
// belongs to, or "" if it isn't on USB
std::string UsbPortOfInterface(const char* interface_path);

// Every USB device plugged in right now, one per port
std::vector<UsbDeviceInfo> ListUsbDevices();
