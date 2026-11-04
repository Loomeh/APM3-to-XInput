#pragma once
// A virtual Xbox 360 gamepad, created through ViGEmBus, that shows up to games through XInput.
//
//   VirtualController pad;                      // created and plugged in
//   pad.Press(Button::A | Button::LB);          // input is sent immediately
//   pad.Release(Button::A);
//   pad.SetLeftStick(32767, 0);
//   pad.SetRightTrigger(65535);
//                                               // unplugged when pad goes out of scope
//
// The ViGEmBus driver must be installed (https://github.com/nefarius/ViGEmBus/releases).
// All failures throw VirtualControllerError.
//
// The constructor blocks briefly so Windows can finish registering the new
// controller before input arrives. Create controllers at startup.
//
// XInput has four slots. A controller beyond that is created without an error
// but its input goes nowhere.
#include <Windows.h>
#include <stdexcept>

enum class Button : USHORT {
	None  = 0x0000,
	Menu  = 0x0004,
	View  = 0x0008,
	A     = 0x0010,
	B     = 0x0020,
	X     = 0x0040,
	Y     = 0x0080,
	Up    = 0x0100,
	Down  = 0x0200,
	Left  = 0x0400,
	Right = 0x0800,
	LB    = 0x1000,
	RB    = 0x2000,
	LS    = 0x4000,
	RS    = 0x8000,
};

constexpr Button operator|(Button a, Button b) { return (Button)((USHORT)a | (USHORT)b); }
constexpr Button operator&(Button a, Button b) { return (Button)((USHORT)a & (USHORT)b); }
constexpr Button operator~(Button a) { return (Button)(~(USHORT)a); }

// The complete state of the gamepad. Buttons use the Xbox One bit layout above;
// VirtualController converts to the Xbox 360 (XUSB) layout for ViGEm.
#pragma pack(push, 1)
struct GamepadState {
	Button buttons = Button::None;
	USHORT left_trigger = 0;  // 0-65535 (XInput sees the top 8 bits)
	USHORT right_trigger = 0; // 0-65535
	SHORT left_x = 0, left_y = 0;   // -32768 to 32767, positive is right/up
	SHORT right_x = 0, right_y = 0;
};
#pragma pack(pop)
static_assert(sizeof(GamepadState) == 14);

class VirtualControllerError : public std::runtime_error {
public:
	VirtualControllerError(const char* action, int code);
	int code() const { return code_; }
private:
	int code_;
};

class VirtualController {
public:
	VirtualController();
	~VirtualController();

	VirtualController(const VirtualController&) = delete;
	VirtualController& operator=(const VirtualController&) = delete;
	VirtualController(VirtualController&& other) noexcept;
	VirtualController& operator=(VirtualController&& other) noexcept;

	// Hold or let go of one or more buttons, leaving the others as they are
	void Press(Button buttons);
	void Release(Button buttons);

	void SetLeftTrigger(USHORT value);
	void SetRightTrigger(USHORT value);
	void SetLeftStick(SHORT x, SHORT y);
	void SetRightStick(SHORT x, SHORT y);

	// Replace everything at once in a single report, e.g. once per polling loop
	void SetState(const GamepadState& state);
	const GamepadState& GetState() const { return state_; }

	// Let go of all buttons and center the triggers and sticks
	void Reset();

private:
	void Close() noexcept;

	static const DWORD SETTLE_TIME_MS = 200;

	void* target_ = nullptr;  // PVIGEM_TARGET
	bool open_ = false;
	GamepadState state_;
};
