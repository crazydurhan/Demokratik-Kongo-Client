#include "../menu.h"

#include "../../base.h"
#include "../../moduleManager/moduleManager.h"
#include "../../moduleManager/modules/combat/friends.h"
#include "../../moduleManager/modules/combat/combatBridge.h"
#include "../../moduleManager/modules/misc/macros.h"
#include "../../util/keybindUtil.h"

#include <atomic>
#include "../../sdk/sdk.h"
#include "../../sdk/strayCache.h"
#include "../../sdk/jniResolve.h"

#include "../../../../ext/imgui/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

typedef LRESULT(CALLBACK* template_WndProc) (HWND, UINT, WPARAM, LPARAM);
template_WndProc original_wndProc;

// True when the player is freely playing - i.e. in a world AND no Minecraft
// GUI (inventory, chat, escape, main menu, ...) is currently active.
static bool isPlayerInGame()
{
    if (!SDK::Minecraft)            return false;
    if (!SDK::Minecraft->thePlayer) return false;       // title / pre-init
    if (SDK::Minecraft->IsInGuiState()) return false;   // any MC screen open
    return true;
}

static bool canSafelyOpenMenuNow()
{
    if (!isPlayerInGame())
        return false;

    JNIEnv* env = Java::GetEnv();
    if (!env || !SDK::Minecraft)
        return false;

    jobject world = SDK::Minecraft->GetTheWorldObject();
    jobject player = SDK::Minecraft->GetThePlayerObject();
    jobject controller = SDK::Minecraft->GetPlayerControllerObject();
    const bool ok = (world != nullptr && player != nullptr && controller != nullptr);

    if (world) env->DeleteLocalRef(world);
    if (player) env->DeleteLocalRef(player);
    if (controller) env->DeleteLocalRef(controller);
    return ok;
}

// Module / macro hotkeys should only fire during normal gameplay input.
static bool shouldRouteModuleKeybinds()
{
    if (Menu::Open || Menu::FakeLoginOpen || Menu::ItemLogOpen)
        return false;
    if (!SDK::Minecraft)
        return false;
    if (SDK::Minecraft->IsInGuiState())
        return false;
    if (!SDK::Minecraft->HasInGameFocus())
        return false;
    return true;
}

static void toggleDummyScreen(bool open)
{
    if (!StrayCache::EnsureEspBridge()) return;
    JNIEnv* env = Java::Env;
    if (!env) return;
    if (!StrayCache::espBridge_class || !StrayCache::espBridge_displayDummyScreen) return;

    env->CallStaticVoidMethod(StrayCache::espBridge_class, StrayCache::espBridge_displayDummyScreen, (jboolean)open);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
}

static bool isMultiplayerScreen()
{
    if (!SDK::Minecraft || !SDK::Minecraft->GetInstance()) return false;
    if (!StrayCache::EnsureEspBridge()) return false;
    JNIEnv* env = Java::Env;
    if (!env) return false;
    if (!StrayCache::espBridge_class || !StrayCache::espBridge_isMultiplayerScreen) return false;

    jboolean res = env->CallStaticBooleanMethod(StrayCache::espBridge_class, StrayCache::espBridge_isMultiplayerScreen);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    return res;
}

