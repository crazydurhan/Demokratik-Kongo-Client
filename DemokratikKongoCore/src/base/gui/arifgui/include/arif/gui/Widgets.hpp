// ArifClient ClickGUI - Ozel cizilmis widget'lar.
// Hepsi ImGui::InvisibleButton + ImDrawList tabanlidir; boylece her piksel
// kontrol edilirken ImGui'nin id/hover/active davranisi korunur.
#pragma once

#include <imgui.h>

#include <string>
#include <vector>

#include "arif/gui/Animation.hpp"

namespace arif::gui::widgets {

// Slider'larin manuel sayi girisi durumu (GUISetting icinde saklanir).
struct EditState {
    bool active = false;
    char buffer[32] = {};
};

// ---------------------------------------------------------------------------
// Modul karti icin animasyonlu toggle switch (akordeon animasyonunu disaridan
// alir; boylece kart ile ayni egride hareket eder).
// ---------------------------------------------------------------------------
bool ToggleSwitch(const char* id, bool* value, AnimationState& anim, float scale = 1.0f, bool whiteStyle = false);

// Alt ayar tiki
bool Checkbox(const char* id, bool* value);

// Yuvarlatilmis cubuk + gradyan dolgu + surukleme.
// Cift tiklayinca manuel sayi girisi acilir (Enter/focus kaybi ile uygular).
bool SliderFloat(const char* id, float* value, float min, float max, const char* format, EditState* edit = nullptr);
bool SliderInt(const char* id, int* value, int min, int max, EditState* edit = nullptr);

// Ozel acilir kutu (popup tabanli)
bool Dropdown(const char* id, int* current, const std::vector<std::string>& options, float width = 0.0f);

// Tus secici: [ NONE ] / [ KEY ] ; sag tik temizler, ESC iptal eder.
bool Keybind(const char* id, int* key, bool* listening, float width = 74.0f);

// Minimal renk secici butonu (tiklayinca popup picker)
bool ColorPicker(const char* id, ImVec4* color);

// Kucuk kapsul buton (Friends/Configs sayfalari)
bool Button(const char* id, const char* label, ImU32 background, ImU32 foreground, ImVec2 size = ImVec2(0.0f, 0.0f));

// Buyutec ikonlu arama cubugu (hint destekli)
bool SearchInput(const char* id, char* buffer, int bufferSize, const char* hint, float width);

// Akordeon oku (aci radyan cinsinden; 0 = asagi, PI = yukari)
void Chevron(ImDrawList* dl, ImVec2 center, float size, float angleRadians, ImU32 color, float thickness = 1.6f);

// Bolum basligi: kucuk buyuk harf etiket + ince ayrac
void SectionHeader(const char* label);

} // namespace arif::gui::widgets
