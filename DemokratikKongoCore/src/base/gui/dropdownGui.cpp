#include "dropdownGui.h"

#include "guiCore.h"
#include "guiWidgets.h"

#include "../menu/menu.h"
#include "../moduleManager/moduleManager.h"
#include "../moduleManager/settings/setting.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

// Defined in renderMenu.cpp. The classic settings page uses the same flag.
extern bool s_requestDetach;

/*
========================================================================
    DemokratikKongo :: Dropdown ClickGUI
------------------------------------------------------------------------
    A second layout for the Insert menu. Categories sit in a horizontal
    row of columns (the "dropdown" look). Each column can collapse, can
    be dragged free of the row, and lists its modules as compact rows.
    Enabled modules fill with the danger colour. The three-dot control
    opens that module's settings in a side panel. A search bar and a
    gear panel sit at the top of the screen.

    Animation state is stored per ImGuiID (GetStateStorage for hover /
    enabled blends, Column::openAnim for the disclosure). Speeds go
    through Ease(), which is frame-rate independent.
========================================================================
*/

namespace Gui
{
    namespace
    {
        constexpr int kCategoryCount = static_cast<int>(Category::_Count);

        constexpr float kColW = 200.0f;
        constexpr float kHeaderH = 36.0f;
        constexpr float kRowPitch = 28.0f;
        constexpr float kRowH = 26.0f;
        constexpr float kColGap = 8.0f;
        constexpr float kRounding = 8.0f;
        constexpr float kSettingsW = 360.0f;

        constexpr ImGuiWindowFlags kOverlayFlags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav;

        struct Column
        {
            bool open = true;
            bool enabledOnly = false;
            bool userPlaced = false;
            bool dragged = false;
            float openAnim = 1.0f;
            ImVec2 pos = ImVec2(0.0f, 0.0f);
        };

        struct HeightFit
        {
            const void* key = nullptr;
            float content = 180.0f;
        };

        Column g_cols[kCategoryCount];
        char g_search[96] = {};
        Module* g_settings = nullptr;
        bool g_gear = false;
        bool g_holdModule = false;
        bool g_holdGear = false;
        HeightFit g_moduleFit;
        HeightFit g_gearFit;

        const char* kLayouts[] = { "Classic", "Dropdown" };

        bool ContainsFold(const char* haystack, const char* needle)
        {
            if (!needle || needle[0] == '\0')
                return true;
            if (!haystack)
                return false;

            auto lower = [](unsigned char ch) -> unsigned char {
                return (ch >= 'A' && ch <= 'Z') ? static_cast<unsigned char>(ch + 32) : ch;
            };

            for (const char* h = haystack; *h; ++h)
            {
                const char* a = h;
                const char* b = needle;
                while (*a && *b && lower(static_cast<unsigned char>(*a)) == lower(static_cast<unsigned char>(*b)))
                {
                    ++a;
                    ++b;
                }
                if (*b == '\0')
                    return true;
            }
            return false;
        }

        const char* SettingDesc(Setting* setting)
        {
            if (!Prefs().showDescriptions || !setting || !setting->description)
                return "";
            return setting->description;
        }

        bool IsArrayListSetting(Module* module, Setting* setting)
        {
            return module && setting && module->hideFromArrayListSetting() == setting;
        }

        bool SettingVisible(Setting* setting)
        {
            return setting && (!setting->visible || setting->visible());
        }

        ImU32 PanelBg()
        {
            return Alpha(IM_COL32(18, 18, 18, 238), GlobalOpacity());
        }

        ImU32 EnabledFill(bool hot)
        {
            const ImU32 color = hot ? IM_COL32(232, 64, 64, 255) : IM_COL32(214, 48, 48, 255);
            return Alpha(color, GlobalOpacity());
        }

        void ClampToScreen(ImVec2& pos, float width)
        {
            const ImVec2 screen = ImGui::GetIO().DisplaySize;
            const float maxX = std::max(8.0f, screen.x - std::min(width, 48.0f));
            const float maxY = std::max(8.0f, screen.y - 24.0f);
            pos.x = std::clamp(pos.x, 8.0f, maxX);
            pos.y = std::clamp(pos.y, 8.0f, maxY);
        }

