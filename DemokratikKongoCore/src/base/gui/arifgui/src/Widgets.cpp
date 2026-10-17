#include "arif/gui/Widgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "arif/gui/Theme.hpp"

namespace arif::gui::widgets {

namespace {

float clamp01(float value) {
    return std::min(1.0f, std::max(0.0f, value));
}

float clampf(float value, float lo, float hi) {
    return std::min(hi, std::max(lo, value));
}

} // namespace

// ---------------------------------------------------------------------------
// Toggle Switch
// ---------------------------------------------------------------------------
bool ToggleSwitch(const char* id, bool* value, AnimationState& anim, float scale, bool whiteStyle) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = 40.0f * scale;
    const float hitH = 32.0f * scale;    // Fitts: min hedef yuksekligi 32px
    const float visualH = 22.0f * scale;
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton(id, ImVec2(w, hitH));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    bool changed = false;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        *value = !*value;
        changed = true;
    }

    anim.setTarget(*value ? 1.0f : 0.0f);
    const float t = clamp01(anim.value());

    // Gorsel kapsul, 32px hedef alaninin ortasinda
    const ImVec2 vMin(pos.x, pos.y + (hitH - visualH) * 0.5f);
    const ImVec2 vMax(pos.x + w, vMin.y + visualH);
    const float radius = visualH * 0.5f;

    // Stil: normal = accent gradyan; vurgulu (mavi) kartta = beyaz switch
    const ImU32 trackOff = whiteStyle ? IM_COL32(255, 255, 255, 48)
                                      : mix(palette().panel, palette().panelSoft, 0.55f);
    const ImU32 onLeft = whiteStyle ? IM_COL32(255, 255, 255, 255) : palette().accent;
    const ImU32 onRight = whiteStyle ? IM_COL32(255, 255, 255, 255) : palette().accent2;
    const ImU32 left = mix(trackOff, onLeft, t);
    const ImU32 right = mix(trackOff, onRight, t);
    drawHGradient(dl, vMin, vMax, radius, left, right, 16);

    const ImU32 borderIdle = whiteStyle ? IM_COL32(255, 255, 255, 80) : palette().outline;
    const ImU32 borderOn = whiteStyle ? IM_COL32(255, 255, 255, 235) : palette().accent;
    dl->AddRect(vMin, vMax, withAlpha(mix(borderIdle, borderOn, t), 0.9f), radius, 0, 1.0f);

    if (hovered || active) {
        dl->AddRect(ImVec2(vMin.x - 1.0f, vMin.y - 1.0f), ImVec2(vMax.x + 1.0f, vMax.y + 1.0f),
                    withAlpha(whiteStyle ? IM_COL32(255, 255, 255, 255) : palette().accent,
                              active ? 0.45f : 0.25f), radius + 1.0f, 0, 2.0f);
    }

    const float knobRadius = radius - 3.0f * scale + (active ? 0.5f : 0.0f);
    const float knobX = vMin.x + radius + (w - 2.0f * radius) * t;
    const ImVec2 knob(knobX, vMin.y + radius);
    const ImU32 knobOff = whiteStyle ? IM_COL32(255, 255, 255, 215) : mix(palette().textDim, palette().text, 0.30f);
    const ImU32 knobOn = whiteStyle ? palette().accent : IM_COL32(245, 245, 250, 255);
    const ImU32 knobColor = mix(knobOff, knobOn, t);

    if (t > 0.05f || hovered) {
        dl->AddCircleFilled(knob, knobRadius + 3.5f,
                            withAlpha(whiteStyle ? IM_COL32(255, 255, 255, 255) : palette().accent,
                                      0.12f * t + (hovered ? 0.06f : 0.0f)), 24);
    }
    dl->AddCircleFilled(knob, knobRadius, knobColor, 24);

    return changed;
}

