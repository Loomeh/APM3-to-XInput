#pragma once
// Public API for GipSyntheticEx (https://github.com/InvoxiPlayGames/GipSyntheticEx),
// compiled directly into this program instead of being loaded as a DLL.
// The calling process must be running as administrator.
#include <Windows.h>

// The GipSynthEx library has not been started.
#define GIPSYNTHEX_ERROR_NOT_STARTED 0x8B000001

// The GipSynthEx library has already been started.
#define GIPSYNTHEX_ERROR_ALREADY_STARTED 0x8B000002

// ConnectEx can not be called if the library could not initialise properly.
#define GIPSYNTHEX_ERROR_NO_CONNECTEX 0x8B000003

// The GipSynthEx library failed to start because it can't load the DLL.
#define GIPSYNTHEX_ERROR_CANT_FIND_DLL 0x8B000004

// The GipSynthEx lbirary failed to start because it can't import the functions.
#define GIPSYNTHEX_ERROR_CANT_FIND_FUNCTIONS 0x8B000005

// Failed to set the arrival data because it was invalid.
#define GIPSYNTHEX_ERROR_INVALID_ARRIVAL 0x8B000006

// Failed to set the metadata because it was invalid.
#define GIPSYNTHEX_ERROR_INVALID_METADATA 0x8B000007

// The library was successfully started.
#define GIPSYNTHEX_STARTED_SUCCESSFULLY 0

// The library was started, but couldn't hook xboxgipsynthetic.dll, so ConnectEx is unavailable
#define GIPSYNTHEX_STARTED_WITHOUT_CONNECTEX 1

extern "C" {
	int GipSynthEx_Startup();
	int GipSynthEx_CreateController(int type, ULONGLONG* controller_handle);
	int GipSynthEx_Connect(ULONGLONG controller_handle);
	int GipSynthEx_ConnectEx(ULONGLONG controller_handle, BYTE* arrival, int arrival_size, BYTE* metadata, int metadata_size);
	int GipSynthEx_SendReport(ULONGLONG controller_handle, ULONG report_type, BYTE* report_buf, unsigned int report_size);
	int GipSynthEx_Disconnect(ULONGLONG controller_handle);
	int GipSynthEx_RemoveController(ULONGLONG controller_handle);
}
