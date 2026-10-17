#include "arif/gui/Panels.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

#include "arif/gui/IconPack.hpp"
#include "arif/gui/Icons.hpp"
#include "arif/gui/Theme.hpp"
#include "arif/gui/Widgets.hpp"

// DK-PORT: host composition hook + product version for the Options header.
#include "arif/gui/HostHooks.hpp"
#include "version.h"

namespace arif::gui::panels {

namespace {

constexpr float kRowGap   = 8.0f;            // modul satirlari arasi
constexpr float kColGap   = kUnit * 2.0f;    // yatay blok boslugu
constexpr float kDockPad  = 14.0f;           // dock ic dolgusu

float clampf(float value, float lo, float hi) {
    return std::min(hi, std::max(lo, value));
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return value;
}

bool containsText(const std::string& haystack, const std::string& needle) {
    return toLower(haystack).find(needle) != std::string::npos;
}

float safeWidth(float width) {
    return std::max(40.0f, width - (ImGui::GetStyle().ScrollbarSize + kUnit));
}

// Surukleme adimi gosterim formatindan turetilir: "%.2f" -> 0.01, "%.1f" -> 0.1.
// Boylece slider'da 0.001'lik oynamalar olmaz; hassas deger icin cift tik ile
// manuel giris kullanilir (dockSliderRow).
float dragStepFromFormat(const char* format) {
    if (format == nullptr) {
        return 0.01f;
    }
    const char* dot = std::strchr(format, '.');
    if (dot == nullptr) {
        return 1.0f;
    }
    int decimals = std::atoi(dot + 1);
    decimals = std::min(4, std::max(0, decimals));
    float step = 1.0f;
    for (int i = 0; i < decimals; ++i) {
        step *= 0.1f;
    }
    return step;
}

// Animasyon durumu ImGuiID ile anahtarlanir (iki widget ayni animasyonu paylasmaz).
AnimationState& keyedAnim(const char* key) {
    static std::unordered_map<unsigned int, AnimationState> anims;
    return anims[ImGui::GetID(key)];
}

AnimationState& keyedAnimId(ImGuiID id) {
    static std::unordered_map<unsigned int, AnimationState> anims;
    return anims[id];
}

// Frame-rate bagimsiz yumusak yaklasim (referans: EaseRate).
void easeTo(float& value, float target, float speed) {
    const float k = 1.0f - std::exp(-speed * animationSpeedScale() * ImGui::GetIO().DeltaTime);
    value += (target - value) * k;
}

// ---------------------------------------------------------------------------
// Dock satir cercevesi (referans: BeginRow / EndRow)
//   - Tum satir tek bir InvisibleButton'dir; kontroller sonradan cizilir.
//   - Ayrac cizgisi satirin ALT kenarina gomulur (dogru konum garanti).
// ---------------------------------------------------------------------------
struct DockRow {
    ImDrawList* dl = nullptr;
    ImGuiID id = 0;
    ImVec2 min;
    ImVec2 max;
    float width = 0.0f;
    bool hovered = false;
    bool active = false;
    bool activated = false;
    bool pressed = false;
    ImVec2 cursorAfter;
};

DockRow dockRowBegin(const char* label, float height, bool allowOverlap = false) {
    DockRow row;
    ImGui::PushID(label);

    row.dl = ImGui::GetWindowDrawList();
    row.min = ImGui::GetCursorScreenPos();
    row.width = std::max(60.0f, ImGui::GetContentRegionAvail().x);
    row.max = ImVec2(row.min.x + row.width, row.min.y + height);
    row.id = ImGui::GetID("##row");

    if (allowOverlap) {
        ImGui::SetNextItemAllowOverlap();
    }
    ImGui::InvisibleButton("##row", ImVec2(row.width, height));
    row.hovered = ImGui::IsItemHovered();
    row.active = ImGui::IsItemActive();
    row.activated = ImGui::IsItemActivated();
    row.pressed = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    row.cursorAfter = ImGui::GetCursorScreenPos();
    return row;
}

void dockRowEnd(const DockRow& row) {
    const float y = std::floor(row.max.y) - 0.5f;
    row.dl->AddLine(ImVec2(row.min.x, y), ImVec2(row.max.x, y),
                    withAlpha(palette().outline, 0.85f), 1.0f);
    ImGui::SetCursorScreenPos(row.cursorAfter);
    ImGui::PopID();
}

struct DockRowState {
    float fill = -1.0f;      // slider dolgusu (eased)
    float knob = 1.0f;       // knob olcegi (eased)
    float valueHl = 0.0f;    // deger metni vurgusu (eased)
    bool grabbing = false;
};

DockRowState& rowState(ImGuiID id) {
    static std::unordered_map<unsigned int, DockRowState> states;
    return states[id];
}

// Cizim-only switch (referans: DrawSwitch) - etkilesim satirin kendisindedir.
void drawSwitchVisual(ImGuiID id, ImVec2 mn, ImVec2 mx, bool on, bool hovered, bool active) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    AnimationState& anim = keyedAnimId(id);
    anim.setTarget(on ? 1.0f : 0.0f);
    anim.update(ImGui::GetIO().DeltaTime);
    const float t = clampf(anim.value(), 0.0f, 1.0f);

    const float radius = (mx.y - mn.y) * 0.5f;
    const ImU32 trackOff = mix(palette().panel, palette().panelSoft, 0.55f);
    const ImU32 track = mix(trackOff, palette().accent, t);
    dl->AddRectFilled(mn, mx, track, radius);
    dl->AddRect(mn, mx, withAlpha(mix(palette().outline, palette().accent, t), 0.85f), radius, 0, 1.0f);

    if (hovered || active) {
        dl->AddRect(ImVec2(mn.x - 1.0f, mn.y - 1.0f), ImVec2(mx.x + 1.0f, mx.y + 1.0f),
                    withAlpha(palette().accent, active ? 0.45f : 0.25f), radius + 1.0f, 0, 2.0f);
    }

