#include "menu.h"
#include "../base.h"
#include "../moduleManager/moduleManager.h"
#include "../gui/gui.h"
#include "../sdk/sdk.h"
#include "../sdk/strayCache.h"
#include <atomic>

/*
    PROJECTX :: renderMenu
    ----------------------
    Thin shim called by the wglSwapBuffers hook. Computes the per-frame
    delta time and forwards rendering to ClickGUI. Also handles the
    detach / save / load requests the GUI raises through these flags.
*/

bool s_requestDetach = false;

namespace
{
    // While the ClickGUI is open we push Minecraft into a dummy GuiScreen.
    // Vanilla's displayGuiScreen -> setIngameNotInFocus releases the mouse and
    // calls KeyBinding.unPressAllKeys(), so the player stops sprinting/walking
    // the moment the menu appears - exactly like opening chat or the pause
    // screen. Without this the last WASD key stays logically held, because the
    // WndProc hook swallows the matching key-up.
    //
    // The toggle used to issue its JNI call straight from the wglSwapBuffers
    // hook. That thread is the game's render thread, so a CallStaticVoidMethod
    // there could corrupt the JVM; the request is now handed to the cheat
    // thread instead.
    std::atomic<bool> s_desiredInputBlocked{ false };
    bool s_appliedInputBlocked = false;
}

void Menu::RequestGameInputBlocked(bool blocked)
{
    s_desiredInputBlocked.store(blocked, std::memory_order_release);
}

void Menu::ApplyPendingInputBlock()
{
    const bool blocked = s_desiredInputBlocked.load(std::memory_order_acquire);
    if (s_appliedInputBlocked == blocked)
        return;

    if (!StrayCache::EnsureEspBridge())
        return;

    JNIEnv* env = Java::GetEnv();
    if (!env || !StrayCache::espBridge_class || !StrayCache::espBridge_displayDummyScreen)
        return;

    env->CallStaticVoidMethod(StrayCache::espBridge_class,
                              StrayCache::espBridge_displayDummyScreen,
                              static_cast<jboolean>(blocked));
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        return;   // leave the flag untouched so we retry next tick
    }

    s_appliedInputBlocked = blocked;
}

void Menu::RenderMenu()
{
    Menu::RequestGameInputBlocked(Menu::Open);

    Gui::Render();
    Gui::RenderFakeLogin();
    Gui::RenderItemLogger();

    if (s_requestDetach) {
        Menu::RequestGameInputBlocked(false);
        Base::ShuttingDown.store(true, std::memory_order_release);
        Base::SetRunning(false);
        s_requestDetach = false;
    }
}
