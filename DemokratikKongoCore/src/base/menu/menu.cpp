#include "menu.h"

#include "../base.h"
#include "../gui/gui.h"

#include "../../../ext/imgui/imgui.h"
#include "../../../ext/imgui/imgui_internal.h"
#include "../../../ext/imgui/imgui_impl_win32.h"
#include "../../../ext/imgui/imgui_impl_opengl2.h"

/*
========================================================================
    PROJECTX :: Menu shell
------------------------------------------------------------------------
    The legacy `Menu` namespace used to host every widget helper plus a
    monolithic ImGui menu. After the rebuild, all GUI-rendering work is
    owned by `gui/clickgui.{h,cpp}`. This file now only:
       - tracks the menu open state and the toggle keybind (Insert)
       - holds the OpenGL context handles used by the wglSwapBuffers hook
       - manages the imgui hook lifecycle
========================================================================
*/

void Menu::Init()
{
    Menu::Title       = "DemokratikKongo";
    Menu::Initialized = false;
    Menu::Open          = false;
    Menu::FakeLoginOpen = false;
    Menu::ItemLogOpen   = false;
    Menu::Keybind       = VK_INSERT;

    Menu::PlaceHooks();
}

void Menu::PrepareShutdown()
{
    Menu::Open = false;
    Menu::ItemLogOpen = false;

    // Hand control back to the game before we tear anything down. PrepareShutdown
    // runs on the cheat thread, so applying the JNI call inline here is safe.
    Menu::RequestGameInputBlocked(false);
    Menu::ApplyPendingInputBlock();

    if (Menu::HandleWindow && IsWindow(Menu::HandleWindow))
    {
        Menu::Unhook_wndProc();
    }
}

void Menu::Kill()
{
    Menu::Open = false;
    Menu::ItemLogOpen = false;

    Menu::RemoveHooks();

    Gui::Shutdown();

    if (Menu::Initialized && Menu::CurrentImGuiContext)
    {
        ImGui::SetCurrentContext(Menu::CurrentImGuiContext);
        ImGui_ImplOpenGL2_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(Menu::CurrentImGuiContext);
        Menu::CurrentImGuiContext = nullptr;
    }

    if (Menu::MenuGLContext && Menu::HandleDeviceContext)
    {
        wglMakeCurrent(Menu::HandleDeviceContext, Menu::OriginalGLContext);
        wglDeleteContext(Menu::MenuGLContext);
        Menu::MenuGLContext = nullptr;
    }

    Menu::Initialized = false;
    Menu::ResetHookSetupState();
}

void Menu::PlaceHooks()
{
    // Hooks are installed lazily from the cheat loop once opengl32 is loaded.
}

void Menu::RemoveHooks()
{
    Menu::Unhook_wndProc();
    Menu::Unhook_wglSwapBuffers();
}
