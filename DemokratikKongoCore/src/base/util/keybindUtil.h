#pragma once

#include <Windows.h>
#include <cstddef>

namespace KeybindUtil
{
    int ResolveKeyVk(int vk, LPARAM lParam);
    bool KeybindMatches(int boundVk, int pressedVk, LPARAM lParam);
    const char* FormatKeyName(int vk, char* buf, size_t bufSz);
}