    const float knobRadius = radius - 3.0f + (active ? 0.5f : 0.0f);
    const float knobX = mn.x + radius + (mx.x - mn.x - 2.0f * radius) * t;
    const ImVec2 knob(knobX, (mn.y + mx.y) * 0.5f);
    const ImU32 knobColor = mix(mix(palette().textDim, palette().text, 0.30f), IM_COL32(245, 246, 248, 255), t);
    dl->AddCircleFilled(knob, knobRadius, knobColor, 24);
}

// ---------------------------------------------------------------------------
// Dock satir widget'lari
// ---------------------------------------------------------------------------
void dockToggleRow(const char* label, bool* value) {
    DockRow row = dockRowBegin(label, 48.0f);
    const float swW = 40.0f;
    const float swH = 22.0f;
    const float cy = (row.min.y + row.max.y) * 0.5f;

    row.dl->AddText(fonts().body, fonts().body->FontSize,
                    ImVec2(row.min.x, cy - fonts().body->FontSize * 0.5f),
                    palette().textDim, label);

    drawSwitchVisual(row.id, ImVec2(row.max.x - swW, cy - swH * 0.5f),
                     ImVec2(row.max.x, cy + swH * 0.5f), *value, row.hovered, row.active);

    if (row.pressed) {
        *value = !*value;
    }

    dockRowEnd(row);
}

// GUI Theme satiri: etiket + 5 duz renk bandi + knob (drag ile tema degisir)
void dockThemeRow() {
    DockRow row = dockRowBegin("##gui_theme", 56.0f);
    DockRowState& st = rowState(row.id);

    const int count = themePresetCount();
    const float trackH = 6.0f;
    const ImVec2 trackMin(row.min.x, row.max.y - 16.0f);
    const ImVec2 trackMax(row.max.x, trackMin.y + trackH);
    const float knobR = 8.0f;
    const float travelMin = trackMin.x + knobR;
    const float travelSpan = std::max(1.0f, (trackMax.x - trackMin.x) - knobR * 2.0f);

    row.dl->AddText(fonts().body, fonts().body->FontSize,
                    ImVec2(row.min.x, row.min.y + 7.0f), palette().text, "GUI Theme");

    if (row.activated) {
        const float my = ImGui::GetIO().MousePos.y;
        st.grabbing = (my >= trackMin.y - 12.0f && my <= trackMax.y + 12.0f);
    }
    if (!row.active) {
        st.grabbing = false;
    }
    if (st.grabbing && row.active && travelSpan > 1.0f) {
        const float t = clampf((ImGui::GetIO().MousePos.x - travelMin) / travelSpan, 0.0f, 0.9999f);
        const int index = (int)(t * (float)count);
        if (index != currentThemeIndex()) {
            applyThemePreset(index);
        }
    }

    const float target = ((float)currentThemeIndex() + 0.5f) / (float)count;
    if (st.fill < 0.0f) {
        st.fill = target;
    }
    easeTo(st.fill, target, 20.0f);
    easeTo(st.knob, (st.grabbing && row.active) ? 1.25f : 1.0f, 16.0f);

    // 5 duz renk bandi
    const ThemePreset* presets = themePresets();
    const float segW = (trackMax.x - trackMin.x) / (float)count;
    for (int i = 0; i < count; ++i) {
        const float x0 = trackMin.x + (float)i * segW;
        const float x1 = std::min(x0 + segW + 0.5f, trackMax.x);
        row.dl->AddRectFilled(ImVec2(x0, trackMin.y), ImVec2(x1, trackMax.y), presets[i].accent, 3.0f);
    }

    const float knobX = travelMin + travelSpan * clampf(st.fill, 0.0f, 1.0f);
    const ImVec2 knob(knobX, (trackMin.y + trackMax.y) * 0.5f);
    if (row.hovered || row.active) {
        row.dl->AddCircleFilled(knob, 12.0f, withAlpha(palette().accent, 0.18f), 24);
    }
    row.dl->AddCircleFilled(knob, knobR * st.knob, IM_COL32(242, 243, 246, 255), 24);

    dockRowEnd(row);
}

// Sayisal slider satiri: etiket + animasyonlu deger + eased fill + knob + wheel
// + cift tikla manuel giris.
bool dockSliderRow(const char* label, float* value, float minV, float maxV,
                   const char* format, bool integer) {
    DockRow row = dockRowBegin(label, 56.0f);
    DockRowState& st = rowState(row.id);
    bool changed = false;

    const float trackH = 6.0f;
    const ImVec2 trackMin(row.min.x, row.max.y - 16.0f);
    const ImVec2 trackMax(row.max.x, trackMin.y + trackH);
    const float knobR = 8.0f;
    const float travelMin = trackMin.x + knobR;
    const float travelSpan = std::max(1.0f, (trackMax.x - trackMin.x) - knobR * 2.0f);
    const float span = maxV - minV;
    // Surukleme/tekerlek adimi: integer ise 1, degilse format hassasiyeti (0.01 vb.)
    const float step = integer ? 1.0f : dragStepFromFormat(format);

    if (row.activated) {
        const float my = ImGui::GetIO().MousePos.y;
        st.grabbing = (my >= trackMin.y - 12.0f && my <= trackMax.y + 12.0f);
    }
    if (!row.active) {
        st.grabbing = false;
    }

    if (st.grabbing && row.active && span > 0.0f && travelSpan > 1.0f) {
        const float t = clampf((ImGui::GetIO().MousePos.x - travelMin) / travelSpan, 0.0f, 1.0f);
        float next = minV + t * span;
        next = integer ? std::round(next) : std::round(next / step) * step;
        next = clampf(next, minV, maxV);
        if (next != *value) {
            *value = next;
            changed = true;
        }
    }

    if (row.hovered && span > 0.0f) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            float next = *value + wheel * step;
            if (integer) {
                next = std::round(next);
            }
            next = clampf(next, minV, maxV);
            if (next != *value) {
                *value = next;
                changed = true;
            }
        }
    }

    *value = clampf(*value, minV, maxV);
    const float ratio = span > 0.0f ? clampf((*value - minV) / span, 0.0f, 1.0f) : 0.0f;
    if (st.fill < 0.0f) {
        st.fill = ratio;
    }
    easeTo(st.fill, ratio, 20.0f);
    easeTo(st.knob, (st.grabbing && row.active) ? 1.25f : 1.0f, 16.0f);
    easeTo(st.valueHl, (st.grabbing && row.active) ? 1.0f : (row.hovered ? 0.45f : 0.0f), 14.0f);

    char buffer[64];
    if (integer) {
        std::snprintf(buffer, sizeof(buffer), "%d", (int)std::lround(*value));
    } else {
        std::snprintf(buffer, sizeof(buffer), format != nullptr ? format : "%.2f", (double)*value);
    }

    const float labelH = fonts().body ? fonts().body->FontSize : 14.0f;
    row.dl->AddText(fonts().body, labelH, ImVec2(row.min.x, row.min.y + 7.0f), palette().text, label);

    const ImVec2 valueSize = measure(fonts().mono, buffer);
    const float valueH = fonts().mono ? fonts().mono->FontSize : 12.0f;
    const ImU32 valueColor = mix(palette().textDim, palette().accent, clampf(st.valueHl, 0.0f, 1.0f));
    row.dl->AddText(fonts().mono, valueH, ImVec2(row.max.x - valueSize.x, row.min.y + 8.0f), valueColor, buffer);

    // Degerin uzerine (sag ust bolge) cift tik: manuel giris
    const ImVec2 valueHitMin(row.max.x - 90.0f, row.min.y);
    const ImVec2 valueHitMax(row.max.x, row.min.y + 26.0f);
    const bool valueHovered = row.hovered && ImGui::IsMouseHoveringRect(valueHitMin, valueHitMax, false);
    if (valueHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        ImGui::OpenPopup("##sliderinput");
    }

    static char editBuffer[48] = {};
    // Manuel giris: popup dogrudan degerin UZERINDE acilir (asagida degil).
    const float popupW = std::max(84.0f, valueSize.x + 30.0f);
    ImGui::SetNextWindowPos(ImVec2(row.max.x - popupW + 2.0f, row.min.y + 1.0f));
    ImGui::SetNextWindowSize(ImVec2(popupW, 0.0f));
    if (ImGui::BeginPopup("##sliderinput")) {
        if (ImGui::IsWindowAppearing()) {
            if (integer) {
                std::snprintf(editBuffer, sizeof(editBuffer), "%d", (int)std::lround(*value));
            } else {
                std::snprintf(editBuffer, sizeof(editBuffer), "%g", (double)*value);
            }
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(popupW - 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
        if (ImGui::InputText("##slidervalue", editBuffer, sizeof(editBuffer),
                             ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            const float parsed = (float)std::atof(editBuffer);
            *value = clampf(integer ? std::round(parsed) : parsed, minV, maxV);
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleVar();
        // ESC: once metin duzenlemesini iptal eder; popup'i de kapatalim ki
        // sonraki tik "popup kapatma" diye yutulmasin.
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Ray + dolgu + knob (dolgu deger hedefine dogru ease eder)
    row.dl->AddRectFilled(trackMin, trackMax, palette().panelSoft, trackH * 0.5f);
    const float knobX = travelMin + travelSpan * clampf(st.fill, 0.0f, 1.0f);
    if (st.fill > 0.002f) {
        row.dl->AddRectFilled(trackMin, ImVec2(knobX, trackMax.y), palette().accent, trackH * 0.5f);
    }
    const ImVec2 knob(knobX, (trackMin.y + trackMax.y) * 0.5f);
    if (row.hovered || row.active) {
        row.dl->AddCircleFilled(knob, 12.0f, withAlpha(palette().accent, 0.18f), 24);
    }
    row.dl->AddCircleFilled(knob, knobR * st.knob, IM_COL32(242, 243, 246, 255), 24);

    dockRowEnd(row);
    return changed;
}

// Renk satiri: etiket + sagda swatch + hue rayi + knob
void dockHueRow(const char* label, ImVec4* color) {
    DockRow row = dockRowBegin(label, 56.0f);
    DockRowState& st = rowState(row.id);

    const float trackH = 6.0f;
    const ImVec2 trackMin(row.min.x, row.max.y - 16.0f);
    const ImVec2 trackMax(row.max.x, trackMin.y + trackH);
    const float knobR = 8.0f;
    const float travelMin = trackMin.x + knobR;
    const float travelSpan = std::max(1.0f, (trackMax.x - trackMin.x) - knobR * 2.0f);

    float h = 0.0f;
    float s = 1.0f;
    float v = 1.0f;
    ImGui::ColorConvertRGBtoHSV(color->x, color->y, color->z, h, s, v);

    if (row.activated) {
        const float my = ImGui::GetIO().MousePos.y;
        st.grabbing = (my >= trackMin.y - 12.0f && my <= trackMax.y + 12.0f);
    }
    if (!row.active) {
        st.grabbing = false;
    }
    if (st.grabbing && row.active) {
        h = clampf((ImGui::GetIO().MousePos.x - travelMin) / travelSpan, 0.0f, 0.9999f);
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        ImGui::ColorConvertHSVtoRGB(h, 1.0f, 1.0f, r, g, b);
        color->x = r;
        color->y = g;
        color->z = b;
    }

    if (st.fill < 0.0f) {
        st.fill = h;
    }
    easeTo(st.fill, h, 20.0f);
    easeTo(st.knob, (st.grabbing && row.active) ? 1.25f : 1.0f, 16.0f);

    row.dl->AddText(fonts().body, fonts().body->FontSize,
                    ImVec2(row.min.x, row.min.y + 7.0f), palette().text, label);

    const float swatch = 20.0f;
    const ImVec2 swMin(row.max.x - swatch, row.min.y + 5.0f);
    const ImVec2 swMax(swMin.x + swatch, swMin.y + swatch);
    row.dl->AddRectFilled(swMin, swMax, ImGui::ColorConvertFloat4ToU32(*color), 5.0f);
    row.dl->AddRect(swMin, swMax, withAlpha(palette().outline, 1.0f), 5.0f, 0, 1.0f);

    auto hsv = [](float hue) {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        ImGui::ColorConvertHSVtoRGB(hue, 1.0f, 1.0f, r, g, b);
        return ImVec4(r, g, b, 1.0f);
    };
    const int segments = 32;
    for (int i = 0; i < segments; ++i) {
        const float t0 = (float)i / (float)segments;
        const float t1 = (float)(i + 1) / (float)segments;
        const ImVec4 c0 = hsv(t0);
        const ImVec4 c1 = hsv(t1);
        const float x0 = trackMin.x + (trackMax.x - trackMin.x) * t0;
        const float x1 = trackMin.x + (trackMax.x - trackMin.x) * t1 + 0.5f;
        row.dl->AddRectFilledMultiColor(ImVec2(x0, trackMin.y), ImVec2(x1, trackMax.y),
                                        ImGui::ColorConvertFloat4ToU32(c0), ImGui::ColorConvertFloat4ToU32(c1),
                                        ImGui::ColorConvertFloat4ToU32(c1), ImGui::ColorConvertFloat4ToU32(c0));
    }

    const float knobX = travelMin + travelSpan * clampf(st.fill, 0.0f, 1.0f);
    const ImVec2 knob(knobX, (trackMin.y + trackMax.y) * 0.5f);
    if (row.hovered || row.active) {
        row.dl->AddCircleFilled(knob, 12.0f, withAlpha(palette().accent, 0.18f), 24);
    }
    row.dl->AddCircleFilled(knob, knobR * st.knob, IM_COL32(242, 243, 246, 255), 24);

    dockRowEnd(row);
}

// Enum satiri: etiket + tam genislik dropdown
void dockEnumRow(const char* label, GUISetting& setting) {
    DockRow row = dockRowBegin(label, 62.0f, true);   // overlay: dropdown hover'i alabilmeli
    row.dl->AddText(fonts().body, fonts().body->FontSize,
                    ImVec2(row.min.x, row.min.y + 7.0f), palette().text, label);
    ImGui::SetCursorScreenPos(ImVec2(row.min.x, row.min.y + 26.0f));
    widgets::Dropdown("##dd", &setting.intValue, setting.options, row.width);
    dockRowEnd(row);
}

// Keybind satiri: etiket (opsiyonel) + tam genislik bind butonu
bool dockKeybindRow(const char* label, const char* idLabel, int* key, bool* listening) {
    const bool hasLabel = (label != nullptr && *label != '\0');
    DockRow row = dockRowBegin(idLabel, hasLabel ? 62.0f : 44.0f);
    bool changed = false;

    if (hasLabel) {
        row.dl->AddText(fonts().body, fonts().body->FontSize,
                        ImVec2(row.min.x, row.min.y + 7.0f), palette().text, label);
    }

    const ImVec2 btnMin(row.min.x, row.min.y + (hasLabel ? 26.0f : 4.0f));
    const ImVec2 btnMax(row.max.x, row.min.y + (hasLabel ? 58.0f : 40.0f));
    row.dl->AddRectFilled(btnMin, btnMax, palette().panelSoft, 8.0f);
    row.dl->AddRect(btnMin, btnMax,
                    withAlpha(*listening ? palette().accent : palette().outline, row.hovered ? 1.0f : 0.9f),
                    8.0f, 0, *listening ? 1.4f : 1.0f);

    if (row.pressed) {
        *listening = !*listening;
    }
    if (row.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        *key = 0;
        *listening = false;
        changed = true;
    }
    if (*listening) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            *listening = false;
        } else {
            for (int k = (int)ImGuiKey_NamedKey_BEGIN; k < (int)ImGuiKey_NamedKey_END; ++k) {
                if (ImGui::IsKeyPressed((ImGuiKey)k, false)) {
                    *key = k;
                    *listening = false;
                    changed = true;
                    break;
                }
            }
        }
    }

    char text[80];
    if (*listening) {
        std::snprintf(text, sizeof(text), "PRESS A KEY...");
    } else if (*key != 0) {
        const char* name = ImGui::GetKeyName((ImGuiKey)*key);
        std::snprintf(text, sizeof(text), "%s  |  CLICK TO BIND", name ? name : "?");
    } else {
        std::snprintf(text, sizeof(text), "UNBOUND  |  CLICK TO BIND");
    }
    const ImVec2 textSize = measure(fonts().small, text);
    const float smallH = fonts().small ? fonts().small->FontSize : 12.0f;
    row.dl->AddText(fonts().small, smallH,
                    ImVec2(btnMin.x + (btnMax.x - btnMin.x - textSize.x) * 0.5f,
                           btnMin.y + (btnMax.y - btnMin.y - smallH) * 0.5f),
                    palette().textDim, text);

    dockRowEnd(row);
    return changed;
}

// ---------------------------------------------------------------------------
// DK-PORT: text row (free string setting) + action row (button setting).
// ---------------------------------------------------------------------------
void dockTextRow(const char* label, GUISetting& setting) {
    DockRow row = dockRowBegin(label, 62.0f, true);
    row.dl->AddText(fonts().body, fonts().body->FontSize,
                    ImVec2(row.min.x, row.min.y + 7.0f), palette().text, label);

    ImGui::SetCursorScreenPos(ImVec2(row.min.x, row.min.y + 26.0f));
    ImGui::SetNextItemWidth(row.width);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(palette().panel));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImGui::ColorConvertU32ToFloat4(palette().panelSoft));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImGui::ColorConvertU32ToFloat4(palette().panelSoft));
    const bool changed = ImGui::InputText("##text", setting.textBuffer, sizeof(setting.textBuffer));
    const bool editing = ImGui::IsItemActive();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();

    if (changed) {
        setting.textValue = setting.textBuffer;
    } else if (!editing && setting.textValue != setting.textBuffer) {
        // Disaridan (config yuklemesi) gelen deger buffer'a tazelenir; kullanici
        // duzenlerken araya girilmez.
        std::snprintf(setting.textBuffer, sizeof(setting.textBuffer), "%s", setting.textValue.c_str());
    }

    dockRowEnd(row);
}

