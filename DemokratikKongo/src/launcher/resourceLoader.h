#pragma once

#include <Windows.h>
#include <string>

namespace dk {

// Resolve inject payload: sibling RuntimeHostCore.dll if present, else embedded -> TEMP cache.
std::wstring ensureEmbeddedPayload();

// Force re-extract from embedded resource (e.g. after AV quarantine).
std::wstring forceExtractEmbeddedPayload();

// Size of the DLL embedded in this EXE (0 if missing / corrupt build).
DWORD embeddedPayloadSize();

} // namespace dk
