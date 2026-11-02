#pragma once
// Reads the INI file. Each section is one virtual controller:
//
//   [Controller1]
//   Device=HID\VID_0CA3&PID_0021\7&2A3B4C5D&0&0000   ; from --list
//   A=1                                              ; button number
//   Left=X-                                          ; axis direction
//   Up=Hat:Up                                        ; hat direction
//   LT=Z                                             ; axis position, triggers only
//
// See APM3-to-XInput.example.ini for everything that can be mapped.
#include <Windows.h>
#include <hidusage.h>
#include <stdexcept>
#include <string>
#include <vector>

#include "VirtualController.h"

class ConfigError : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};

enum class SourceKind { Button, Axis, Hat, AnalogAxis };
enum class Direction { Up, Down, Left, Right, Negative, Positive };

// One input on the HID device
struct InputSource {
	SourceKind kind = SourceKind::Button;
	USAGE usage = 0;                      // button number, or axis usage (0x30 = X)
	Direction direction = Direction::Up;  // for axes and hats
	std::string text;                     // as written in the INI, for error messages
};

// What that input does on the virtual controller: a button, or a trigger, which an
// AnalogAxis pulls as far as the axis has moved and anything else pulls fully
struct InputMapping {
	std::string target;
	Button button = Button::None;
	bool left_trigger = false;
	bool right_trigger = false;
	InputSource source;
};

struct ControllerConfig {
	std::string name;
	std::string device;
	std::vector<InputMapping> mappings;
};

// Throws ConfigError, naming the line, if anything in the file is not understood
std::vector<ControllerConfig> LoadConfig(const std::string& path);
