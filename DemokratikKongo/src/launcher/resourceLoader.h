#pragma once

#include <Windows.h>
#include <cstdint>
#include <string>
#include <vector>

namespace dk {

// Resolve inject payload: sibling RuntimeHostCore.dll if present, else embedded -> TEMP cache.
std::wstring ensureEmbeddedPayload();

// Force re-extract from embedded resource (e.g. after AV quarantine).
std::wstring forceExtractEmbeddedPayload();

// Size of the DLL embedded in this EXE (0 if missing / corrupt build).
DWORD embeddedPayloadSize();

// Decoded bytes of the embedded companion agent JAR (empty on failure).
std::vector<uint8_t> embeddedAgentJarBytes();

// Extract the embedded agent JAR (and the DLL-path hint file) next to the
// extracted core so the JVM attach agent can pick it up. Returns the JAR path
// or empty string on failure.
std::wstring ensureEmbeddedAgentJar();

} // namespace dk