// ---------------------------------------------------------------------------
// Checkbox (alt ayar tiki)
// ---------------------------------------------------------------------------
bool Checkbox(const char* id, bool* value) {
    const float hitW = 24.0f;
    const float hitH = 32.0f;   // Fitts: min hedef yuksekligi
    const float box = 18.0f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton(id, ImVec2(hitW, hitH));
    const bool hovered = ImGui::IsItemHovered();

    bool changed = false;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        *value = !*value;
        changed = true;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 min(pos.x + (hitW - box) * 0.5f, pos.y + (hitH - box) * 0.5f);
    const ImVec2 max(min.x + box, min.y + box);
    const float rounding = 5.0f;

    ImU32 background = *value ? palette().accent : mix(palette().panelSoft, palette().panel, 0.5f);
    if (hovered) {
        background = brighten(background, 0.06f);
    }

    dl->AddRectFilled(min, max, background, rounding);
    dl->AddRect(min, max, withAlpha(*value ? palette().accent2 : palette().outline, 0.9f), rounding);

    if (*value) {
        const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        dl->AddLine(ImVec2(center.x - 4.0f, center.y),
                    ImVec2(center.x - 1.5f, center.y + 2.5f), IM_COL32(255, 255, 255, 255), 2.0f);
        dl->AddLine(ImVec2(center.x - 1.5f, center.y + 2.5f),
                    ImVec2(center.x + 4.0f, center.y - 3.0f), IM_COL32(255, 255, 255, 255), 2.0f);
    }

    return changed;
}

// ---------------------------------------------------------------------------
// Slider (float)
// ---------------------------------------------------------------------------
bool SliderFloat(const char* id, float* value, float min, float max, const char* format, EditState* edit) {
    ImGui::PushID(id);

    const float h = 32.0f;   // Fitts: min hedef yuksekligi
    const float w = ImGui::CalcItemWidth();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    bool changed = false;

    // --- Manuel giris modu ---------------------------------------------------
    if (edit && edit->active) {
        ImGui::SetNextItemWidth(w);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 3.0f));
        const bool enter = ImGui::InputText("##manual", edit->buffer, sizeof(edit->buffer),
                                            ImGuiInputTextFlags_EnterReturnsTrue |
                                            ImGuiInputTextFlags_CharsDecimal |
                                            ImGuiInputTextFlags_AutoSelectAll);
        const bool deactivated = ImGui::IsItemDeactivated();
        ImGui::PopStyleVar();

        if (enter || deactivated) {
            *value = clampf((float)std::atof(edit->buffer), min, max);
            edit->active = false;
            changed = true;
        }
        ImGui::PopID();
        return changed;
    }

    // --- Surukleme ------------------------------------------------------------
    ImGui::InvisibleButton("##bar", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    if (active && w > 1.0f && max > min) {
        const float t = clamp01((ImGui::GetIO().MousePos.x - pos.x) / w);
        *value = min + (max - min) * t;
        changed = true;
    }

    if (edit && hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        edit->active = true;
        std::snprintf(edit->buffer, sizeof(edit->buffer), "%g", (double)*value);
    }

    // --- Cizim ---------------------------------------------------------------
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float barH = 8.0f;
    const float barY = pos.y + (h - barH) * 0.5f;
    const float radius = barH * 0.5f;
    const float t = (max > min) ? clamp01((*value - min) / (max - min)) : 0.0f;
    const float fillX = pos.x + w * t;

    dl->AddRectFilled(ImVec2(pos.x, barY), ImVec2(pos.x + w, barY + barH), palette().panelSoft, radius);
    if (t > 0.002f) {
        drawHGradient(dl, ImVec2(pos.x, barY), ImVec2(fillX, barY + barH), radius,
                      palette().accent, palette().accent2, 24);
    }

    const ImVec2 knob(fillX, barY + radius);
    if (hovered || active) {
        dl->AddCircleFilled(knob, radius + 4.0f, withAlpha(palette().accent2, 0.18f), 24);
    }
    dl->AddCircleFilled(knob, radius + (active ? 0.0f : 1.0f), IM_COL32(240, 240, 246, 255), 24);

    char text[48];
    std::snprintf(text, sizeof(text), format, (double)*value);
    ImFont* valueFont = fonts().mono;
    const float valueFontSize = valueFont ? valueFont->FontSize : ImGui::GetFontSize();
    const ImVec2 textSize = measure(valueFont, text);
    dl->AddText(valueFont, valueFontSize,
                ImVec2(pos.x + w - textSize.x - 6.0f, pos.y + (h - textSize.y) * 0.5f),
                withAlpha(palette().text, 0.88f), text);

    ImGui::PopID();
    return changed;
}