void dockActionRow(const char* label, GUISetting& setting) {
    // DK-PORT: allowOverlap so the action button submitted on top of the row
    // still receives hover/clicks (same reason as dockEnumRow/dockTextRow).
    DockRow row = dockRowBegin(label, 48.0f, true);
    const float cy = (row.min.y + row.max.y) * 0.5f;

    row.dl->AddText(fonts().body, fonts().body->FontSize,
                    ImVec2(row.min.x, cy - fonts().body->FontSize * 0.5f), palette().text, label);

    const float btnW = 104.0f;
    const float btnH = 30.0f;
    ImGui::SetCursorScreenPos(ImVec2(row.max.x - btnW, cy - btnH * 0.5f));
    const char* face = setting.actionLabel.empty() ? "RUN" : setting.actionLabel.c_str();
    if (widgets::Button("##action", face, palette().accent, IM_COL32(255, 255, 255, 255),
                        ImVec2(btnW, btnH))) {
        if (setting.onAction) {
            setting.onAction();
        }
    }

    dockRowEnd(row);
}

} // namespace

// ---------------------------------------------------------------------------
// Modul satiri: [ikon karesi] [baslik] [ozet] ..... [toggle]
// ---------------------------------------------------------------------------
float moduleRow(GUIModule& module, float width, GUIModule*& selected) {
    ImGui::PushID(module.id.c_str());

    const float rowH = 52.0f;
    const bool isSelected = (&module == selected);
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::SetCursorScreenPos(pos);
    ImGui::SetNextItemAllowOverlap();   // sag taraftaki toggle / uc nokta hover'i alabilmeli
    ImGui::InvisibleButton("##row", ImVec2(width, rowH));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    // Sol tik: yalnizca ac/kapa. Sag tik: modul ayarlari (ac/kapat).
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        module.enabled = !module.enabled;
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        selected = isSelected ? nullptr : &module;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + width, pos.y + rowH);

    // Arka plan: acik modul kutulari bir tik daha acik; kapalilar zemine yakin.
    // (Kutular rounded degil; yalnizca ic rengi farkli.)
    const ImU32 base = module.enabled ? palette().card
                                      : mix(palette().card, palette().bgTop, 0.55f);
    ImU32 background = mix(base, palette().cardHover, clampf(module.hoverAnim.value(), 0.0f, 1.0f));
    if (isSelected) {
        background = mix(background, palette().panelSoft, 0.6f);
    }
    dl->AddRectFilled(pos, max, background, 0.0f);
    // Secili modulde renkli cerceve YOK; kenarlik her zaman notr.
    dl->AddRect(pos, max, withAlpha(palette().outline, 0.9f), 0.0f, 0, 1.0f);

    // Ikon karesi: satirin soluna bitisik — kutunun sol koseleri karenin sol
    // koseleridir; yuvarlatma yok, boyut satir yuksekligi (kare).
    const ImVec2 tileMin(pos.x, pos.y);
    const ImVec2 tileMax(pos.x + rowH, pos.y + rowH);
    if (module.enabled) {
        dl->AddRectFilled(tileMin, tileMax, palette().accent);
    } else {
        dl->AddRectFilled(tileMin, tileMax, mix(palette().outline, palette().panelSoft, 0.55f));
    }
    const ImVec2 tileCenter((tileMin.x + tileMax.x) * 0.5f, (tileMin.y + tileMax.y) * 0.5f);
    // PNG paket ikonu varsa onu, yoksa vektorel ikonu ciz.
    const char* packIcon = iconpack::moduleIconName(module.id.c_str());
    const ImU32 packTint = module.enabled ? IM_COL32(255, 255, 255, 240)
                                          : IM_COL32(172, 175, 182, 210);
    if (!iconpack::drawCentered(dl, packIcon, tileCenter, 40.0f, packTint)) {
        icons::drawModuleIcon(dl, module.icon, tileCenter, 9.5f,
                              module.enabled ? IM_COL32(255, 255, 255, 255) : palette().textDim);
    }

    // Baslik (durum bagimli renk) + satir ici ozet
    const float centerY = pos.y + rowH * 0.5f;
    const float titleH = fonts().title ? fonts().title->FontSize : 14.0f;
    const float subH = fonts().small ? fonts().small->FontSize : 12.0f;
    const float titleX = tileMax.x + 12.0f;
    const float dotsW = 26.0f;   // satir sonundaki ince gri kutu
    const float toggleX = pos.x + width - dotsW - 12.0f - 40.0f;
    const float clipRight = toggleX - 12.0f;

    const ImU32 titleColor = module.enabled ? palette().text : mix(palette().text, palette().textDim, 0.35f);
    if (fonts().title && fonts().small && clipRight > titleX + kUnit) {
        dl->PushClipRect(ImVec2(titleX, pos.y), ImVec2(clipRight, pos.y + rowH), true);
        dl->AddText(fonts().title, titleH, ImVec2(titleX, centerY - titleH * 0.5f),
                    titleColor, module.name.c_str());
        if (!module.description.empty()) {
            const ImVec2 nameSize = measure(fonts().title, module.name.c_str());
            dl->AddText(fonts().small, subH,
                        ImVec2(titleX + nameSize.x + 12.0f, centerY - subH * 0.5f + 0.5f),
                        palette().textDim, module.description.c_str());
        }
        dl->PopClipRect();
    }

    // Toggle (sag)
    ImGui::SetCursorScreenPos(ImVec2(toggleX, centerY - 16.0f));
    widgets::ToggleSwitch("##toggle", &module.enabled, module.toggleAnim);

    // Satir sonu: ince gri kutu + dikey uc nokta. Sol tik -> modul ayarlari.
    const ImVec2 dotsMin(pos.x + width - dotsW, pos.y);
    const ImVec2 dotsMax(pos.x + width, pos.y + rowH);
    ImGui::SetCursorScreenPos(dotsMin);
    ImGui::PushID("row_menu");
    ImGui::InvisibleButton("##dots", ImVec2(dotsW, rowH));
    const bool dotsHovered = ImGui::IsItemHovered();
    if (dotsHovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    const bool dotsClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    ImGui::PopID();

    const ImU32 dotsBase = mix(palette().outline, palette().panelSoft, 0.55f);
    dl->AddRectFilled(dotsMin, dotsMax, dotsHovered ? brighten(dotsBase, 0.06f) : dotsBase);
    {
        const ImVec2 dotsCenter((dotsMin.x + dotsMax.x) * 0.5f, (dotsMin.y + dotsMax.y) * 0.5f);
        const ImU32 dotColor = dotsHovered ? palette().text : palette().textDim;
        for (int i = -1; i <= 1; ++i) {
            dl->AddCircleFilled(ImVec2(dotsCenter.x, dotsCenter.y + (float)i * 5.0f), 1.6f, dotColor);
        }
    }
    if (dotsClicked) {
        selected = isSelected ? nullptr : &module;
    }

    module.hoverAnim.setTarget(hovered ? 1.0f : 0.0f);

    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + rowH));
    ImGui::PopID();
    return rowH;
}

