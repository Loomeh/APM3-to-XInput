#include <Windows.h>
#include <stdio.h>
#include <string.h>

#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "Config.h"
#include "ControllerBridge.h"
#include "HidDevice.h"
#include "XInputObserver.h"

static const DWORD ALL_XINPUT_SLOTS = 0xF;
static const int APPEAR_TIMEOUT_MS = 20000; // about 4 s is normal if Windows has to start XboxGipSvc first

static std::atomic<bool> stop_requested = false;
static HANDLE stopped_event = NULL;

static BOOL WINAPI OnConsoleEvent(DWORD) {
	stop_requested = true;
	// Windows ends the process when this returns, so give the controllers time to unplug
	WaitForSingleObject(stopped_event, 5000);
	return TRUE;
}

static std::string DefaultConfigPath() {
	char path[MAX_PATH];
	GetModuleFileNameA(NULL, path, MAX_PATH);
	std::string result = path;
	return result.substr(0, result.find_last_of('.')) + ".ini";
}

static bool WaitForNewSlot(XInputObserver& observer, DWORD slots_before) {
	ULONGLONG start = GetTickCount64();
	while (!stop_requested && GetTickCount64() - start < APPEAR_TIMEOUT_MS) {
		if (observer.ConnectedSlots() & ~slots_before)
			return true;
		Sleep(50);
	}
	return false;
}

static int RunList() {
	std::vector<HidControllerInfo> controllers = ListGameControllers();
	if (controllers.empty())
		printf("No game controllers connected.\n");
	for (const HidControllerInfo& controller : controllers) {
		printf("%s\n", controller.product.c_str());
		printf("  Device=%s\n", controller.instance_path.c_str());
		printf("  %s\n\n", controller.controls.c_str());
	}
	return 0;
}

static int Run(const std::string& config_path) {
	std::vector<ControllerConfig> configs = LoadConfig(config_path);

	// Windows can report success for a controller that never shows up, so go and look.
	// This process can't use XInput, so the observer is only kept around for startup.
	std::vector<std::unique_ptr<ControllerBridge>> bridges;
	{
		XInputObserver observer;
		if (!observer.Started())
			printf("WARNING: could not start the XInput check, so a controller that fails to appear won't be noticed\n");

		for (const ControllerConfig& config : configs) {
			if (stop_requested)
				break;
			DWORD slots_before = observer.ConnectedSlots();
			// Controllers beyond the limit go nowhere, and may be what breaks new ones until a reboot
			if (slots_before == ALL_XINPUT_SLOTS)
				throw std::runtime_error("[" + config.name + "] All four XInput slots are already in use, unplug a controller first");

			bridges.push_back(std::make_unique<ControllerBridge>(config));
			if (observer.Started() && !WaitForNewSlot(observer, slots_before) && !stop_requested) {
				printf("WARNING: [%s] The virtual controller still isn't in XInput after %d seconds, so games won't see it.\n"
					"         Restart this app, or Windows if that doesn't help. Still running in case it shows up late.\n",
					config.name.c_str(), APPEAR_TIMEOUT_MS / 1000);
			}
		}
	}

	printf("Running %zu controller(s). Press Ctrl+C to stop.\n", bridges.size());
	RunPollLoop(bridges, stop_requested);
	return 0;
}

int main(int argc, char** argv) {
	if (argc > 1 && strcmp(argv[1], "--observer") == 0)
		return RunXInputObserver();

	setvbuf(stdout, NULL, _IONBF, 0);
	stopped_event = CreateEventA(NULL, TRUE, FALSE, NULL);
	SetConsoleCtrlHandler(OnConsoleEvent, TRUE);

	int result = 1;
	try {
		if (argc > 1 && strcmp(argv[1], "--list") == 0)
			result = RunList();
		else if (argc > 2 && strcmp(argv[1], "--config") == 0)
			result = Run(argv[2]);
		else if (argc > 1)
			printf("Usage: APM3-to-XInput [--list | --config <file>]\n");
		else
			result = Run(DefaultConfigPath());
	} catch (const std::exception& error) {
		printf("ERROR: %s\n", error.what());
	}
	SetEvent(stopped_event);

	// Started by double-clicking: keep the window open long enough to read it
	DWORD processes[2];
	if (!stop_requested && GetConsoleProcessList(processes, 2) == 1) {
		printf("Press Enter to close.\n");
		getchar();
	}
	return result;
}
