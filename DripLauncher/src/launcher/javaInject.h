#pragma once

#include <Windows.h>

#include <string>

namespace dk {

// Extracts the embedded RuntimeHostInjector.dll into the launcher's temp
// cache (content-hash keyed) and returns its path, or an empty string on
// failure.
std::wstring ensureAgentDll();

struct JavaInjectResult
{
    bool ok = false;
    std::string error;
    std::string detail; // one line for the log
    DWORD exitCode = 0;
};

// Runs the injector agent inside a java.exe host:
//   <javaExe> -agentpath:<agentDll>=pid=<targetPid>;dll=<payloadDll> -version
// The agent performs the remote load and exits with 0 on success; its
// diagnostics go to %APPDATA%\RuntimeHost\inject-agent.log.
JavaInjectResult runJavaAgentInject(const std::wstring& javaExe,
                                    const std::wstring& agentDll,
                                    DWORD targetPid,
                                    const std::wstring& payloadDll);

} // namespace dk
