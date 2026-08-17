#pragma once

#include <cstdint>

enum class NameScheme : uint8_t
{
	Unknown,
	McpNamed,
	SrgMembers,
	Notch,
	Mixed
};

namespace NameSchemeDetect
{
	void Init();
	NameScheme Scheme();
	const char* SchemeName(NameScheme scheme);
	const char* NotchAlias(const char* mcpSlash);
	const char* McpFromNotch(const char* notch);
}