LRESULT CALLBACK hook_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (Base::ShuttingDown.load(std::memory_order_acquire) || !Base::IsRunning())
    {
        if (original_wndProc)
            return CallWindowProc(original_wndProc, hwnd, msg, wParam, lParam);
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    static int s_closingKey = 0;

    // Swallow trailing messages from the key used to close the menu so MC
    // does not open pause/chat/etc. on the same press.
    if (s_closingKey != 0)
    {
        if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && (int)wParam == s_closingKey)
            return true;
        if (msg == WM_KEYUP && (int)wParam == s_closingKey)
        {
            s_closingKey = 0;
            return true;
        }
        if (msg == WM_CHAR)
        {
            int c = (int)wParam;
            if (tolower(c) == tolower(s_closingKey) || c == s_closingKey)
                return true;
        }
    }

    if (msg == WM_LBUTTONDOWN)
    {
        // Reach module not included in initial DemokratikKongo build
    }
    if (msg == WM_MBUTTONDOWN)
    {
        // Gated by the Friends module's "Middle click" setting.
        // Use a C++ raycast that never skips friends — Piercing rewrites
        // objectMouseOver so GetMouseOver() cannot hit friends while active.
        if (!Menu::Open && isPlayerInGame() && Friends::MiddleClickEnabled())
        {
            // GetName() allocates a jstring plus intermediate objects. Bounding
            // this in a frame means a stray leak here cannot accumulate across
            // clicks on the game's own message-pump thread.
            JNIEnv* env = Java::GetEnv();
            JniResolve::LocalFrame frame(env, 32);

            CombatBridge::EntityRayHit hit = CombatBridge::RaycastEntity(3.0f, /*skipFriends=*/false);
            if (hit.hit && hit.entity)
            {
                if (env && StrayCache::entityPlayer_class
                    && env->IsInstanceOf(hit.entity, StrayCache::entityPlayer_class))
                {
                    CEntity entity(hit.entity);
                    std::string name = entity.GetName();
                    if (!name.empty())
                        Friends::ToggleFriend(name);
                }
                if (env) env->DeleteLocalRef(hit.entity);
            }
        }
    }
    if (msg == WM_KEYDOWN)
    {
        // The menu key is bound from the Settings page (top bar) and stored
        // in Menu::Keybind (default: Insert).
        if (KeybindUtil::KeybindMatches(Menu::Keybind ? Menu::Keybind : VK_INSERT, (int)wParam, lParam))
        {
            if (Menu::Open || Menu::FakeLoginOpen || Menu::ItemLogOpen)
            {
                Menu::Open = false;
                Menu::FakeLoginOpen = false;
                Menu::ItemLogOpen = false;
            }
            else if (canSafelyOpenMenuNow())
            {
                Menu::Open = true;
            }
            else if (isMultiplayerScreen())
            {
                // In the server list the menu key opens the Fake Login screen
                // instead of the full ClickGUI, so cracked users can type a
                // name and join offline/cracked servers with it.
                Menu::FakeLoginOpen = true;
            }
            return true;
        }
        else if (wParam == VK_ESCAPE && (Menu::Open || Menu::FakeLoginOpen || Menu::ItemLogOpen))
        {
            s_closingKey = (int)wParam;
            Menu::Open = false;
            Menu::FakeLoginOpen = false;
            Menu::ItemLogOpen = false;
            return true;
        }
        else if (shouldRouteModuleKeybinds())
        {
            Macros::OnHotkey((int)wParam, lParam);
            ModuleManager::OnKey((int)wParam, true, lParam);
        }
    }
    else if (msg == WM_KEYUP)
    {
        if (shouldRouteModuleKeybinds())
            ModuleManager::OnKey((int)wParam, false, lParam);
    }

    if ((Menu::Open || Menu::FakeLoginOpen || Menu::ItemLogOpen) && Menu::Initialized)
    {
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);

        switch (msg)
        {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
        case WM_KEYDOWN: case WM_KEYUP:
        case WM_SYSKEYDOWN: case WM_SYSKEYUP:
        case WM_CHAR: case WM_SYSCHAR:
        case WM_UNICHAR:
        case WM_SETCURSOR:
            return true;
        default:
            break;
        }
    }

    if (!original_wndProc)
        return DefWindowProc(hwnd, msg, wParam, lParam);
    return CallWindowProc(original_wndProc, hwnd, msg, wParam, lParam);
}

static HWND s_hookedWindow = nullptr;

void Menu::Hook_wndProc()
{
    if (!Menu::HandleWindow || !IsWindow(Menu::HandleWindow))
        return;

    // Never re-hook a window that already points at hook_WndProc: doing so
    // stored our own hook as the "original", causing infinite recursion and
    // restoring the hook onto itself during unhook.
    if ((template_WndProc)GetWindowLongPtr(Menu::HandleWindow, GWLP_WNDPROC) == (template_WndProc)hook_WndProc)
    {
        s_hookedWindow = Menu::HandleWindow;
        return;
    }

    // Moving to a new window (fullscreen toggle recreates the HWND): restore
    // the previous window's proc first so it never keeps a dangling hook.
    if (s_hookedWindow && s_hookedWindow != Menu::HandleWindow
        && IsWindow(s_hookedWindow) && original_wndProc
        && (template_WndProc)GetWindowLongPtr(s_hookedWindow, GWLP_WNDPROC) == (template_WndProc)hook_WndProc)
    {
        SetWindowLongPtr(s_hookedWindow, GWLP_WNDPROC, (LONG_PTR)original_wndProc);
    }

    template_WndProc previous = (template_WndProc)SetWindowLongPtr(Menu::HandleWindow, GWLP_WNDPROC, (LONG_PTR)hook_WndProc);
    if (previous != (template_WndProc)hook_WndProc)
        original_wndProc = previous;
    s_hookedWindow = Menu::HandleWindow;
}

void Menu::Unhook_wndProc()
{
    HWND target = s_hookedWindow ? s_hookedWindow : Menu::HandleWindow;
    if (target && IsWindow(target) && original_wndProc
        && (template_WndProc)GetWindowLongPtr(target, GWLP_WNDPROC) == (template_WndProc)hook_WndProc)
    {
        SetWindowLongPtr(target, GWLP_WNDPROC, (LONG_PTR)original_wndProc);
    }
    original_wndProc = nullptr;
    s_hookedWindow = nullptr;
}
