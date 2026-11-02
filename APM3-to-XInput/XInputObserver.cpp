#include "XInputObserver.h"

#include <Xinput.h>
#include <string.h>

int RunXInputObserver() {
	typedef DWORD (WINAPI *XInputGetState_t)(DWORD, XINPUT_STATE*);
	HMODULE xinput = LoadLibraryA("xinput1_4.dll");
	XInputGetState_t get_state = xinput ? (XInputGetState_t)GetProcAddress(xinput, "XInputGetState") : NULL;
	if (get_state == NULL)
		return 1;

	HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
	HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
	BYTE request;
	DWORD transferred;
	while (ReadFile(in, &request, sizeof(request), &transferred, NULL) && transferred == sizeof(request)) {
		DWORD slots = 0;
		for (DWORD slot = 0; slot < XUSER_MAX_COUNT; slot++) {
			XINPUT_STATE state;
			if (get_state(slot, &state) == ERROR_SUCCESS)
				slots |= 1 << slot;
		}
		if (!WriteFile(out, &slots, sizeof(slots), &transferred, NULL))
			break;
	}
	return 0;
}

XInputObserver::XInputObserver() {
	SECURITY_ATTRIBUTES inherit = { sizeof(inherit), NULL, TRUE };
	HANDLE child_in, child_out, in, out;
	if (!CreatePipe(&child_in, &in, &inherit, 0))
		return;
	if (!CreatePipe(&out, &child_out, &inherit, 0)) {
		CloseHandle(child_in);
		CloseHandle(in);
		return;
	}
	SetHandleInformation(in, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(out, HANDLE_FLAG_INHERIT, 0);

	char command[MAX_PATH + 32] = "\"";
	GetModuleFileNameA(NULL, command + 1, MAX_PATH);
	strcat_s(command, "\" --observer");

	STARTUPINFOA startup = { sizeof(startup) };
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = child_in;
	startup.hStdOutput = child_out;
	startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
	PROCESS_INFORMATION info;
	BOOL started = CreateProcessA(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &info);
	CloseHandle(child_in);
	CloseHandle(child_out);
	if (!started) {
		CloseHandle(in);
		CloseHandle(out);
		return;
	}
	CloseHandle(info.hProcess);
	CloseHandle(info.hThread);
	in_ = in;
	out_ = out;
}

XInputObserver::~XInputObserver() {
	if (in_) {
		CloseHandle(in_);
		CloseHandle(out_);
	}
}

DWORD XInputObserver::ConnectedSlots() {
	BYTE request = 0;
	DWORD slots = 0, transferred;
	if (in_ == NULL || !WriteFile(in_, &request, sizeof(request), &transferred, NULL) ||
		!ReadFile(out_, &slots, sizeof(slots), &transferred, NULL) || transferred != sizeof(slots))
		return 0;
	return slots;
}
