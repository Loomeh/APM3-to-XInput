#pragma once
// Reads XInput from a separate process (this same exe run with --observer), because
// XInput must never be used in the process that owns the virtual controllers.
#include <Windows.h>

class XInputObserver {
public:
	XInputObserver();
	~XInputObserver();
	XInputObserver(const XInputObserver&) = delete;
	XInputObserver& operator=(const XInputObserver&) = delete;

	bool Started() const { return in_ != NULL; }

	// A bit for each XInput slot with a controller on it
	DWORD ConnectedSlots();

private:
	HANDLE in_ = NULL;
	HANDLE out_ = NULL;
};

// The other process: answers requests until the XInputObserver closes the pipe
int RunXInputObserver();
