// ArifClient ClickGUI - Tema, palet, tipografi ve cizim yardimcilari.
#pragma once

#include <imgui.h>

namespace arif::gui {

// ---------------------------------------------------------------------------
// Renk paleti: tum arayuz bu tek kaynaktan beslenir.
// Ayarlar sayfasindan accent renkleri calisma aninda degistirilebilir.
// ---------------------------------------------------------------------------
struct Palette {
    ImU32 bgTop     = IM_COL32(15, 15, 18, 255);     // #0F0F12 (tum client zemini - duz)
    ImU32 bgBottom  = IM_COL32(15, 15, 18, 255);     // #0F0F12 (gradyan kaldirildi)
    ImU32 contentBg = IM_COL32(15, 15, 18, 255);     // #0F0F12
    ImU32 panel     = IM_COL32(23, 23, 28, 255);     // #17171C (panel)
    ImU32 panelSoft = IM_COL32(31, 31, 38, 255);     // #1F1F26 (hover)
    ImU32 card      = IM_COL32(27, 27, 33, 255);     // #1B1B21 (acik modul kutusu)
    ImU32 cardHover = IM_COL32(32, 32, 39, 255);     // #202027
    ImU32 outline   = IM_COL32(38, 38, 44, 255);     // #26262C
    ImU32 text      = IM_COL32(255, 255, 255, 255);  // #FFFFFF
    ImU32 textDim   = IM_COL32(138, 138, 147, 255);  // #8A8A93
    ImU32 textMuted = IM_COL32(107, 107, 116, 255);  // #6B6B74
    ImU32 accent    = IM_COL32(47, 128, 237, 255);   // #2F80ED (varsayilan tema - duz renk)
    ImU32 accent2   = IM_COL32(47, 128, 237, 255);   // #2F80ED (temalar duz: accent2 = accent)
    ImU32 success   = IM_COL32(52, 211, 153, 255);   // #34D399
    ImU32 danger    = IM_COL32(248, 113, 113, 255);  // #F87171
    ImU32 warning   = IM_COL32(251, 191, 36, 255);   // #FBBF24
};

Palette& palette();

// ---------------------------------------------------------------------------
// GUI Theme presetleri (dock'taki "GUI Theme" slider'i bunlari gezer)
// ---------------------------------------------------------------------------
struct ThemePreset {
    const char* name;
    ImU32 accent;
    ImU32 accent2;
};

const ThemePreset* themePresets();
int themePresetCount();
int& currentThemeIndex();
void applyThemePreset(int index);

// ---------------------------------------------------------------------------
// Tipografi hiyerarsisi
// ---------------------------------------------------------------------------
struct Fonts {
    ImFont* logo       = nullptr;  // 21px bold      - marka / logo
    ImFont* heading    = nullptr;  // 19px semibold  - inspector basligi
    ImFont* header     = nullptr;  // 14px bold      - kategori basliklari (UPPERCASE + tracking)
    ImFont* title      = nullptr;  // 14px semibold  - modul adi, profil adi
    ImFont* body       = nullptr;  // 14px regular   - genel metin, ayar etiketleri
    ImFont* small      = nullptr;  // 12px regular   - aciklamalar (min 12px)
    ImFont* tiny       = nullptr;  // 12px regular   - rozetler, ipuclari (min 12px)
    ImFont* mono       = nullptr;  // 12px mono      - sayisal degerler, keybind

    // Font Awesome 6 Free (Solid) ikon fontlari - IconPack tarafindan cizilir.
    ImFont* iconBig   = nullptr;  // 40px - modul ikon kareleri
    ImFont* iconMid   = nullptr;  // 24px - ust bar ikonlari
    ImFont* iconSmall = nullptr;  // 16px - kucuk UI ikonlari (geri, yildiz, serit)
};

Fonts& fonts();

// TTF'ler bulunamazsa varsayilan ImGui fontuna duser (crash yok).
void LoadFonts(float uiScale = 1.0f);

// ImGui stil + renklerini paletten yukler.
void SetupGUIStyle();

// Accent renkleri degistiginde ImGui'nin kendi widget renklerini tazeler.
void refreshStyleColors();

// ---------------------------------------------------------------------------
// Geometri
// ---------------------------------------------------------------------------
struct Rect {
    ImVec2 min{};
    ImVec2 max{};

    ImVec2 size() const { return ImVec2(max.x - min.x, max.y - min.y); }
    float  width() const { return max.x - min.x; }
    float  height() const { return max.y - min.y; }
    Rect inset(float px) const {
        return Rect{ImVec2(min.x + px, min.y + px), ImVec2(max.x - px, max.y - px)};
    }
};

// ---------------------------------------------------------------------------
// Renk / metin yardimcilari
// ---------------------------------------------------------------------------
ImU32 mix(ImU32 a, ImU32 b, float t);
ImU32 withAlpha(ImU32 color, float alpha);
ImU32 brighten(ImU32 color, float amount);  // amount: -1..1 (negatif = karart)

constexpr float kPi = 3.14159265358979323846f;

// 8px grid birimi: tum layout olculeri bunun kati olmalidir.
constexpr float kUnit = 8.0f;

ImVec2 measure(ImFont* font, const char* text);

// ---------------------------------------------------------------------------
// Yuvarlatilmis gradyan cizimleri
// (kose yuvarlakligini cap + dilim teknigiyle korur; alpha yerine renk
//  karistirma kullanildigi icin seam olusmaz)
// ---------------------------------------------------------------------------
void drawHGradient(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding,
                   ImU32 left, ImU32 right, int steps = 32);

void drawVGradient(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding,
                   ImU32 top, ImU32 bottom, int steps = 64);

// Karakter bazli yazi gradyani (yalniz ASCII; logo gibi kisa metinler icin).
void gradientText(ImDrawList* dl, ImFont* font, float size, ImVec2 pos,
                  const char* text, ImU32 from, ImU32 to);

// Letter-spacing'li sabit renkli yazi (kategori basliklari icin; ASCII).
void trackedText(ImDrawList* dl, ImFont* font, float size, ImVec2 pos,
                 const char* text, ImU32 color, float tracking);

} // namespace arif::gui
