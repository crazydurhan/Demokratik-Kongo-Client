#include "keybindUtil.h"

#include <cstdio>

namespace
{
    int scanCodeFromLParam(LPARAM lParam)
    {
        return static_cast<int>((lParam >> 16) & 0xFF);
    }

    int resolveGenericModifier(int vk, LPARAM lParam)
    {
        const int sc = scanCodeFromLParam(lParam);

        if (vk == VK_SHIFT) {
            if (sc == 0x36) return VK_RSHIFT;
            return VK_LSHIFT;
        }
        if (vk == VK_CONTROL) {
            if (sc == 0x1D) return VK_LCONTROL;
            if (sc == 0x9D) return VK_RCONTROL;
            return VK_LCONTROL;
        }
        if (vk == VK_MENU) {
            if (sc == 0x38) return VK_LMENU;
            if (sc == 0xB8) return VK_RMENU;
            return VK_LMENU;
        }

        return vk;
    }
}

namespace KeybindUtil
{
    int ResolveKeyVk(int vk, LPARAM lParam)
    {
        if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU)
            return resolveGenericModifier(vk, lParam);
        return vk;
    }

    bool KeybindMatches(int boundVk, int pressedVk, LPARAM lParam)
    {
        if (boundVk == 0)
            return false;

        const int resolvedPressed = ResolveKeyVk(pressedVk, lParam);
        return boundVk == resolvedPressed;
    }

    const char* FormatKeyName(int vk, char* buf, size_t bufSz)
    {
        if (!buf || bufSz == 0)
            return "";

        if (vk == 0) {
            std::snprintf(buf, bufSz, "-");
        } else if (vk >= 'A' && vk <= 'Z') {
            std::snprintf(buf, bufSz, "%c", vk);
        } else if (vk >= '0' && vk <= '9') {
            std::snprintf(buf, bufSz, "%c", vk);
        } else {
            switch (vk) {
                case VK_LBUTTON:  std::snprintf(buf, bufSz, "LMB");    break;
                case VK_RBUTTON:  std::snprintf(buf, bufSz, "RMB");    break;
                case VK_MBUTTON:  std::snprintf(buf, bufSz, "MMB");    break;
                case VK_XBUTTON1: std::snprintf(buf, bufSz, "MB4");    break;
                case VK_XBUTTON2: std::snprintf(buf, bufSz, "MB5");    break;
                case VK_LSHIFT:   std::snprintf(buf, bufSz, "LShift"); break;
                case VK_RSHIFT:   std::snprintf(buf, bufSz, "RShift"); break;
                case VK_SHIFT:    std::snprintf(buf, bufSz, "Shift");  break;
                case VK_LCONTROL: std::snprintf(buf, bufSz, "LCtrl");   break;
                case VK_RCONTROL: std::snprintf(buf, bufSz, "RCtrl");  break;
                case VK_CONTROL:  std::snprintf(buf, bufSz, "Ctrl");   break;
                case VK_LMENU:    std::snprintf(buf, bufSz, "LAlt");   break;
                case VK_RMENU:    std::snprintf(buf, bufSz, "RAlt");   break;
                case VK_MENU:     std::snprintf(buf, bufSz, "Alt");    break;
                case VK_SPACE:    std::snprintf(buf, bufSz, "SPACE");   break;
                case VK_TAB:      std::snprintf(buf, bufSz, "TAB");     break;
                case VK_RETURN:   std::snprintf(buf, bufSz, "ENTER");   break;
                case VK_INSERT:   std::snprintf(buf, bufSz, "INS");     break;
                case VK_DELETE:   std::snprintf(buf, bufSz, "DEL");     break;
                case VK_HOME:     std::snprintf(buf, bufSz, "HOME");    break;
                case VK_END:      std::snprintf(buf, bufSz, "END");     break;
                case VK_F1: case VK_F2: case VK_F3: case VK_F4:
                case VK_F5: case VK_F6: case VK_F7: case VK_F8:
                case VK_F9: case VK_F10: case VK_F11: case VK_F12:
                    std::snprintf(buf, bufSz, "F%d", vk - VK_F1 + 1);
                    break;
                default:
                    std::snprintf(buf, bufSz, "VK%d", vk);
                    break;
            }
        }
        return buf;
    }
}