        float* AnimFloat(const char* salt, float initial)
        {
            ImGuiStorage* storage = ImGui::GetStateStorage();
            return storage->GetFloatRef(ImGui::GetID(salt), initial);
        }

        void DrawSetting(Setting* setting)
        {
            if (!setting)
                return;

            const char* desc = SettingDesc(setting);

            switch (setting->type)
            {
            case SettingType::Bool:
            {
                auto* option = static_cast<BoolSetting*>(setting);
                RowToggle(option->name, desc, &option->value);
                break;
            }
            case SettingType::Number:
            {
                auto* option = static_cast<NumberSetting*>(setting);
                if (option->slotPicker)
                {
                    RowSlotPicker(option->name, desc, &option->value,
                                  static_cast<int>(option->min), static_cast<int>(option->max));
                }
                else
                {
                    RowSlider(option->name, desc, &option->value, option->min, option->max,
                              option->step >= 1.0f, option->suffix);
                }
                break;
            }
            case SettingType::Enum:
            {
                auto* option = static_cast<EnumSetting*>(setting);
                if (!option->options.empty())
                {
                    RowDropdown(option->name, desc, &option->index,
                                option->options.data(), static_cast<int>(option->options.size()));
                }
                break;
            }
            case SettingType::Keybind:
            {
                auto* option = static_cast<KeybindSetting*>(setting);
                RowKeybind(option->name, desc, &option->virtualKey, &option->listening);
                break;
            }
            case SettingType::Color:
            {
                auto* option = static_cast<ColorSetting*>(setting);
                float rgba[4] = { option->value.r, option->value.g, option->value.b, option->value.a };
                RowColor(option->name, desc, rgba);
                option->value = Color{ rgba[0], rgba[1], rgba[2], rgba[3] };
                break;
            }
            case SettingType::String:
            {
                auto* option = static_cast<StringSetting*>(setting);
                if (option->itemList)
                    RowItemList(option->name, desc, &option->value);
                else if (option->slotPicker)
                    RowSlotMultiPicker(option->name, desc, &option->value);
                else
                    RowTextInput(option->name, desc, &option->value, option->maxLength);
                break;
            }
            case SettingType::IntRange:
            {
                auto* option = static_cast<IntRangeSetting*>(setting);
                float low = static_cast<float>(option->low);
                float high = static_cast<float>(option->high);
                if (RowRange(option->name, desc, &low, &high,
                             static_cast<float>(option->min), static_cast<float>(option->max),
                             true, option->suffix))
                {
                    option->low = static_cast<int>(std::lround(low));
                    option->high = static_cast<int>(std::lround(high));
                }
                break;
            }
            case SettingType::FloatRange:
            {
                auto* option = static_cast<FloatRangeSetting*>(setting);
                RowRange(option->name, desc, &option->low, &option->high,
                         option->min, option->max, false, option->suffix);
                break;
            }
            case SettingType::Action:
            {
                auto* option = static_cast<ActionSetting*>(setting);
                const std::string face = option->label();
                if (RowAction(option->name, desc, face.c_str()) && option->onClick)
                    option->onClick();
                break;
            }
            default:
                break;
            }

            if (RowRightClicked() && setting->hasDefault)
                setting->resetToDefault();
        }

        void DrawModuleSettingRows(Module* module)
        {
            if (!module)
                return;

            if (module->keybindEditable())
            {
                KeybindSetting& bind = module->keybind();
                RowKeybind("Keybind",
                           Prefs().showDescriptions ? "Toggle this module while playing" : "",
                           &bind.virtualKey, &bind.listening);
                if (RowRightClicked() && bind.hasDefault)
                    bind.resetToDefault();
            }

            for (Setting* setting : module->settings())
            {
                if (SettingVisible(setting) && !IsArrayListSetting(module, setting))
                    DrawSetting(setting);
            }
        }

