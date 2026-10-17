// ArifClient ClickGUI - Sayfa cizimleri.
#pragma once

#include <string>

#include "arif/gui/Model.hpp"

namespace arif::gui::panels {

// Modul satiri: [ikon karesi] [baslik] [ozet] ....... [toggle]
float moduleRow(GUIModule& module, float width, GUIModule*& selected);

// Tek sutun modul listesi (Home / kategori / favoriler)
void modulesPage(ModuleRegistry& registry, Category category, float width, GUIModule*& selected);

// Scripts sekmesi (placeholder)
void scriptsPage();

// Sag panel (sag kenara yapisik, tam yukseklik, sol koseler yuvarlak):
// module == nullptr ise global "Options", degilse modul ayarlari.
// true donerse kapatma istendi (geri oku).
bool dockPanel(GUIModule* module, ImVec2 size);

// Friends / Target manager
void friendsPage(FriendManager& friends, char* searchBuffer, int searchBufferSize);

// Config / Profile manager
void configsPage(ConfigManager& configs, char* newConfigBuffer, int newConfigBufferSize);

// DK-PORT: dock-styled toggle row exposed so the host application can compose
// the Options overlay with the exact same look (see HostHooks.hpp).
void hostToggleRow(const char* label, bool* value);

// DK-PORT: dock-styled keybind row (id = row id, key = ImGuiKey value).
void hostKeybindRow(const char* id, int* key, bool* listening);

} // namespace arif::gui::panels
