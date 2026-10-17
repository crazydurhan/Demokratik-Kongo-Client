#pragma once

/*
========================================================================
    DemokratikKongo :: ArifGUI host bridge
------------------------------------------------------------------------
    The vendored arifclient ClickGUI (src/base/gui/arifgui) is engine- and
    game-agnostic: it renders from an arif::gui::ClickGUI instance that the
    host fills. This module owns that instance and keeps it in lock-step
    with the live client:

      lifecycle (render thread, overlay GL context current):
        Init      - called at the end of Menu::SetupImgui(): builds the
                    ImGui style, embedded fonts and icon textures.
        Render    - called every frame from Gui::Render(): syncs modules /
                    settings / keybinds / favourites / friends / profiles /
                    stats, then draws the ClickGUI (open + close animation).
        OnGLContextLost / OnGLContextRestored - a fullscreen toggle
                    recreates the overlay GL context, which invalidates the
                    icon textures; these re-upload them.
        Shutdown  - render-thread teardown (icon textures + model).

    Persistence: config.cpp stores the selected GUI theme preset through
    ThemeIndex() / SetThemeIndex(), which are safe to call from any thread
    (the preset is applied by Render()).
========================================================================
*/

namespace ArifGUI
{
    void Init();
    void Render();
    void OnGLContextLost();
    void OnGLContextRestored();
    void Shutdown();

    int  ThemeIndex();
    void SetThemeIndex(int index);
}