        void DrawClientSettingRows()
        {
            const bool desc = Prefs().showDescriptions;

            SectionLabel("LAYOUT", 2.0f);
            RowDropdown("Menu layout",
                        desc ? "Classic window, or category columns you can drag apart" : "",
                        &Prefs().menuLayout, kLayouts, 2);
            if (Prefs().menuLayout != 1)
                g_gear = false;

            SectionLabel("INTERFACE");
            RowToggle("Reduce motion", desc ? "Snap animations instead of easing them" : "",
                      &Prefs().reduceMotion);
            RowToggle("Show descriptions", desc ? "Display helper text under every option" : "",
                      &Prefs().showDescriptions);
            RowToggle("Toggle notifications", desc ? "Pop a toast when a module changes state" : "",
                      &Prefs().notifyOnToggle);

            SectionLabel("MENU");
            {
                static bool listening = false;
                if (RowKeybind("Open menu", desc ? "Key that shows and hides this menu" : "",
                               &Menu::Keybind, &listening))
                {
                    if (Menu::Keybind == 0)
                        Menu::Keybind = VK_INSERT;
                }
            }

            if (RowAction("Detach", desc ? "Detach the client from the game process" : "", "Detach"))
                s_requestDetach = true;
        }

        void DrawFittedPanel(const char* id, ImVec2 pos, float opacity, HeightFit& fit,
                             const void* key, float fallback, const char* title,
                             bool* openFlag, bool& hold, void (*drawRows)(void*), void* rowsArg)
        {
            const ImVec2 screen = ImGui::GetIO().DisplaySize;
            const float maxBody = std::max(72.0f, screen.y - pos.y - 56.0f);
            if (fit.key != key)
            {
                fit.key = key;
                fit.content = fallback;
            }
            const float body = std::clamp(fit.content, 36.0f, maxBody);
            const float titleH = 40.0f;
            const bool overflow = fit.content > maxBody + 1.0f;

            ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(kSettingsW, titleH + body), ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, opacity);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, kRounding);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