// ---------------------------------------------------------------------------
// Tek sutun modul listesi
// ---------------------------------------------------------------------------
namespace {

void renderModuleList(const std::vector<GUIModule*>& list, float width, GUIModule*& selected) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    float y = 0.0f;
    for (GUIModule* module : list) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + y));
        y += moduleRow(*module, width, selected) + kRowGap;
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + y));
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
}

} // namespace

void modulesPage(ModuleRegistry& registry, Category category, float width, GUIModule*& selected) {
    std::vector<GUIModule*> list = registry.byCategory(category);

    if (list.empty()) {
        ImGui::PushFont(fonts().small);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(palette().textMuted));
        ImGui::TextUnformatted("Liste bos");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        return;
    }

    renderModuleList(list, width, selected);
}

void scriptsPage() {
    const ImVec2 available = ImGui::GetContentRegionAvail();
    ImGui::Dummy(ImVec2(0.0f, available.y * 0.28f));

    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const float centerX = cursor.x + available.x * 0.5f;
    icons::drawUiIcon(ImGui::GetWindowDrawList(), icons::UiIcon::Scripts,
                      ImVec2(centerX, cursor.y + 24.0f), 22.0f, palette().textMuted);

    ImGui::Dummy(ImVec2(0.0f, 56.0f));
    const char* title = "Scripts";
    const ImVec2 titleSize = measure(fonts().title, title);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available.x - titleSize.x) * 0.5f);
    ImGui::PushFont(fonts().title);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();

    const char* hint = "Henuz script yok";
    const ImVec2 hintSize = measure(fonts().small, hint);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available.x - hintSize.x) * 0.5f);
    ImGui::PushFont(fonts().small);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(palette().textMuted));
    ImGui::TextUnformatted(hint);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

