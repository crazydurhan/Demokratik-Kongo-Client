#pragma once

#include <atomic>
#include <string>
#include <Windows.h>

#include "../../../ext/imgui/imgui.h"

/*
    PROJECTX :: Menu (lifecycle / context holder)
    ---------------------------------------------
    All actual GUI rendering lives in gui/ClickGUI. This struct just
    keeps Win32/OpenGL handles, the toggle key, and the open flag that
    the rest of the codebase reads.
*/

struct Menu
{
    static void Init();
    static void Kill();

    static inline std::string Title;
    static inline bool        Open;
    static inline bool        FakeLoginOpen;
    static inline bool        ItemLogOpen;
    static inline int         Keybind;
    static inline ImFont*     Font;
    static inline ImFont*     FontBold;
    static inline ImFont*     FontMono;
    static inline ImFont*     FontIcon;     // glyph font (optional)
    static inline std::atomic<bool> Initialized{false};

    static void SetupImgui();
    static void RenderMenu();              // implemented in renderMenu.cpp

    // Set by Kill() (cheat thread); the actual ImGui/GL teardown runs on the
    // render thread inside the wglSwapBuffers hook (GL contexts are
    // thread-affine). Returns true once teardown has been performed.
    static bool ProcessPendingGLTeardown();
    static inline std::atomic<bool> PendingGLTeardown{false};

    // Pushes / releases Minecraft's dummy GuiScreen so the game stops
    // receiving movement input while our menu is up.
    //
    // Request from any thread (including the wglSwapBuffers hook); the JNI call
    // itself is performed by ApplyPendingInputBlock() on the cheat thread, which
    // is properly attached to the JVM.
    static void RequestGameInputBlocked(bool blocked);
    static void ApplyPendingInputBlock();

    static inline HWND HandleWindow;
    static inline HDC  HandleDeviceContext;

    static inline HGLRC OriginalGLContext;
    static inline HGLRC MenuGLContext;

    static inline ImGuiContext* CurrentImGuiContext;

    static void PlaceHooks();
    static void RemoveHooks();
    static void PrepareShutdown();
    static void ResetHookSetupState();

    static void Hook_wglSwapBuffers();
    static void EnsureRenderHooks();
    static bool RenderHooksInstalled();
    static void Hook_wndProc();

    static void Unhook_wglSwapBuffers();
    static void Unhook_wndProc();
};