            if (ImGui::Begin(id, nullptr, kOverlayFlags))
            {
                const ImVec2 wp = ImGui::GetWindowPos();
                const ImVec2 ws = ImGui::GetWindowSize();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const Palette& c = Colors();

                ImGui::GetBackgroundDrawList()->AddShadowRect(
                    wp, ImVec2(wp.x + ws.x, wp.y + ws.y),
                    Alpha(IM_COL32(0, 0, 0, 170), opacity), 24.0f, ImVec2(0.0f, 8.0f),
                    ImDrawFlags_ShadowCutOutShapeBackground, kRounding);
                dl->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), PanelBg(), kRounding);
                dl->AddRect(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), Alpha(c.border, 0.85f), kRounding, 0, 1.0f);

                TextEllipsis(dl, FontBold(), 14.0f, ImVec2(wp.x + 14.0f, wp.y + 12.0f),
                             kSettingsW - 52.0f, c.text, title ? title : "");

                ImGui::SetCursorScreenPos(ImVec2(wp.x + kSettingsW - 34.0f, wp.y + 6.0f));
                if (IconButton("##closePanel", Icon::Close, 26.0f, false) && openFlag)
                {
                    *openFlag = false;
                    hold = true;
                }

                dl->AddLine(ImVec2(wp.x + 10.0f, wp.y + titleH - 1.0f),
                            ImVec2(wp.x + ws.x - 10.0f, wp.y + titleH - 1.0f),
                            Alpha(c.line, 0.9f), 1.0f);

                ImGui::SetCursorScreenPos(ImVec2(wp.x, wp.y + titleH));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
                const ImGuiWindowFlags childFlags = overflow ? 0 : ImGuiWindowFlags_NoScrollbar;
                ImGui::BeginChild("##panelBody", ImVec2(kSettingsW, body),
                                  ImGuiChildFlags_AlwaysUseWindowPadding, childFlags);
                if (drawRows)
                    drawRows(rowsArg);
                fit.content = ImGui::GetCursorPosY() + ImGui::GetStyle().WindowPadding.y;
                ImGui::EndChild();
                ImGui::PopStyleVar(2);

                const ImVec2 mouse = ImGui::GetIO().MousePos;
                const bool inside = mouse.x >= wp.x && mouse.y >= wp.y &&
                                    mouse.x <= wp.x + ws.x && mouse.y <= wp.y + ws.y;
                if (inside || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup))
                    hold = true;
            }
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(5);
        }

        void DrawModuleRowsThunk(void* arg)
        {
            DrawModuleSettingRows(static_cast<Module*>(arg));
        }

        void DrawClientRowsThunk(void*)
        {
            DrawClientSettingRows();
        }

        void DrawModulePanel(float opacity)
        {
            if (!g_settings)
                return;

            const int cat = static_cast<int>(g_settings->category());
            ImVec2 anchor = (cat >= 0 && cat < kCategoryCount) ? g_cols[cat].pos : ImVec2(80.0f, 80.0f);
            ImVec2 pos(anchor.x + kColW + 8.0f, anchor.y);
            const ImVec2 screen = ImGui::GetIO().DisplaySize;
            if (pos.x + kSettingsW > screen.x - 8.0f)
                pos.x = anchor.x - kSettingsW - 8.0f;
            if (pos.x < 8.0f)
                pos.x = 8.0f;
            if (pos.y > screen.y - 96.0f)
                pos.y = std::max(8.0f, screen.y - 96.0f);

            bool open = true;
            bool hold = false;
            DrawFittedPanel("##dropdownModule", pos, opacity, g_moduleFit, g_settings, 180.0f,
                            g_settings->name(), &open, hold, DrawModuleRowsThunk, g_settings);
            if (!open)
                g_settings = nullptr;
            if (hold)
                g_holdModule = true;
        }

        void DrawGearPanel(float opacity)
        {
            if (!g_gear)
                return;

            const ImVec2 screen = ImGui::GetIO().DisplaySize;
            constexpr float kSearchW = 280.0f;
            constexpr float kBarH = 34.0f;
            const float barW = kSearchW + 8.0f + kBarH;
            const float barX = (screen.x - barW) * 0.5f;
            ImVec2 pos(barX + barW - kSettingsW, 16.0f + kBarH + 8.0f);
            if (pos.x < 8.0f)
                pos.x = 8.0f;

            bool open = true;
            bool hold = false;
            DrawFittedPanel("##dropdownGear", pos, opacity, g_gearFit, &g_gear, 280.0f,
                            "Menu", &open, hold, DrawClientRowsThunk, nullptr);
            if (!open)
                g_gear = false;
            if (hold)
                g_holdGear = true;
        }

        struct Listed
        {
            Module* items[48] = {};
            int shown = 0;
            int matched = 0;
            int enabled = 0;
        };

        Listed Collect(Category category, bool enabledOnly)
        {
            Listed out;
            const auto modules = ModuleManager::ByCategory(category);
            for (Module* module : modules)
            {
                if (!module)
                    continue;
                if (module->toggleable() && module->isEnabled())
                    ++out.enabled;

                const bool matched = ContainsFold(module->name(), g_search) ||
                                     ContainsFold(module->description(), g_search);
                if (!matched)
                    continue;
                ++out.matched;
                if (enabledOnly && module->toggleable() && !module->isEnabled())
                    continue;
                if (out.shown < static_cast<int>(sizeof(out.items) / sizeof(out.items[0])))
                    out.items[out.shown++] = module;
            }

            std::sort(out.items, out.items + out.shown, [](const Module* a, const Module* b)
            {
                if (!a || !b)
                    return a != nullptr;
                return std::strcmp(a->name(), b->name()) < 0;
            });
            return out;
        }

        void DrawModuleRow(Module* module, float rowW)
        {
            if (!module)
                return;

            ImGui::PushID(module);
            const Palette& c = Colors();
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const ImVec2 rowMin(origin.x + 4.0f, origin.y + (kRowPitch - kRowH) * 0.5f);
            const ImVec2 rowMax(rowMin.x + rowW, rowMin.y + kRowH);

            ImGui::SetCursorScreenPos(rowMin);
            ImGui::InvisibleButton("##mod", ImVec2(rowW, kRowH));
            const bool hovered = ImGui::IsItemHovered();
            const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            const bool right = ImGui::IsItemClicked(ImGuiMouseButton_Right);

            const ImVec2 dotsMin(rowMax.x - 22.0f, rowMin.y + (kRowH - 18.0f) * 0.5f);
            const ImVec2 dotsMax(dotsMin.x + 18.0f, dotsMin.y + 18.0f);
            const bool overDots = hovered && ImGui::IsMouseHoveringRect(dotsMin, dotsMax, true);

            const bool enabled = module->toggleable() && module->isEnabled();
            float* hover = AnimFloat("##hov", 0.0f);
            float* on = AnimFloat("##on", enabled ? 1.0f : 0.0f);
            Ease(*hover, (hovered && !overDots) ? 1.0f : 0.0f, 16.0f);
            Ease(*on, enabled ? 1.0f : 0.0f, 16.0f);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImU32 idle = Alpha(IM_COL32(255, 255, 255, 16), GlobalOpacity());
            const ImU32 fill = Mix(Mix(IM_COL32(0, 0, 0, 0), idle, *hover),
                                   EnabledFill(*hover > 0.45f), *on);
            if ((*on > 0.01f) || (*hover > 0.01f))
                dl->AddRectFilled(rowMin, rowMax, fill, 3.0f);

            const ImU32 label = Mix(c.text, Alpha(IM_COL32(255, 255, 255, 255), GlobalOpacity()), *on);
            TextEllipsis(dl, FontRegular(), 13.0f,
                         ImVec2(rowMin.x + 8.0f, rowMin.y + (kRowH - 13.0f) * 0.5f),
                         std::max(24.0f, rowW - 34.0f), label, module->name());

            const ImVec2 dotsCenter((dotsMin.x + dotsMax.x) * 0.5f, (dotsMin.y + dotsMax.y) * 0.5f);
            DrawIcon(dl, Icon::Dots, dotsCenter, 13.0f,
                     overDots ? c.text : Mix(c.textMute, label, *on), 1.8f);

            if (overDots)
                SetTooltip("Settings");
            else if (hovered && Prefs().showDescriptions && module->description() && module->description()[0])
                SetTooltip("%s", module->description());

            if (clicked && overDots)
            {
                g_settings = (g_settings == module) ? nullptr : module;
                g_holdModule = true;
            }
            else if (clicked && module->toggleable())
            {
                module->toggle();
                if (Prefs().notifyOnToggle && Prefs().notifyModuleToggles)
                {
                    Notify(module->name(),
                           module->isEnabled() ? "Module enabled" : "Module disabled",
                           module->isEnabled() ? Icon::Check : Icon::Close,
                           module->isEnabled() ? c.good : c.textMute);
                }
            }
            else if ((clicked && !module->toggleable()) || right)
            {
                g_settings = module;
                g_holdModule = true;
            }

            ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + kRowPitch));
            ImGui::PopID();
        }

        void DrawColumn(int index, const Listed& listed, float opacity, int flowIndex, int flowCount)
        {
            Column& col = g_cols[index];
            const ImVec2 screen = ImGui::GetIO().DisplaySize;
            const float totalW = static_cast<float>(flowCount) * kColW +
                                 static_cast<float>(std::max(0, flowCount - 1)) * kColGap;
            const float startX = std::max(12.0f, (screen.x - totalW) * 0.5f);
            if (!col.userPlaced)
            {
                col.pos.x = startX + static_cast<float>(flowIndex) * (kColW + kColGap);
                col.pos.y = 68.0f;
            }
            ClampToScreen(col.pos, kColW);

            const float openTarget = col.open ? 1.0f : 0.0f;
            Ease(col.openAnim, openTarget, 16.0f);
            if (std::fabs(col.openAnim - openTarget) < 0.004f)
                col.openAnim = openTarget;

            const float maxBody = std::max(kRowPitch, screen.y - col.pos.y - kHeaderH - 16.0f);
            const float fullBody = listed.shown > 0
                                       ? static_cast<float>(listed.shown) * kRowPitch + 4.0f
                                       : 32.0f;
            const float targetBody = std::min(fullBody, maxBody);
            const float bodyH = col.openAnim * targetBody;
            const bool scroll = fullBody > maxBody + 0.5f && col.openAnim > 0.98f;

            ImGui::SetNextWindowPos(col.pos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(kColW, kHeaderH + bodyH), ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, opacity);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, kRounding);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

            char id[32];
            std::snprintf(id, sizeof(id), "##dropdownCol%d", index);

            if (ImGui::Begin(id, nullptr, kOverlayFlags))
            {
                const ImVec2 wp = ImGui::GetWindowPos();
                const ImVec2 winMax(wp.x + kColW, wp.y + kHeaderH + bodyH);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const Palette& c = Colors();
                const bool selected = g_settings && static_cast<int>(g_settings->category()) == index;

                ImGui::GetBackgroundDrawList()->AddShadowRect(
                    wp, winMax, Alpha(IM_COL32(0, 0, 0, 160), opacity), 22.0f, ImVec2(0.0f, 8.0f),
                    ImDrawFlags_ShadowCutOutShapeBackground, kRounding);
                dl->AddRectFilled(wp, winMax, PanelBg(), kRounding);
                dl->AddRect(wp, winMax, selected ? Alpha(c.accent, 0.85f) : Alpha(c.border, 0.75f),
                            kRounding, 0, 1.0f);

                ImGui::SetCursorScreenPos(wp);
                ImGui::InvisibleButton("##header", ImVec2(kColW, kHeaderH));
                const bool headerHovered = ImGui::IsItemHovered();
                const bool headerActive = ImGui::IsItemActive();

                const float midY = wp.y + kHeaderH * 0.5f;
                const ImVec2 chevronCenter(wp.x + kColW - 16.0f, midY);
                const ImVec2 eyeCenter(wp.x + kColW - 38.0f, midY);
                const bool overChevron = headerHovered &&
                    ImGui::IsMouseHoveringRect(ImVec2(chevronCenter.x - 10.0f, wp.y),
                                               ImVec2(chevronCenter.x + 10.0f, wp.y + kHeaderH), true);
                const bool overEye = headerHovered &&
                    ImGui::IsMouseHoveringRect(ImVec2(eyeCenter.x - 10.0f, wp.y),
                                               ImVec2(eyeCenter.x + 10.0f, wp.y + kHeaderH), true);

                ImGuiIO& io = ImGui::GetIO();
                if (headerActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f))
                {
                    if (col.dragged || (!overEye && !overChevron))
                    {
                        col.dragged = true;
                        col.userPlaced = true;
                        col.pos.x += io.MouseDelta.x;
                        col.pos.y += io.MouseDelta.y;
                        ClampToScreen(col.pos, kColW);
                        ImGui::SetWindowPos(col.pos);
                    }
                }

                const bool released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
                if (released && headerHovered && !col.dragged)
                {
                    if (overEye)
                        col.enabledOnly = !col.enabledOnly;
                    else
                        col.open = !col.open;
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    col.userPlaced = false;
                if (released)
                    col.dragged = false;

                float* headerHover = AnimFloat("##headerHover", 0.0f);
                Ease(*headerHover, headerHovered ? 1.0f : 0.0f, 14.0f);

                const ImU32 titleCol = selected ? c.accent : c.text;
                DrawIcon(dl, IconForModule(nullptr, index), ImVec2(wp.x + 16.0f, midY), 14.0f,
                         selected ? c.accent : Mix(c.textDim, c.text, *headerHover), 1.8f);

                char countBuf[8];
                std::snprintf(countBuf, sizeof(countBuf), "%d", listed.enabled);
                const ImVec2 countSize = TextSize(FontRegular(), 11.0f, countBuf);
                const float pillW = countSize.x + 10.0f;
                const ImVec2 pillMax(eyeCenter.x - 12.0f, midY + 8.0f);
                const ImVec2 pillMin(pillMax.x - pillW, midY - 8.0f);
                dl->AddRectFilled(pillMin, pillMax, Alpha(c.widget, 0.95f), 4.0f);
                TextIn(dl, FontRegular(), 11.0f, pillMin, pillMax, c.textDim, countBuf, ImVec2(0.5f, 0.5f));

                TextEllipsis(dl, FontBold(), 13.0f, ImVec2(wp.x + 30.0f, midY - 7.0f),
                             std::max(20.0f, pillMin.x - (wp.x + 34.0f)), titleCol,
                             CategoryName(static_cast<Category>(index)));

                DrawIcon(dl, Icon::Eye, eyeCenter, 12.0f,
                         col.enabledOnly ? c.accent : c.textMute, 1.7f);
                Chevron(dl, chevronCenter, 9.0f, col.openAnim, Mix(c.textMute, c.text, *headerHover), 1.7f);

                if (overEye)
                    SetTooltip(col.enabledOnly ? "Show every module" : "Show enabled only");
                else if (overChevron)
                    SetTooltip(col.open ? "Collapse" : "Expand");

                if (bodyH > 2.0f)
                {
                    ImGui::SetCursorScreenPos(ImVec2(wp.x, wp.y + kHeaderH));
                    const ImGuiWindowFlags childFlags =
                        scroll ? 0 : (ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                    ImGui::BeginChild("##colBody", ImVec2(kColW, bodyH), ImGuiChildFlags_None, childFlags);
                    if (listed.shown == 0)
                    {
                        const char* empty = g_search[0] ? "No matches" :
                                            (col.enabledOnly ? "None enabled" : "Empty");
                        Text(ImGui::GetWindowDrawList(), FontRegular(), 12.0f,
                             ImVec2(wp.x + 12.0f, wp.y + kHeaderH + 8.0f), c.textMute, empty);
                    }
                    else
                    {
                        const float rowW = kColW - 8.0f - (scroll ? 8.0f : 0.0f);
                        for (int i = 0; i < listed.shown; ++i)
                            DrawModuleRow(listed.items[i], rowW);
                    }
                    ImGui::EndChild();
                }
            }
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(5);
        }

        void DrawSearchBar(float opacity)
        {
            const ImVec2 screen = ImGui::GetIO().DisplaySize;
            constexpr float kSearchW = 280.0f;
            constexpr float kBarH = 34.0f;
            const float barW = kSearchW + 8.0f + kBarH;
            const ImVec2 pos((screen.x - barW) * 0.5f, 16.0f);

            ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(barW, kBarH), ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, opacity);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

            if (ImGui::Begin("##dropdownSearch", nullptr, kOverlayFlags))
            {
                ImGui::SetCursorScreenPos(pos);
                SearchField("##dropdownFind", g_search, sizeof(g_search), kSearchW, kBarH);

                ImGui::SetCursorScreenPos(ImVec2(pos.x + kSearchW + 8.0f, pos.y));
                if (IconButton("##gear", Icon::Gear, kBarH, g_gear))
                {
                    g_gear = !g_gear;
                    g_holdGear = true;
                }
                if (ImGui::IsItemHovered())
                    SetTooltip("Menu settings");
            }
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(3);
        }

        void DrawDim(float opacity)
        {
            if (opacity <= 0.004f)
                return;
            ImDrawList* dl = ImGui::GetBackgroundDrawList();
            const ImVec2 screen = ImGui::GetIO().DisplaySize;
            dl->AddRectFilled(ImVec2(0.0f, 0.0f), screen, Alpha(IM_COL32(0, 0, 0, 80), opacity));
        }
    }

    void ResetDropdown()
    {
        for (Column& col : g_cols)
            col = Column{};
        g_search[0] = '\0';
        g_settings = nullptr;
        g_gear = false;
        g_holdModule = false;
        g_holdGear = false;
        g_moduleFit = HeightFit{};
        g_gearFit = HeightFit{};
    }

    void RenderDropdown(float opacity)
    {
        g_holdModule = false;
        g_holdGear = false;
        const bool popupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);

        DrawDim(opacity);

        int flowCount = 0;
        Listed listed[kCategoryCount];
        bool visible[kCategoryCount] = {};
        for (int i = 0; i < kCategoryCount; ++i)
        {
            listed[i] = Collect(static_cast<Category>(i), g_cols[i].enabledOnly);
            const bool hide = g_search[0] != '\0' && listed[i].matched == 0;
            visible[i] = !hide;
            if (visible[i] && !g_cols[i].userPlaced)
                ++flowCount;
        }

        int flowIndex = 0;
        for (int i = 0; i < kCategoryCount; ++i)
        {
            if (!visible[i])
                continue;
            const int slot = g_cols[i].userPlaced ? 0 : flowIndex++;
            DrawColumn(i, listed[i], opacity, slot, std::max(flowCount, 1));
        }

        DrawSearchBar(opacity);
        if (g_settings)
            DrawModulePanel(opacity);
        if (g_gear)
            DrawGearPanel(opacity);

        bool anyVisible = false;
        for (int i = 0; i < kCategoryCount; ++i)
            anyVisible = anyVisible || visible[i];

        if (!anyVisible && g_search[0] != '\0')
        {
            const ImVec2 screen = ImGui::GetIO().DisplaySize;
            const char* msg = "No matches";
            const ImVec2 size = TextSize(FontRegular(), 14.0f, msg);
            Text(ImGui::GetForegroundDrawList(), FontRegular(), 14.0f,
                 ImVec2((screen.x - size.x) * 0.5f, 120.0f), Colors().textMute, msg);
        }

        const bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                           ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if (click && !popupOpen)
        {
            if (!g_holdModule)
                g_settings = nullptr;
            if (!g_holdGear)
                g_gear = false;
        }
    }
}