// ---------------------------------------------------------------------------
// Sag panel: Options veya modul ayarlari
// ---------------------------------------------------------------------------
namespace {

bool optionsHeader() {
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = 28.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGui::PushID("options_back");
    ImGui::InvisibleButton("##back", ImVec2(24.0f, h));
    const bool backHovered = ImGui::IsItemHovered();
    const bool closeRequested = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    ImGui::PopID();
    {
        const ImVec2 backCenter(pos.x + 10.0f, pos.y + h * 0.5f);
        const ImU32 backTint = backHovered ? palette().text : palette().textDim;
        if (!iconpack::drawCentered(dl, "back", backCenter, 16.0f, backTint)) {
            icons::drawUiIcon(dl, icons::UiIcon::Back, backCenter, 8.0f, backTint);
        }
    }

    const float titleH = fonts().title ? fonts().title->FontSize : 14.0f;
    dl->AddText(fonts().title, titleH, ImVec2(pos.x + 28.0f, pos.y + (h - titleH) * 0.5f),
                palette().text, "Options");

    const char* version = dk::version::kShortString;
    const ImVec2 versionSize = measure(fonts().small, version);
    dl->AddText(fonts().small, fonts().small ? fonts().small->FontSize : 12.0f,
                ImVec2(pos.x + width - versionSize.x, pos.y + (h - versionSize.y) * 0.5f),
                palette().textMuted, version);

    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + h + 4.0f));
    return closeRequested;
}

bool optionsPanel() {
    const bool closeRequested = optionsHeader();

    dockThemeRow();

    // DK-PORT: the demo toggles (self destruct / text gui / save module
    // states / ...) were placeholders. The host (arifHost.cpp) supplies the
    // real client toggles through this hook.
    host::optionsPanelRows();

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    return closeRequested;
}