// ---------------------------------------------------------------------------
// Slider (int)
// ---------------------------------------------------------------------------
bool SliderInt(const char* id, int* value, int min, int max, EditState* edit) {
    ImGui::PushID(id);

    const float h = 32.0f;   // Fitts: min hedef yuksekligi
    const float w = ImGui::CalcItemWidth();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    bool changed = false;

    if (edit && edit->active) {
        ImGui::SetNextItemWidth(w);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 3.0f));
        const bool enter = ImGui::InputText("##manual", edit->buffer, sizeof(edit->buffer),
                                            ImGuiInputTextFlags_EnterReturnsTrue |
                                            ImGuiInputTextFlags_CharsDecimal |
                                            ImGuiInputTextFlags_AutoSelectAll);
        const bool deactivated = ImGui::IsItemDeactivated();
        ImGui::PopStyleVar();

        if (enter || deactivated) {
            *value = std::min(max, std::max(min, std::atoi(edit->buffer)));
            edit->active = false;
            changed = true;
        }
        ImGui::PopID();
        return changed;
    }

    ImGui::InvisibleButton("##bar", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    if (active && w > 1.0f && max > min) {
        const float t = clamp01((ImGui::GetIO().MousePos.x - pos.x) / w);
        *value = (int)(min + (max - min) * t + 0.5f);
        changed = true;
    }

    if (edit && hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        edit->active = true;
        std::snprintf(edit->buffer, sizeof(edit->buffer), "%d", *value);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float barH = 8.0f;
    const float barY = pos.y + (h - barH) * 0.5f;
    const float radius = barH * 0.5f;
    const float t = (max > min) ? clamp01((float)(*value - min) / (float)(max - min)) : 0.0f;
    const float fillX = pos.x + w * t;

    dl->AddRectFilled(ImVec2(pos.x, barY), ImVec2(pos.x + w, barY + barH), palette().panelSoft, radius);
    if (t > 0.002f) {
        drawHGradient(dl, ImVec2(pos.x, barY), ImVec2(fillX, barY + barH), radius,
                      palette().accent, palette().accent2, 24);
    }

    const ImVec2 knob(fillX, barY + radius);
    if (hovered || active) {
        dl->AddCircleFilled(knob, radius + 4.0f, withAlpha(palette().accent2, 0.18f), 24);
    }
    dl->AddCircleFilled(knob, radius + (active ? 0.0f : 1.0f), IM_COL32(240, 240, 246, 255), 24);

    char text[48];
    std::snprintf(text, sizeof(text), "%d", *value);
    ImFont* valueFont = fonts().mono;
    const float valueFontSize = valueFont ? valueFont->FontSize : ImGui::GetFontSize();
    const ImVec2 textSize = measure(valueFont, text);
    dl->AddText(valueFont, valueFontSize,
                ImVec2(pos.x + w - textSize.x - 6.0f, pos.y + (h - textSize.y) * 0.5f),
                withAlpha(palette().text, 0.88f), text);

    ImGui::PopID();
    return changed;
}

// ---------------------------------------------------------------------------
// Dropdown
// ---------------------------------------------------------------------------
bool Dropdown(const char* id, int* current, const std::vector<std::string>& options, float width) {
    ImGui::PushID(id);

    if (width <= 0.0f) {
        width = ImGui::CalcItemWidth();
    }

    const float h = 34.0f;
    const float radius = 8.0f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##combo", ImVec2(width, h));
    const bool hovered = ImGui::IsItemHovered();
    const bool open = ImGui::IsPopupOpen("##popup");

    // Popup acilis animasyonu (yumusak kayma + fade).
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID popupId = ImGui::GetID("##popup");
    float anim = storage->GetFloat(popupId, 0.0f);
    anim += ((open ? 1.0f : 0.0f) - anim) * std::min(1.0f, ImGui::GetIO().DeltaTime * 18.0f);
    storage->SetFloat(popupId, anim);

    bool changed = false;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        ImGui::OpenPopup("##popup");
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + width, pos.y + h);
    const ImU32 background = open ? palette().panelSoft
                                  : (hovered ? brighten(palette().panelSoft, 0.05f) : palette().panel);
    dl->AddRectFilled(pos, max, background, radius);
    dl->AddRect(pos, max, withAlpha(open ? palette().accent : palette().outline, 0.9f), radius, 0, 1.0f);

    // Secili deger kutuda ORTALANIR; sagda chevron.
    const char* label = (*current >= 0 && *current < (int)options.size())
                            ? options[(size_t)*current].c_str()
                            : "-";
    const ImVec2 labelSize = measure(fonts().body, label);
    dl->AddText(fonts().body, fonts().body->FontSize,
                ImVec2(pos.x + (width - labelSize.x) * 0.5f, pos.y + (h - labelSize.y) * 0.5f),
                palette().text, label);
    Chevron(dl, ImVec2(pos.x + width - 16.0f, pos.y + h * 0.5f), 5.0f,
            open ? kPi : 0.0f, palette().textDim, 1.5f);

    // Popup: yuvarlak koseler, ortalanmis secenekler, hover/secili accent zemin.
    ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y + h + 4.0f + (1.0f - anim) * 6.0f));
    ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 3.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(brighten(palette().panel, 0.035f)));
    if (ImGui::BeginPopup("##popup")) {
        ImDrawList* pdl = ImGui::GetWindowDrawList();
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, anim);

        const float optW = width - 16.0f;
        const float optH = 32.0f;
        for (int i = 0; i < (int)options.size(); ++i) {
            const bool selected = (i == *current);
            const ImVec2 optMin = ImGui::GetCursorScreenPos();
            const ImVec2 optMax(optMin.x + optW, optMin.y + optH);

            ImGui::PushID(i);
            ImGui::InvisibleButton("##opt", ImVec2(optW, optH));
            const bool optHovered = ImGui::IsItemHovered();
            const bool optClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            ImGui::PopID();

            if (selected || optHovered) {
                pdl->AddRectFilled(optMin, optMax,
                                   withAlpha(palette().accent, selected ? 0.30f : 0.16f), 8.0f);
            }

            const std::string& text = options[(size_t)i];
            const ImVec2 textSize = measure(fonts().body, text.c_str());
            pdl->AddText(fonts().body, fonts().body->FontSize,
                         ImVec2(optMin.x + (optW - textSize.x) * 0.5f, optMin.y + (optH - textSize.y) * 0.5f),
                         selected ? palette().text : (optHovered ? palette().text : palette().textDim),
                         text.c_str());

            if (optClicked) {
                *current = i;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    ImGui::PopID();
    return changed;
}

// ---------------------------------------------------------------------------
// Keybind
// ---------------------------------------------------------------------------
bool Keybind(const char* id, int* key, bool* listening, float width) {
    ImGui::PushID(id);

    const float h = 32.0f;   // Fitts: min hedef yuksekligi
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##key", ImVec2(width, h));
    const bool hovered = ImGui::IsItemHovered();

    bool changed = false;

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        *listening = !*listening;
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        *key = 0;
        *listening = false;
        changed = true;
    }
    // Baska bir yere tiklaninca dinlemeyi birak
    if (*listening && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !hovered) {
        *listening = false;
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

    char text[64];
    if (*listening) {
        std::snprintf(text, sizeof(text), "[ ... ]");
    } else if (*key != 0) {
        const char* name = ImGui::GetKeyName((ImGuiKey)*key);
        std::snprintf(text, sizeof(text), "[ %s ]", name ? name : "?");
    } else {
        std::snprintf(text, sizeof(text), "[ NONE ]");
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + width, pos.y + h);
    const ImU32 background = *listening
        ? withAlpha(palette().accent, 0.85f)
        : (hovered ? palette().panelSoft : withAlpha(palette().panelSoft, 0.55f));
    dl->AddRectFilled(pos, max, background, 6.0f);
    dl->AddRect(pos, max, withAlpha(*listening ? palette().accent2 : palette().outline, hovered ? 1.0f : 0.6f), 6.0f);

    ImFont* keyFont = fonts().mono;
    const float keyFontSize = keyFont ? keyFont->FontSize : ImGui::GetFontSize();
    const ImVec2 textSize = measure(keyFont, text);
    dl->AddText(keyFont, keyFontSize,
                ImVec2(pos.x + (width - textSize.x) * 0.5f, pos.y + (h - textSize.y) * 0.5f), palette().text, text);

    ImGui::PopID();
    return changed;
}

// ---------------------------------------------------------------------------
// Color picker
// ---------------------------------------------------------------------------
bool ColorPicker(const char* id, ImVec4* color) {
    ImGui::PushID(id);

    const float w = 40.0f;
    const float hitH = 32.0f;    // Fitts: min hedef yuksekligi
    const float swatchH = 22.0f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##swatch", ImVec2(w, hitH));
    const bool hovered = ImGui::IsItemHovered();

    bool changed = false;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        ImGui::OpenPopup("##pick");
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 min(pos.x, pos.y + (hitH - swatchH) * 0.5f);
    const ImVec2 max(min.x + w, min.y + swatchH);
    dl->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(*color), 6.0f);
    dl->AddRect(min, max, withAlpha(hovered ? palette().textDim : palette().outline, 0.9f), 6.0f);

    if (ImGui::BeginPopup("##pick")) {
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::ColorPicker4("##picker", (float*)color,
                                ImGuiColorEditFlags_NoSidePreview |
                                ImGuiColorEditFlags_NoInputs |
                                ImGuiColorEditFlags_AlphaBar |
                                ImGuiColorEditFlags_PickerHueBar)) {
            changed = true;
        }
        ImGui::EndPopup();
    }

    ImGui::PopID();
    return changed;
}

