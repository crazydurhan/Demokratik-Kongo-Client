#include "base.h"

#include "moduleManager/moduleManager.h"
#include "moduleManager/commonData.h"
#include "menu/menu.h"

/*
    PROJECTX :: RenderLoop (per-frame 2D pass)
    ------------------------------------------
    Runs inside the wglSwapBuffers hook AFTER the ClickGUI itself, so the
    watermark/render output of modules sit BEHIND the menu (the menu is
    drawn last in renderMenu.cpp). All cheat 2D rendering happens here
    via the ModuleManager.
*/

void Base::RenderLoop()
{
    if (!Base::IsRunning() || Base::ShuttingDown.load(std::memory_order_acquire)) return;

    // Never call JNI from the wglSwapBuffers/render thread — Minecraft client
    // objects are not thread-safe. All entity/camera data is cached on the cheat
    // thread inside CommonData::UpdateData().
    if (!CommonData::DataUpdated())
        return;

    // Overlays also render while a screen (ClickGUI, chat, inventory) is open:
    // the menu is drawn after this pass, so module overlays stay behind it.
    ModuleManager::OnRender2D();
}