bool moduleHeader(GUIModule& module) {
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = 28.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGui::PushID("mod_back");
    ImGui::InvisibleButton("##back", ImVec2(24.0f, h));
    const bool backHovered = ImGui::IsItemHovered();
    const bool closeRequested = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    ImGui::PopID();
    {
        const ImVec2 backCenter(pos.x + 10.0f, pos.y + h * 0.5f);
        const ImU32 backTint = backHovered ? palette().text : palette().textDim;
        if (!iconpack::drawCentered(dl, "back", backCenter, 16.0f, backTint)) {
            icons::drawUiIcon(dl, icons::UiIcon::Back, backCenter, 8.0f, backTint);
        }
    }

    ImGui::PushID("mod_fav");
    ImGui::SetCursorScreenPos(ImVec2(pos.x + width - 24.0f, pos.y));
    ImGui::InvisibleButton("##fav", ImVec2(24.0f, h));
    const bool favHovered = ImGui::IsItemHovered();
    // Yildiz icin modul bazli "pop" animasyonu (tik -> 1'e firlar, soner).
    AnimationState& favPop = keyedAnim((module.id + "##favpop").c_str());
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        module.favorite = !module.favorite;
        favPop.snap(1.0f);
        favPop.setTarget(0.0f);
        favPop.setSpeed(6.5f);
    }
    ImGui::PopID();
    favPop.update(ImGui::GetIO().DeltaTime);
    {
        const float pop = clampf(favPop.value(), 0.0f, 1.0f);
        const ImVec2 starCenter(pos.x + width - 12.0f, pos.y + h * 0.5f);
        const ImU32 starTint = module.favorite ? palette().accent
                                               : (favHovered ? palette().text : palette().textDim);
        if (pop > 0.01f) {
            // Disari dogru genisleyip sonen vurgu halkasi.
            dl->AddCircleFilled(starCenter, 9.0f + (1.0f - pop) * 7.0f,
                                withAlpha(starTint, 0.30f * pop), 24);
        }
        const float starScale = 1.0f + 0.35f * pop;
        if (!iconpack::drawCentered(dl, "favorites", starCenter, 16.0f * starScale, starTint)) {
            icons::drawUiIcon(dl, icons::UiIcon::Star, starCenter, 7.5f * starScale, starTint);
        }
    }

    char title[96];
    std::snprintf(title, sizeof(title), "%s Settings", module.name.c_str());
    const float titleH = fonts().title ? fonts().title->FontSize : 14.0f;
    dl->PushClipRect(ImVec2(pos.x + 28.0f, pos.y), ImVec2(pos.x + width - 28.0f, pos.y + h), true);
    dl->AddText(fonts().title, titleH, ImVec2(pos.x + 28.0f, pos.y + (h - titleH) * 0.5f),
                palette().text, title);
    dl->PopClipRect();

    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + h + 4.0f));
    return closeRequested;
}

bool modulePanel(GUIModule& module) {
    const bool closeRequested = moduleHeader(module);

    for (const auto& settingPtr : module.settings) {
        GUISetting& setting = *settingPtr;
        if (!setting.visible()) {
            continue;
        }

        switch (setting.kind) {
            case SettingKind::Enum:
                dockEnumRow(setting.label.c_str(), setting);
                break;
            case SettingKind::Bool:
                dockToggleRow(setting.label.c_str(), &setting.boolValue);
                break;
            case SettingKind::Float:
                dockSliderRow(setting.label.c_str(), &setting.floatValue, setting.floatMin, setting.floatMax,
                              setting.floatFormat, false);
                break;
            case SettingKind::Int: {
                float current = (float)setting.intValue;
                if (dockSliderRow(setting.label.c_str(), &current, (float)setting.intMin,
                                  (float)setting.intMax, nullptr, true)) {
                    setting.intValue = (int)std::lround(current);
                }
                break;
            }
            case SettingKind::Color:
                dockHueRow(setting.label.c_str(), &setting.colorValue);
                break;
            case SettingKind::Keybind:
                dockKeybindRow(setting.label.c_str(), setting.id.c_str(), &setting.keyValue, &setting.listeningKey);
                break;
            // DK-PORT: host integration kinds.
            case SettingKind::Text:
                dockTextRow(setting.label.c_str(), setting);
                break;
            case SettingKind::Action:
                dockActionRow(setting.label.c_str(), setting);
                break;
        }
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    return closeRequested;
}

} // namespace

bool dockPanel(GUIModule* module, ImVec2 size) {
    bool closeRequested = false;

    ImGui::PushID("##dock");
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    ImGui::BeginChild("##dockpanel", size, false);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 panelMin = ImGui::GetWindowPos();
    const ImVec2 panelMax(panelMin.x + size.x, panelMin.y + size.y);

    // Sag kenara yapisik panel: sol koseler yuvarlatilir. DK-PORT: ana pencere
    // artik yuvarlatilmis serbest bir panel oldugu icin sag-alt kose de pencere
    // yuvarlakligini takip eder (sag-ust kose seridin altinda kaldigindan keskin).
    const ImDrawFlags panelCorners = ImDrawFlags_RoundCornersLeft | ImDrawFlags_RoundCornersBottomRight;
    dl->AddRectFilled(panelMin, panelMax, palette().panel, 12.0f, panelCorners);

    // Icerik: simetrik padding (sagdan tasma olmaz) + kendi scroll'u
    ImGui::SetCursorPos(ImVec2(kDockPad, kDockPad));
    ImGui::BeginChild("##content", ImVec2(size.x - kDockPad * 2.0f, size.y - kDockPad * 2.0f), false);

    if (module != nullptr) {
        closeRequested = modulePanel(*module);
    } else {
        closeRequested = optionsPanel();
    }

    ImGui::EndChild();
    ImGui::EndChild();

    dl->AddRect(panelMin, panelMax, withAlpha(palette().outline, 0.9f), 12.0f, panelCorners, 1.0f);

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::PopID();

    return closeRequested;
}

// DK-PORT: see Panels.hpp - thin wrapper over the dock-styled toggle row so
// the host can compose the Options overlay with an identical look.
void hostToggleRow(const char* label, bool* value) {
    dockToggleRow(label, value);
}

void hostKeybindRow(const char* id, int* key, bool* listening) {
    dockKeybindRow(nullptr, id, key, listening);
}

// ---------------------------------------------------------------------------
// Friends / Target manager
// ---------------------------------------------------------------------------
namespace {

void playerRow(FriendManager& friends, const PlayerEntry& player) {
    ImGui::PushID(player.name.c_str());

    const float rowH = 44.0f;
    const float rowW = safeWidth(ImGui::GetContentRegionAvail().x);
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton("##row", ImVec2(rowW, rowH));
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered) {
        dl->AddRectFilled(pos, ImVec2(pos.x + rowW, pos.y + rowH), withAlpha(palette().panelSoft, 0.5f), 8.0f);
    }

