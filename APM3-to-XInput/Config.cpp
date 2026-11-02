#include "Config.h"

#include <stdlib.h>
#include <string.h>

#include <fstream>

// XInput has four slots, and a controller beyond that silently goes nowhere
static const size_t MAX_CONTROLLERS = 4;

struct TargetName {
	const char* name;
	Button button;
	bool left_trigger;
	bool right_trigger;
};

static const TargetName target_names[] = {
	{ "A", Button::A }, { "B", Button::B }, { "X", Button::X }, { "Y", Button::Y },
	{ "LB", Button::LB }, { "RB", Button::RB }, { "LS", Button::LS }, { "RS", Button::RS },
	{ "Up", Button::Up }, { "Down", Button::Down }, { "Left", Button::Left }, { "Right", Button::Right },
	{ "Start", Button::Menu }, { "Back", Button::View },
	{ "LT", Button::None, true, false }, { "RT", Button::None, false, true },
};

struct AxisName {
	const char* name;
	USAGE usage;
};

static const AxisName axis_names[] = {
	{ "X", HID_USAGE_GENERIC_X }, { "Y", HID_USAGE_GENERIC_Y }, { "Z", HID_USAGE_GENERIC_Z },
	{ "Rx", HID_USAGE_GENERIC_RX }, { "Ry", HID_USAGE_GENERIC_RY }, { "Rz", HID_USAGE_GENERIC_RZ },
	{ "Slider", HID_USAGE_GENERIC_SLIDER }, { "Dial", HID_USAGE_GENERIC_DIAL },
};

static std::string Trim(const std::string& text) {
	const char* blank = " \t\r\n";
	size_t first = text.find_first_not_of(blank);
	if (first == std::string::npos)
		return "";
	return text.substr(first, text.find_last_not_of(blank) - first + 1);
}

static bool ParseSource(const std::string& text, InputSource& source) {
	source.text = text;

	// A button number, as shown in the Windows game controller test
	if (text.find_first_not_of("0123456789") == std::string::npos && text.size() <= 5) {
		int number = atoi(text.c_str());
		if (number < 1 || number > 0xFFFF)
			return false;
		source.kind = SourceKind::Button;
		source.usage = (USAGE)number;
		return true;
	}

	if (_strnicmp(text.c_str(), "Hat:", 4) == 0) {
		const char* direction = text.c_str() + 4;
		source.kind = SourceKind::Hat;
		source.usage = HID_USAGE_GENERIC_HATSWITCH;
		if (_stricmp(direction, "Up") == 0)
			source.direction = Direction::Up;
		else if (_stricmp(direction, "Down") == 0)
			source.direction = Direction::Down;
		else if (_stricmp(direction, "Left") == 0)
			source.direction = Direction::Left;
		else if (_stricmp(direction, "Right") == 0)
			source.direction = Direction::Right;
		else
			return false;
		return true;
	}

	// An axis direction such as X- or Rz+, or a whole axis such as Z
	bool has_direction = !text.empty() && (text.back() == '-' || text.back() == '+');
	std::string axis = has_direction ? text.substr(0, text.size() - 1) : text;
	for (const AxisName& name : axis_names) {
		if (_stricmp(axis.c_str(), name.name) == 0) {
			source.kind = has_direction ? SourceKind::Axis : SourceKind::AnalogAxis;
			source.usage = name.usage;
			source.direction = text.back() == '-' ? Direction::Negative : Direction::Positive;
			return true;
		}
	}
	return false;
}

std::vector<ControllerConfig> LoadConfig(const std::string& path) {
	std::ifstream file(path);
	if (!file)
		throw ConfigError("Could not open " + path);

	std::vector<ControllerConfig> controllers;
	std::string line;
	int line_number = 0;
	auto fail = [&](const std::string& message) {
		throw ConfigError(path + " line " + std::to_string(line_number) + ": " + message);
	};

	while (std::getline(file, line)) {
		line_number++;
		if (line_number == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0)
			line.erase(0, 3);
		size_t comment = line.find_first_of(";#");
		if (comment != std::string::npos)
			line.erase(comment);
		line = Trim(line);
		if (line.empty())
			continue;

		if (line.front() == '[') {
			if (line.back() != ']')
				fail("missing ] in section name");
			ControllerConfig controller;
			controller.name = Trim(line.substr(1, line.size() - 2));
			if (controller.name.empty())
				fail("empty section name");
			for (const ControllerConfig& other : controllers) {
				if (_stricmp(other.name.c_str(), controller.name.c_str()) == 0)
					fail("section [" + controller.name + "] appears twice");
			}
			controllers.push_back(controller);
			if (controllers.size() > MAX_CONTROLLERS)
				fail("more than " + std::to_string(MAX_CONTROLLERS) + " controllers, XInput only has four slots");
			continue;
		}

		size_t equals = line.find('=');
		if (equals == std::string::npos)
			fail("expected Name=Value, found \"" + line + "\"");
		if (controllers.empty())
			fail("\"" + line + "\" must be inside a [section]");
		std::string key = Trim(line.substr(0, equals));
		std::string value = Trim(line.substr(equals + 1));
		if (value.empty())
			fail(key + " has no value");
		ControllerConfig& controller = controllers.back();

		if (_stricmp(key.c_str(), "Device") == 0) {
			if (!controller.device.empty())
				fail("Device appears twice in [" + controller.name + "]");
			controller.device = value;
			continue;
		}

		const TargetName* target = NULL;
		for (const TargetName& name : target_names) {
			if (_stricmp(key.c_str(), name.name) == 0)
				target = &name;
		}
		if (target == NULL)
			fail("\"" + key + "\" is not a controller button");
		for (const InputMapping& other : controller.mappings) {
			if (other.target == target->name)
				fail(key + " appears twice in [" + controller.name + "]");
		}

		InputMapping mapping;
		mapping.target = target->name;
		mapping.button = target->button;
		mapping.left_trigger = target->left_trigger;
		mapping.right_trigger = target->right_trigger;
		if (!ParseSource(value, mapping.source))
			fail("\"" + value + "\" is not a button number, axis direction (X-), hat direction (Hat:Up) or, for LT and RT, an axis (Z)");
		if (mapping.source.kind == SourceKind::AnalogAxis && !mapping.left_trigger && !mapping.right_trigger)
			fail(key + " is on or off, so it needs a direction: " + value + "+ or " + value + "-");
		controller.mappings.push_back(mapping);
	}

	if (controllers.empty())
		throw ConfigError(path + ": no controllers defined");
	for (const ControllerConfig& controller : controllers) {
		if (controller.device.empty())
			throw ConfigError(path + ": [" + controller.name + "] has no Device=");
		if (controller.mappings.empty())
			throw ConfigError(path + ": [" + controller.name + "] has no buttons mapped");
	}
	return controllers;
}