// ---------------------------------------------------------------------------
// Kucuk kapsul buton
// ---------------------------------------------------------------------------
bool Button(const char* id, const char* label, ImU32 background, ImU32 foreground, ImVec2 size) {
    if (size.x <= 0.0f) {
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        size = ImVec2(textSize.x + 24.0f, 32.0f);   // varsayilan hedef yuksekligi 32px
    }

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

    ImU32 bg = active ? brighten(background, -0.06f) : (hovered ? brighten(background, 0.10f) : background);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + size.x, pos.y + size.y);
    dl->AddRectFilled(pos, max, bg, 6.0f);

    const ImVec2 textSize = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(pos.x + (size.x - textSize.x) * 0.5f, pos.y + (size.y - textSize.y) * 0.5f),
                foreground, label);

    return clicked;
}

// ---------------------------------------------------------------------------
// Arama cubugu
// ---------------------------------------------------------------------------
bool SearchInput(const char* id, char* buffer, int bufferSize, const char* hint, float width) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(38.0f, 9.0f));  // genis yatay padding
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(palette().panel));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImGui::ColorConvertU32ToFloat4(palette().panelSoft));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImGui::ColorConvertU32ToFloat4(palette().panelSoft));

    ImGui::SetNextItemWidth(width);
    const bool changed = ImGui::InputTextWithHint(id, hint, buffer, (size_t)bufferSize);

    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    // Cerceve + buyutec ikonu (elle)
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const ImU32 border = active ? palette().accent : (hovered ? palette().outline : withAlpha(palette().outline, 0.7f));
    dl->AddRect(min, max, border, 8.0f);

    const ImVec2 center(min.x + 20.0f, (min.y + max.y) * 0.5f);
    dl->AddCircle(center, 5.0f, palette().textMuted, 16, 1.5f);
    dl->AddLine(ImVec2(center.x + 3.5f, center.y + 3.5f), ImVec2(center.x + 7.0f, center.y + 7.0f),
                palette().textMuted, 1.5f);

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
    return changed;
}

