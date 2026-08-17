#pragma once

#include <string>
#include <vector>

// Loads MCP + obfuscated member aliases from bundled / external mapping files.
// Used by StrayCache to resolve JNI IDs on Lunar, OptiFine, Forge, and vanilla.
struct MappingLoader
{
	static void Init();
	static const std::vector<std::string>& Names(const char* mcpName);
	// Registers an extra alias for an MCP name (used by MappedRegistry to feed
	// the extracted Vape/MCP SRG + Notch aliases into the shared table).
	static void AddAlias(const char* mcpName, const char* alt);
};
