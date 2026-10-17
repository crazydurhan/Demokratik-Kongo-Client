// ArifClient ClickGUI - Vektorel ikon seti.
// Ikon fontu bagimliligi olmadan ImDrawList primitifleriyle cizilir.
#pragma once

#include <imgui.h>

#include "arif/gui/Model.hpp"

namespace arif::gui::icons {

// Ust bar / pencere kontrol ikonlari
enum class UiIcon {
    Home,
    Profiles,
    Search,
    Scripts,
    Gear,
    Refresh,
    Minimize,
    Close,
    More,
    Back,
    Star
};

// size: yaricap benzeri olcek (px). rotation: radyan (refresh/gear icin).
void drawModuleIcon(ImDrawList* drawList, ModuleIcon icon, ImVec2 center, float size, ImU32 color);
void drawUiIcon(ImDrawList* drawList, UiIcon icon, ImVec2 center, float size, ImU32 color, float rotation = 0.0f);

} // namespace arif::gui::icons
