#include "main.h"
#include "attachEntry.h"
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

		// Both load paths (CRT LoadLibrary and JVMTI attach) converge here;
		// the guard inside StartFromDllMain keeps a single bootstrap.
		AttachEntry::StartFromDllMain(hModule);
	}

	return TRUE;
}