    const float avatarR = 14.0f;
    const ImVec2 avatar(pos.x + 16.0f + avatarR, pos.y + rowH * 0.5f);
    const ImU32 avatarColor = mix(palette().accent, palette().accent2, (float)(player.name.size() % 10) / 10.0f);
    dl->AddCircleFilled(avatar, avatarR, withAlpha(avatarColor, 0.9f), 24);
    char initial[2] = {player.name.empty() ? '?' : (char)std::toupper((unsigned char)player.name[0]), 0};
    const ImVec2 initialSize = measure(fonts().body, initial);
    dl->AddText(fonts().body, fonts().body->FontSize,
                ImVec2(avatar.x - initialSize.x * 0.5f, avatar.y - fonts().body->FontSize * 0.5f),
                IM_COL32(255, 255, 255, 255), initial);

    const float btnW = 88.0f;
    const float btnH = 32.0f;
    const float btnGap = kUnit;
    const float actionsX = pos.x + rowW - 16.0f - (btnW * 2.0f + btnGap);

    const float statusRight = actionsX - 16.0f;
    if (player.ping > 0) {
        char pingText[24];
        std::snprintf(pingText, sizeof(pingText), "%d ms", player.ping);
        const ImVec2 pingSize = measure(fonts().mono, pingText);
        const ImU32 pingColor = player.ping < 60 ? palette().success
                                : (player.ping < 150 ? palette().warning : palette().danger);
        dl->AddText(fonts().mono, fonts().mono ? fonts().mono->FontSize : ImGui::GetFontSize(),
                    ImVec2(statusRight - pingSize.x, pos.y + (rowH - pingSize.y) * 0.5f),
                    pingColor, pingText);
    }

    const float nameX = pos.x + 16.0f + avatarR * 2.0f + 12.0f;
    dl->PushClipRect(pos, ImVec2(statusRight - 64.0f, pos.y + rowH), true);
    dl->AddText(fonts().body, fonts().body->FontSize,
                ImVec2(nameX, pos.y + (rowH - fonts().body->FontSize) * 0.5f),
                palette().text, player.name.c_str());
    dl->PopClipRect();

    const float btnY = pos.y + (rowH - btnH) * 0.5f;
    const bool isFriend = friends.isFriend(player.name);
    const bool isTarget = friends.isTarget(player.name);

    ImGui::SetCursorScreenPos(ImVec2(actionsX, btnY));
    if (!isFriend) {
        if (widgets::Button("##friend", "+ Friend", palette().success, IM_COL32(18, 32, 24, 255), ImVec2(btnW, btnH))) {
            friends.addFriend(player.name);
        }
    } else {
        widgets::Button("##friendOn", "FRIEND", withAlpha(palette().success, 0.22f), palette().success, ImVec2(btnW, btnH));
    }

    ImGui::SetCursorScreenPos(ImVec2(actionsX + btnW + btnGap, btnY));
    if (!isTarget) {
        if (widgets::Button("##target", "+ Target", palette().danger, IM_COL32(34, 20, 22, 255), ImVec2(btnW, btnH))) {
            friends.addTarget(player.name);
        }
    } else {
        widgets::Button("##targetOn", "TARGET", withAlpha(palette().danger, 0.22f), palette().danger, ImVec2(btnW, btnH));
    }

    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + rowH + kUnit * 0.5f));
    ImGui::PopID();
}

void namedListRow(const char* label, const std::string& name, ImU32 color, bool* removeRequested) {
    ImGui::PushID(label);
    ImGui::PushID(name.c_str());

    const float rowH = 40.0f;
    const float rowW = safeWidth(ImGui::GetContentRegionAvail().x);
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton("##row", ImVec2(rowW, rowH));
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered) {
        dl->AddRectFilled(pos, ImVec2(pos.x + rowW, pos.y + rowH), withAlpha(palette().panelSoft, 0.45f), 8.0f);
    }
    dl->AddCircleFilled(ImVec2(pos.x + 16.0f, pos.y + rowH * 0.5f), 5.0f, withAlpha(color, 0.9f), 16);
    dl->AddText(fonts().body, fonts().body->FontSize,
                ImVec2(pos.x + 32.0f, pos.y + (rowH - fonts().body->FontSize) * 0.5f),
                palette().text, name.c_str());

    const float removeSize = 32.0f;
    ImGui::SetCursorScreenPos(ImVec2(pos.x + rowW - 16.0f - removeSize, pos.y + (rowH - removeSize) * 0.5f));
    if (widgets::Button("##remove", "x", withAlpha(palette().danger, 0.25f), palette().danger, ImVec2(removeSize, removeSize))) {
        *removeRequested = true;
    }

    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + rowH + kUnit * 0.5f));
    ImGui::PopID();
    ImGui::PopID();
}

} // namespace

