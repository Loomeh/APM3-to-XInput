#pragma once
// A virtual Xbox One gamepad that shows up to games through XInput.
//
//   VirtualController pad;                      // created and plugged in
//   pad.Press(Button::A | Button::LB);          // input is sent immediately
//   pad.Release(Button::A);
//   pad.SetLeftStick(32767, 0);
//   pad.SetRightTrigger(65535);
//                                               // unplugged when pad goes out of scope
//
// The process must run as administrator with Windows Developer Mode turned on.
// All failures throw VirtualControllerError.
//
// The constructor blocks for about half a second, because sending input before
// Windows has registered a new controller can crash the process. Create
// controllers at startup, not somewhere that can't afford the wait.
//
// XInput has four slots. A controller beyond that is created without an error
// but its input goes nowhere, and nothing here can detect that.
//
// Do not call XInput in the same process as a VirtualController: while polling,
// XInput's I/O reached xboxgipsynthetic.dll's worker threads and crashed the
// process. Read XInput from a separate process instead (see XInputObserver.h).
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

// The complete state of the gamepad. This is also the report format
// xboxgipsynthetic.dll expects, so the layout must not change.
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

	ULONGLONG handle_ = 0;
	bool open_ = false;
	GamepadState state_;
};
