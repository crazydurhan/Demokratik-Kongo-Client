#pragma once

// Unique 1.8.9 string constants used only as a last-resort class seed.
// Lunar's patched JVM often denies GetConstantPool; Client-thread seed is preferred.
namespace StringFingerprints
{
	inline constexpr const char* kMinecraft[] = {
		"Manually triggered debug crash",
		"Setting user: ",
		"########## GL ERROR ##########",
		"textures/gui/title/mojang.png",
	};
}