void friendsPage(FriendManager& friends, char* searchBuffer, int searchBufferSize) {
    const float available = ImGui::GetContentRegionAvail().x;

    widgets::SearchInput("##friend_search", searchBuffer, searchBufferSize, "Search players...", 280.0f);
    ImGui::SameLine(0.0f, kUnit * 2.0f);
    ImGui::PushFont(fonts().small);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(palette().textMuted));
    ImGui::Text("%d online  -  %d friend  -  %d target",
                (int)friends.online.size(), (int)friends.friends.size(), (int)friends.targets.size());
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(0.0f, kUnit));

    const float gap = kColGap;
    const float leftW = (available - gap) * 0.62f;
    const float rightW = available - gap - leftW;
    const float panelH = std::max(220.0f, ImGui::GetContentRegionAvail().y - kUnit);

    ImGui::BeginChild("##players", ImVec2(leftW, panelH), false);
    widgets::SectionHeader("ONLINE PLAYERS");

    const std::string filter = searchBuffer;
    for (const PlayerEntry& player : friends.online) {
        if (!filter.empty() && !containsText(player.name, filter)) {
            continue;
        }
        playerRow(friends, player);
    }
    ImGui::EndChild();

    ImGui::SameLine(0.0f, gap);

    ImGui::BeginChild("##lists", ImVec2(rightW, panelH), false);
    widgets::SectionHeader("FRIENDS");
    if (friends.friends.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(palette().textMuted));
        ImGui::TextUnformatted("Liste bos");
        ImGui::PopStyleColor();
    } else {
        std::string removeName;
        for (const PlayerEntry& entry : friends.friends) {
            bool removeRequested = false;
            namedListRow("friend", entry.name, palette().success, &removeRequested);
            if (removeRequested) {
                removeName = entry.name;
            }
        }
        if (!removeName.empty()) {
            friends.removeFriend(removeName);
        }
    }

    widgets::SectionHeader("TARGETS");
    if (friends.targets.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(palette().textMuted));
        ImGui::TextUnformatted("Liste bos");
        ImGui::PopStyleColor();
    } else {
        std::string removeName;
        for (const PlayerEntry& entry : friends.targets) {
            bool removeRequested = false;
            namedListRow("target", entry.name, palette().danger, &removeRequested);
            if (removeRequested) {
                removeName = entry.name;
            }
        }
        if (!removeName.empty()) {
            friends.removeTarget(removeName);
        }
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Configs / Profiles
// ---------------------------------------------------------------------------
void configsPage(ConfigManager& configs, char* newConfigBuffer, int newConfigBufferSize) {
    static TimedAnimation flash;
    static std::string flashMessage;

    const float deltaTime = ImGui::GetIO().DeltaTime;
    flash.update(deltaTime);

    const float available = safeWidth(ImGui::GetContentRegionAvail().x);
    const int columns = 3;
    const float gap = kColGap;
    const float cardW = (available - gap * (float)(columns - 1)) / (float)columns;
    const float cardH = 112.0f;

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    std::string pendingDelete;
    std::string pendingLoad;
    std::string pendingSave;

    auto chip = [&](ImVec2 cardPos, float width, const char* text, ImU32 color) {
        const ImVec2 textSize = measure(fonts().small, text);
        const ImVec2 chipMin(cardPos.x + width - 16.0f - (textSize.x + 16.0f), cardPos.y + 16.0f);
        const ImVec2 chipMax(chipMin.x + textSize.x + 16.0f, chipMin.y + 20.0f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(chipMin, chipMax, withAlpha(color, 0.16f), 10.0f);
        dl->AddRect(chipMin, chipMax, withAlpha(color, 0.55f), 10.0f);
        dl->AddText(fonts().small, fonts().small->FontSize,
                    ImVec2(chipMin.x + 8.0f, chipMin.y + (20.0f - fonts().small->FontSize) * 0.5f),
                    color, text);
    };

    int index = 0;
    for (const ConfigProfile& profile : configs.profiles) {
        const int column = index % columns;
        const int row = index / columns;
        const ImVec2 pos(origin.x + (float)column * (cardW + gap), origin.y + (float)row * (cardH + gap));

        ImGui::PushID(profile.name.c_str());
        ImGui::SetCursorScreenPos(pos);
        ImGui::InvisibleButton("##card", ImVec2(cardW, cardH));
        const bool hovered = ImGui::IsItemHovered();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 max(pos.x + cardW, pos.y + cardH);
        dl->AddRectFilled(pos, max,
                          profile.active ? mix(palette().card, palette().accent, 0.05f) : palette().card,
                          10.0f);
        if (profile.active) {
            dl->AddRect(ImVec2(pos.x - 1.0f, pos.y - 1.0f), ImVec2(max.x + 1.0f, max.y + 1.0f),
                        withAlpha(palette().accent, 0.16f), 11.0f, 0, 2.0f);
            dl->AddRect(pos, max, palette().accent, 10.0f, 0, 1.0f);
        } else {
            dl->AddRect(pos, max, withAlpha(palette().outline, hovered ? 1.0f : 0.65f), 10.0f, 0, 1.0f);
        }

        dl->AddText(fonts().title, fonts().title->FontSize,
                    ImVec2(pos.x + 16.0f, pos.y + 16.0f), palette().text, profile.name.c_str());

        if (profile.active) {
            chip(pos, cardW, "ACTIVE", palette().accent2);
        } else if (profile.builtin) {
            chip(pos, cardW, "BUILTIN", palette().textMuted);
        }

        const float btnW = 72.0f;
        const float btnH = 32.0f;
        ImGui::SetCursorScreenPos(ImVec2(pos.x + 16.0f, pos.y + cardH - 16.0f - btnH));
        if (widgets::Button("##load", "Load", palette().accent, IM_COL32(255, 255, 255, 255), ImVec2(btnW, btnH))) {
            pendingLoad = profile.name;
        }
        ImGui::SetCursorScreenPos(ImVec2(pos.x + 16.0f + btnW + kUnit, pos.y + cardH - 16.0f - btnH));
        if (widgets::Button("##save", "Save", palette().panelSoft, palette().text, ImVec2(btnW, btnH))) {
            pendingSave = profile.name;
        }
        if (!profile.builtin) {
            ImGui::SetCursorScreenPos(ImVec2(pos.x + 16.0f + (btnW + kUnit) * 2.0f, pos.y + cardH - 16.0f - btnH));
            if (widgets::Button("##delete", "Delete", withAlpha(palette().danger, 0.22f), palette().danger, ImVec2(btnW, btnH))) {
                pendingDelete = profile.name;
            }
        }

        ImGui::PopID();
        ++index;
    }

    const int rows = (index + columns - 1) / columns;
    const ImVec2 barPos(origin.x, origin.y + (float)rows * (cardH + gap) + kUnit);
    ImGui::SetCursorScreenPos(barPos);

    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputTextWithHint("##new_profile", "New profile name...", newConfigBuffer, (size_t)newConfigBufferSize);

    ImGui::SameLine(0.0f, kUnit);
    if (widgets::Button("##create", "Create New", palette().accent, IM_COL32(255, 255, 255, 255), ImVec2(116.0f, 32.0f))) {
        if (newConfigBuffer[0] != '\0') {
            configs.create(newConfigBuffer);
            newConfigBuffer[0] = '\0';
            flashMessage = "Profile created";
            flash.play(1.6f);
        }
    }

    ImGui::SameLine(0.0f, kUnit * 3.0f);
    if (widgets::Button("##export", "Export to Clipboard", palette().panelSoft, palette().text, ImVec2(168.0f, 32.0f))) {
        ImGui::SetClipboardText(configs.exportText().c_str());
        flashMessage = "Profiles copied to clipboard";
        flash.play(1.6f);
    }

    ImGui::SameLine(0.0f, kUnit * 2.0f);
    if (widgets::Button("##import", "Import from Clipboard", palette().panelSoft, palette().text, ImVec2(168.0f, 32.0f))) {
        const char* clipboard = ImGui::GetClipboardText();
        const bool ok = clipboard != nullptr && configs.importText(clipboard);
        flashMessage = ok ? "Profiles imported" : "Import failed";
        flash.play(1.6f);
    }

    if (flash.value() > 0.01f) {
        ImGui::SameLine(0.0f, kUnit * 2.0f);
        ImGui::PushFont(fonts().small);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImGui::ColorConvertU32ToFloat4(withAlpha(palette().success, flash.value())));
        ImGui::TextUnformatted(flashMessage.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    if (!pendingLoad.empty()) {
        configs.load(pendingLoad);
        flashMessage = "Loaded: " + pendingLoad;
        flash.play(1.6f);
    }
    if (!pendingSave.empty()) {
        configs.save(pendingSave);
        flashMessage = "Saved: " + pendingSave;
        flash.play(1.6f);
    }
    if (!pendingDelete.empty()) {
        configs.remove(pendingDelete);
        flashMessage = "Deleted: " + pendingDelete;
        flash.play(1.6f);
    }
}

} // namespace arif::gui::panels
