#include "main.h"
#include "base/util/crash_diagnostics.h"


void Main::Init()
{
	// First thing: initialize crash diagnostics
	CrashDiag::Init();

	// Wrap entire boot sequence in SEH guard
	bool success = CrashDiag::Guard([]() {
		Base::Init();
	}, "Base::Init");

	if (!success) {
		CrashDiag::LogEvent("ERROR", "Main", "Base::Init failed with exception! Check %s", CrashDiag::GetLogFilePath().c_str());
	}
}

void Main::Kill()
{
	CrashDiag::Shutdown();
	Base::Kill();
	FreeLibraryAndExitThread(Main::HModule, 0);
}

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD dwReason, LPVOID lpReserved)
{
	if (dwReason == DLL_PROCESS_ATTACH)
	{
		Main::HModule = hModule;
		DisableThreadLibraryCalls(hModule);

		HANDLE hThread = CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(Main::Init), hModule, 0, nullptr);

		if (hThread) CloseHandle(hThread);
	}

	return TRUE;
}