// ---------------------------------------------------------------------------
// Chevron (akordeon oku)
// ---------------------------------------------------------------------------
void Chevron(ImDrawList* dl, ImVec2 center, float size, float angleRadians, ImU32 color, float thickness) {
    const float s = std::sin(angleRadians);
    const float c = std::cos(angleRadians);

    auto rotate = [&](float x, float y) {
        return ImVec2(center.x + x * c - y * s, center.y + x * s + y * c);
    };

    const ImVec2 a = rotate(-size, -size * 0.5f);
    const ImVec2 b = rotate(0.0f, size * 0.5f);
    const ImVec2 d = rotate(size, -size * 0.5f);

    dl->AddLine(a, b, color, thickness);
    dl->AddLine(b, d, color, thickness);
}

// ---------------------------------------------------------------------------
// Bolum basligi
// ---------------------------------------------------------------------------
void SectionHeader(const char* label) {
    ImGui::PushFont(fonts().tiny);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(palette().textMuted));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::PopFont();

    const ImVec2 itemMin = ImGui::GetItemRectMin();
    const ImVec2 itemMax = ImGui::GetItemRectMax();
    const float lineY = (itemMin.y + itemMax.y) * 0.5f;
    const float lineStart = itemMax.x + 10.0f;
    const float lineEnd = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - 12.0f;
    if (lineEnd > lineStart) {
        ImGui::GetWindowDrawList()->AddLine(ImVec2(lineStart, lineY), ImVec2(lineEnd, lineY),
                                            withAlpha(palette().outline, 0.7f), 1.0f);
    }

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
}

} // namespace arif::gui::widgets
