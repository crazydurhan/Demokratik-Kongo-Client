#pragma once

#include <cstdint>

enum class LauncherKind : uint8_t
{
	Unknown,
	Vanilla,
	Forge,
	Lunar,
	Badlion,
	Feather,
	OptiFine
};

namespace LauncherDetection
{
	void Init();
	LauncherKind Kind();
	const char* KindName(LauncherKind kind);
}
