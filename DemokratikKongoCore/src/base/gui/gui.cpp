#include "gui.h"

#include "dropdownGui.h"
#include "guiCore.h"
#include "guiWidgets.h"

#include "../config/config.h"
#include "../menu/menu.h"
#include "../moduleManager/moduleManager.h"
#include "../moduleManager/modules/combat/friends.h"
#include "../moduleManager/modules/misc/fakeLogin.h"
#include "../moduleManager/modules/utility/itemLogger.h"
#include "../util/keybindUtil.h"
#include "../util/trimmer.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

// Set by the Detach action; consumed by renderMenu.cpp on the cheat thread.
extern bool s_requestDetach;

/*
========================================================================
    DemokratikKongo :: ClickGUI
------------------------------------------------------------------------
    +------------------------------------------------------------------+
    | [Home v] [F] [C] [S]          search.................      [X]   |
    +--------+---------------------------------------------------------+
    | Combat>| | page title / modules / manager pages                  |
    | Move   | |                                                       |
    | Render | |                                                       |
    | ------ | |                                                       |
    | ## ## #| |  (active module icons, 3-wide, Home only)             |
    +--------+---------------------------------------------------------+
========================================================================
*/

namespace
{
    using namespace Gui;

    constexpr float kSidebarWidth = 168.0f;
    constexpr float kTopBarHeight = 64.0f;
    constexpr float kContentPad = 22.0f;
    constexpr float kCardHeaderHeight = 66.0f;
    constexpr float kCardSpacing = 9.0f;

    // Top-level shell pages (horizontal expanding tabs).
    enum Shell
    {
        Shell_Home = 0,
        Shell_Friends,
        Shell_Config,
        Shell_Settings,
    };

    struct NavEntry
    {
        const char* label;
        Icon icon;
        Category category;
    };

    const NavEntry kNav[] = {
        { "Combat",   Icon::Sword,  Category::Combat   },
        { "Movement", Icon::Run,    Category::Movement },
        { "Render",   Icon::Eye,    Category::Render   },
        { "Utility",  Icon::Wrench, Category::Utility  },
        { "Misc",     Icon::Grid,   Category::Misc     },
    };

    constexpr int kNavCount = static_cast<int>(sizeof(kNav) / sizeof(kNav[0]));

    struct GuiState
    {
        int shell = Shell_Home;
        int category = 0;       // index into kNav while on Home
        int lastView = -1;
        float pageFade = 1.0f;
        float openAnim = 0.0f;
        float fakeLoginAnim = 0.0f;
        float itemLogAnim = 0.0f;
        char search[96] = {};
        bool styled = false;
    };

    GuiState g;

    // Preferences live in guiCore so the profile serialiser can reach them.
    bool& ReduceMotion()     { return Prefs().reduceMotion; }
    bool& ShowDescriptions() { return Prefs().showDescriptions; }
    bool& NotifyOnToggle()   { return Prefs().notifyOnToggle; }

    Icon CategoryIcon(Category category)
    {
        return IconForModule(nullptr, static_cast<int>(category));
    }

    // ------------------------------------------------------------ favourites

    std::set<std::string> g_favorites;
    bool g_favoritesLoaded = false;

    void EnsureFavoritesLoaded()
    {
        if (g_favoritesLoaded)
            return;
        g_favoritesLoaded = true;

        const std::string& csv = Prefs().favoritesCsv;
        size_t start = 0;
        while (start <= csv.size())
        {
            const size_t comma = csv.find(',', start);
            const size_t end = (comma == std::string::npos) ? csv.size() : comma;
            std::string token = csv.substr(start, end - start);
            size_t b = 0, e = token.size();
            while (b < e && token[b] == ' ') ++b;
            while (e > b && token[e - 1] == ' ') --e;
            if (b < e)
                g_favorites.insert(token.substr(b, e - b));
            if (comma == std::string::npos)
                break;
            start = comma + 1;
        }
    }

    bool IsFavorite(const char* name)
    {
        EnsureFavoritesLoaded();
        return name && g_favorites.count(name) != 0;
    }

    void ToggleFavorite(const char* name)
    {
        if (!name || !*name)
            return;
        EnsureFavoritesLoaded();
        if (!g_favorites.erase(name))
            g_favorites.insert(name);

        std::string csv;
        for (const std::string& f : g_favorites)
        {
            if (!csv.empty())
                csv += ",";
            csv += f;
        }
        Prefs().favoritesCsv = csv;

        if (Prefs().notifyModuleToggles)
            Notify("Favourites",
                   g_favorites.count(name) ? "Pinned to top" : "Removed from favourites",
                   Icon::Star, g_favorites.count(name) ? Colors().accent : Colors().textMute);
    }

    bool ContainsFold(const char* haystack, const char* needle)
    {
        if (!haystack || !needle)
            return false;

        std::string a(haystack);
        std::string b(needle);
        std::transform(a.begin(), a.end(), a.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        std::transform(b.begin(), b.end(), b.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return a.find(b) != std::string::npos;
    }

    bool MatchesSearch(Module* module)
    {
        if (g.search[0] == '\0')
            return true;
        if (ContainsFold(module->name(), g.search) ||
            ContainsFold(module->description(), g.search))
            return true;

        // Also match setting names and the current enum option label so users
        // can search by what a module does rather than just its display name.
        for (Setting* setting : module->settings())
        {
            if (ContainsFold(setting->name, g.search))
                return true;
            if (setting->type == SettingType::Enum)
            {
                auto* option = static_cast<EnumSetting*>(setting);
                if (option->index >= 0 && option->index < static_cast<int>(option->options.size()) &&
                    ContainsFold(option->options[option->index], g.search))
                    return true;
            }
        }
        return false;
    }

    bool SettingVisible(Setting* setting)
    {
        return setting && (!setting->visible || setting->visible());
    }

    const char* SettingDesc(Setting* setting)
    {
        if (!ShowDescriptions() || !setting->description)
            return "";
        return setting->description;
    }

    // The ArrayList visibility flag is driven from an icon on the module card,
    // so it must not appear in the module's own settings list.
    bool IsArrayListSetting(Module* module, Setting* setting)
    {
        return module && setting && module->hideFromArrayListSetting() == setting;
    }

    float SettingRowHeight(Setting* setting, float contentWidth)
    {
        const char* desc = SettingDesc(setting);
        const bool tall = desc && *desc;

        switch (setting->type)
        {
        case SettingType::Number:
        {
            auto* option = static_cast<NumberSetting*>(setting);
            if (option->slotPicker)
                return tall ? 62.0f : 52.0f;
            return tall ? 62.0f : 50.0f;
        }
        case SettingType::IntRange:
        case SettingType::FloatRange: return tall ? 64.0f : 52.0f;
        case SettingType::Enum:
        case SettingType::Keybind:
        case SettingType::Color:
        case SettingType::Action:     return tall ? 50.0f : 40.0f;
        case SettingType::String:
        {
            auto* option = static_cast<StringSetting*>(setting);
            if (option->itemList)
                return MeasureItemList(desc, option->value, contentWidth);
            if (option->slotPicker)
                return tall ? 62.0f : 52.0f;
            return tall ? 52.0f : 42.0f;
        }
        case SettingType::Bool:
        default:                      return tall ? 46.0f : 36.0f;
        }
    }

    void DrawSetting(Setting* setting)
    {
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

    float MeasureModuleSettings(Module* module, float contentWidth)
    {
        float height = 0.0f;
        if (module->keybindEditable())
            height += ShowDescriptions() ? 50.0f : 40.0f;

        for (Setting* setting : module->settings())
        {
            if (SettingVisible(setting) && !IsArrayListSetting(module, setting))
                height += SettingRowHeight(setting, contentWidth);
        }
        return height;
    }

    void DrawModuleSettings(Module* module)
    {
        if (module->keybindEditable())
        {
            KeybindSetting& bind = module->keybind();
            RowKeybind("Keybind",
                       ShowDescriptions() ? "Toggle this module while playing" : "",
                       &bind.virtualKey, &bind.listening);
        }

        for (Setting* setting : module->settings())
        {
            if (SettingVisible(setting) && !IsArrayListSetting(module, setting))
                DrawSetting(setting);
        }
    }

    void DrawModuleCard(Module* module, int index)
    {
        const Palette& c = Colors();
        ImGui::PushID(module);

        const float width = ImGui::GetContentRegionAvail().x;
        const bool enabled = module->isEnabled();

        // Staggered entrance: cards cascade down when the page changes. Derived
        // from the shared page fade with a per-index delay so nothing needs to
        // be stored between frames.
        const float delay = std::min(0.22f, static_cast<float>(index) * 0.028f);
        const float entrance = SmoothStep(Clamp01((g.pageFade - delay) / (1.0f - delay)));
        const float slide = (1.0f - entrance) * 12.0f;

        // Animated disclosure height. Measuring walks every setting and text-wraps
        // item-list CSVs, so skip it for cards that are collapsed and no longer
        // animating - their target height is 0 regardless.
        const bool needsMeasure = module->expanded() || module->expandAnim() > 0.5f;
        const float settingsHeight = needsMeasure
            ? MeasureModuleSettings(module, width - 32.0f)
            : 0.0f;
        const float target = module->expanded() ? settingsHeight + 16.0f : 0.0f;
        Ease(module->expandAnim(), target, 15.0f);
        if (std::fabs(module->expandAnim() - target) < 0.5f)
            module->expandAnim() = target;
        const float extra = std::max(0.0f, module->expandAnim());

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 base = ImGui::GetCursorScreenPos();
        const ImVec2 cardMin(base.x, base.y + slide);
        ImGui::SetCursorScreenPos(cardMin);
        const ImVec2 cardMax(cardMin.x + width, cardMin.y + kCardHeaderHeight + extra);

        const ImGuiID id = ImGui::GetID("##card");
        ImGui::InvisibleButton("##card", ImVec2(width, kCardHeaderHeight));
        const bool hovered = ImGui::IsItemHovered();
        const bool pressed = ImGui::IsItemClicked(ImGuiMouseButton_Left);

        const ImVec2 swMax(cardMax.x - 52.0f, cardMin.y + kCardHeaderHeight * 0.5f + 11.0f);
        const ImVec2 swMin(swMax.x - 42.0f, swMax.y - 22.0f);
        const ImVec2 chevronCenter(cardMax.x - 26.0f, cardMin.y + kCardHeaderHeight * 0.5f);

        // ArrayList visibility lives on the card, not in the settings list.
        BoolSetting* arrayListFlag = module->hideFromArrayListSetting();
        const bool showArrayListToggle = arrayListFlag && module->toggleable();
        const ImVec2 eyeMin(swMin.x - 36.0f, cardMin.y + kCardHeaderHeight * 0.5f - 13.0f);
        const ImVec2 eyeMax(eyeMin.x + 26.0f, eyeMin.y + 26.0f);

        const bool overSwitch = hovered && module->toggleable() &&
                                ImGui::IsMouseHoveringRect(swMin, swMax, false);
        const bool overEye = showArrayListToggle &&
                             ImGui::IsMouseHoveringRect(eyeMin, eyeMax, false);

        // Favourite pin, top-right corner of the header.
        const ImVec2 favCenter(cardMax.x - 18.0f, cardMin.y + 16.0f);
        const ImVec2 favMin(favCenter.x - 9.0f, favCenter.y - 9.0f);
        const ImVec2 favMax(favCenter.x + 9.0f, favCenter.y + 9.0f);
        const bool favorite = IsFavorite(module->name());
        const bool overFav = hovered && ImGui::IsMouseHoveringRect(favMin, favMax, false);

        if (pressed && overFav)
        {
            ToggleFavorite(module->name());
        }
        else if (pressed && !overEye)
        {
            if (overSwitch)
            {
                module->toggle();
                if (NotifyOnToggle() && Prefs().notifyModuleToggles)
                {
                    Notify(module->name(),
                           module->isEnabled() ? "Module enabled" : "Module disabled",
                           module->isEnabled() ? Icon::Check : Icon::Close,
                           module->isEnabled() ? c.good : c.textMute);
                }
            }
            else
            {
                module->expanded() = !module->expanded();
            }
        }

        struct CardState
        {
            float hover = 0.0f;
            float on = 0.0f;
            float open = 0.0f;
            float fav = 0.0f;
        };
        CardState* state = State<CardState>(id);
        Ease(state->hover, hovered ? 1.0f : 0.0f, 14.0f);
        Ease(state->on, enabled ? 1.0f : 0.0f, 14.0f);
        Ease(state->open, module->expanded() ? 1.0f : 0.0f, 15.0f);
        Ease(state->fav, favorite ? 1.0f : 0.0f, 14.0f);

        // Card body: hover lift + accent wash when the module is running.
        const ImU32 body = Mix(Mix(c.card, c.cardHover, state->hover),
                               Mix(c.card, c.accent, 0.16f), state->on);
        dl->AddRectFilled(cardMin, cardMax, body, 11.0f);
        if (state->on > 0.01f)
        {
            dl->AddRect(cardMin, cardMax, Alpha(c.accent, 0.42f * state->on), 11.0f, 0, 1.0f);
            dl->AddRectFilled(ImVec2(cardMin.x, cardMin.y + 15.0f),
                              ImVec2(cardMin.x + 3.0f, cardMin.y + kCardHeaderHeight - 15.0f),
                              Alpha(c.accent, state->on), 2.0f);
        }

        // Icon tile.
        const ImVec2 tileMin(cardMin.x + 14.0f, cardMin.y + 11.0f);
        const ImVec2 tileMax(tileMin.x + 44.0f, tileMin.y + 44.0f);
        dl->AddRectFilled(tileMin, tileMax, Mix(c.widget, c.accent, state->on * 0.9f), 11.0f);
        DrawIcon(dl, IconForModule(module->name(), static_cast<int>(module->category())),
                 ImVec2((tileMin.x + tileMax.x) * 0.5f, (tileMin.y + tileMax.y) * 0.5f),
                 19.0f,
                 state->on > 0.5f ? c.onAccent : Mix(c.textDim, c.text, state->hover), 2.0f);

        // Title + subtitle.
        const float textLeft = tileMax.x + 14.0f;
        const float textRight = swMin.x - 16.0f;
        const float textWidth = std::max(40.0f, textRight - textLeft);

        TextEllipsis(dl, FontRegular(), 14.0f, ImVec2(textLeft, cardMin.y + 15.0f),
                     textWidth, c.text, module->name());

        const std::string suffix = module->arrayListSuffix(SuffixDetail::Basic);
        std::string sub = module->description() ? module->description() : "";
        if (!suffix.empty())
            sub = suffix + "  -  " + sub;

        TextEllipsis(dl, FontRegular(), 11.0f, ImVec2(textLeft, cardMin.y + 35.0f),
                     textWidth, enabled ? Alpha(c.text, 0.60f) : c.textMute, sub.c_str());

        // Keybind pill.
        if (module->keybindEditable() && module->keybind().virtualKey != 0)
        {
            char keyName[64];
            const char* text = KeybindUtil::FormatKeyName(module->keybind().virtualKey,
                                                          keyName, sizeof(keyName));
            const ImVec2 size = TextSize(FontRegular(), 10.0f, text);
            const float pillRight = showArrayListToggle ? eyeMin.x - 10.0f : swMin.x - 14.0f;
            const ImVec2 pillMax(pillRight, cardMin.y + kCardHeaderHeight * 0.5f + 9.0f);
            const ImVec2 pillMin(pillMax.x - size.x - 14.0f, pillMax.y - 18.0f);
            if (pillMin.x > textLeft + 60.0f)
            {
                dl->AddRectFilled(pillMin, pillMax, Alpha(c.widget, 0.95f), 5.0f);
                TextIn(dl, FontRegular(), 10.0f, pillMin, pillMax, c.textMute, text, ImVec2(0.5f, 0.5f));
            }
        }

        if (showArrayListToggle)
        {
            // Icon shows the resulting state: open eye = listed in ArrayList.
            const bool listed = !arrayListFlag->value;
            if (MiniIconToggle("##arraylist", listed ? Icon::Eye : Icon::Close,
                               eyeMin, eyeMax, listed,
                               listed ? "Shown in ArrayList" : "Hidden from ArrayList"))
            {
                arrayListFlag->value = !arrayListFlag->value;
            }
        }

        if (module->toggleable())
            DrawSwitch(id, swMin, swMax, enabled, overSwitch, false);

        Chevron(dl, chevronCenter, 10.0f, state->open, Mix(c.textMute, c.text, state->hover), 1.9f);

        // Favourite star (top-right). Accent-tinted when pinned.
        if (state->fav > 0.01f || overFav)
        {
            dl->AddCircleFilled(favCenter, 9.0f,
                                Alpha(state->fav > 0.5f ? c.accent : c.widgetHover,
                                      state->fav > 0.5f ? 0.22f + state->fav * 0.78f : 1.0f), 24);
        }
        DrawIcon(dl, Icon::Star, favCenter, 12.0f,
                 state->fav > 0.5f ? c.accent : Mix(c.textMute, c.text, overFav ? 0.7f : 0.0f),
                 1.6f, state->fav > 0.5f);
        if (overFav)
            SetTooltip("%s", favorite ? "Unpin from top" : "Pin to top");

        // Expanded settings.
        if (extra > 10.0f)
        {
            dl->AddLine(ImVec2(cardMin.x + 14.0f, cardMin.y + kCardHeaderHeight),
                        ImVec2(cardMax.x - 14.0f, cardMin.y + kCardHeaderHeight),
                        Alpha(c.line, 0.95f), 1.0f);

            ImGui::SetCursorScreenPos(ImVec2(cardMin.x + 16.0f,
                                             cardMin.y + kCardHeaderHeight + 8.0f));
            ImGui::BeginChild("##settings",
                              ImVec2(width - 32.0f, std::max(1.0f, extra - 8.0f)), 0,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            DrawModuleSettings(module);
            ImGui::EndChild();
        }

        ImGui::SetCursorScreenPos(ImVec2(cardMin.x, cardMax.y));
        ImGui::Dummy(ImVec2(width, kCardSpacing));
        ImGui::PopID();
    }

    void DrawEmptyState(const char* title, const char* message)
    {
        const Palette& c = Colors();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 center(pos.x + avail.x * 0.5f, pos.y + std::min(avail.y * 0.38f, 140.0f));

        DrawIcon(dl, Icon::Search, ImVec2(center.x, center.y - 28.0f), 34.0f,
                 Alpha(c.textMute, 0.75f), 2.0f);

        const ImVec2 titleSize = TextSize(FontRegular(), 14.0f, title);
        Text(dl, FontRegular(), 14.0f, ImVec2(center.x - titleSize.x * 0.5f, center.y + 2.0f),
             c.textDim, title);

        const ImVec2 msgSize = TextSize(FontRegular(), 11.0f, message);
        Text(dl, FontRegular(), 11.0f, ImVec2(center.x - msgSize.x * 0.5f, center.y + 24.0f),
             c.textMute, message);
    }

    void DrawModuleList()
    {
        std::vector<Module*> list;

        if (g.search[0] != '\0')
        {
            for (const auto& owned : ModuleManager::All())
            {
                Module* module = owned.get();
                if (module->showInMenu() && MatchesSearch(module))
                    list.push_back(module);
            }
        }
        else if (g.category >= 0 && g.category < kNavCount)
        {
            list = ModuleManager::ByCategory(kNav[g.category].category);
        }

        // Favourites first, then alphabetical by display name.
        std::sort(list.begin(), list.end(), [](const Module* a, const Module* b)
        {
            const bool fa = IsFavorite(a->name());
            const bool fb = IsFavorite(b->name());
            if (fa != fb)
                return fa;
            return std::strcmp(a->name(), b->name()) < 0;
        });

        if (list.empty())
        {
            DrawEmptyState(g.search[0] ? "No matches" : "Nothing here",
                           g.search[0] ? "Try a different search term"
                                       : "This category has no modules");
            return;
        }

        // Result count while searching.
        if (g.search[0] != '\0')
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            char count[64];
            std::snprintf(count, sizeof(count), "%d result%s",
                          static_cast<int>(list.size()), list.size() == 1 ? "" : "s");
            Text(dl, FontRegular(), 10.0f, pos, Colors().textMute, count);
            ImGui::Dummy(ImVec2(0.0f, 16.0f));
        }

        for (size_t i = 0; i < list.size(); ++i)
            DrawModuleCard(list[i], static_cast<int>(i));
    }

    void DrawActiveModuleGrid(float railWidth)
    {
        std::vector<Module*> enabled;
        for (const auto& owned : ModuleManager::All())
        {
            Module* m = owned.get();
            if (m->showInMenu() && m->toggleable() && m->isEnabled())
                enabled.push_back(m);
        }
        if (enabled.empty())
            return;

        const Palette& c = Colors();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        ImGui::Dummy(ImVec2(0.0f, 12.0f));
        Text(dl, FontRegular(), 10.0f, ImGui::GetCursorScreenPos(), c.textMute, "ACTIVE");
        ImGui::Dummy(ImVec2(0.0f, 16.0f));

        constexpr float kCell = 36.0f;
        constexpr float kGap = 8.0f;
        constexpr int kCols = 3;
        const float gridW = kCols * kCell + (kCols - 1) * kGap;
        const float left = ImGui::GetCursorScreenPos().x;
        const float originX = left + std::max(0.0f, (railWidth - gridW) * 0.5f);
        const float originY = ImGui::GetCursorScreenPos().y;

        for (int i = 0; i < static_cast<int>(enabled.size()); ++i)
        {
            const int col = i % kCols;
            const int row = i / kCols;
            const float x = originX + col * (kCell + kGap);
            const float y = originY + row * (kCell + kGap);

            Module* m = enabled[i];
            ImGui::PushID(m->name());
            ImGui::SetCursorScreenPos(ImVec2(x, y));
            ImGui::InvisibleButton("##active", ImVec2(kCell, kCell));
            const bool hovered = ImGui::IsItemHovered();
            const bool pressed = ImGui::IsItemClicked(ImGuiMouseButton_Left);

            const ImVec2 min(x, y);
            const ImVec2 max(x + kCell, y + kCell);
            dl->AddRectFilled(min, max,
                              hovered ? c.widgetHover : Alpha(c.widget, 0.92f), 10.0f);
            if (hovered)
                dl->AddRect(min, max, Alpha(c.accent, 0.55f), 10.0f, 0, 1.0f);

            DrawIcon(dl, IconForModule(m->name(), static_cast<int>(m->category())),
                     ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f),
                     15.0f, hovered ? c.accent : c.text, 1.7f, true);

            if (hovered)
                SetTooltip("%s", m->name());

            if (pressed)
            {
                g.shell = Shell_Home;
                g.search[0] = '\0';
                for (int n = 0; n < kNavCount; ++n)
                {
                    if (kNav[n].category == m->category())
                    {
                        g.category = n;
                        break;
                    }
                }
            }
            ImGui::PopID();
        }

        const int rows = (static_cast<int>(enabled.size()) + kCols - 1) / kCols;
        ImGui::SetCursorScreenPos(ImVec2(left, originY + rows * (kCell + kGap)));
        ImGui::Dummy(ImVec2(railWidth, 4.0f));
    }

    void DrawAccentPicker()
    {
        const Palette& c = Colors();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        int presetCount = 0;
        const AccentPreset* presets = AccentPresets(presetCount);

        const float diameter = 30.0f;
        const float gap = 12.0f;
        const float stripWidth = static_cast<float>(presetCount) * diameter +
                                 static_cast<float>(presetCount - 1) * gap;

        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const float available = ImGui::GetContentRegionAvail().x;
        // Centre the swatch strip instead of hugging the left edge.
        const ImVec2 origin(cursor.x + std::max(0.0f, (available - stripWidth) * 0.5f), cursor.y);

        for (int i = 0; i < presetCount; ++i)
        {
            const ImVec2 center(origin.x + diameter * 0.5f + static_cast<float>(i) * (diameter + gap),
                                origin.y + diameter * 0.5f);

            ImGui::PushID(i);
            ImGui::SetCursorScreenPos(ImVec2(center.x - diameter * 0.5f, center.y - diameter * 0.5f));
            const ImGuiID id = ImGui::GetID("##swatch");
            ImGui::InvisibleButton("##swatch", ImVec2(diameter, diameter));
            const bool hovered = ImGui::IsItemHovered();
            const bool pressed = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            ImGui::PopID();

            const ImVec4& accent = Accent();
            const bool selected = std::fabs(accent.x - presets[i].r) < 0.01f &&
                                  std::fabs(accent.y - presets[i].g) < 0.01f &&
                                  std::fabs(accent.z - presets[i].b) < 0.01f;

            struct SwatchState { float hover = 0.0f; float sel = 0.0f; };
            SwatchState* s = State<SwatchState>(id);
            Ease(s->hover, hovered ? 1.0f : 0.0f, 14.0f);
            Ease(s->sel, selected ? 1.0f : 0.0f, 14.0f);

            if (pressed)
            {
                Prefs().accent[0] = presets[i].r;
                Prefs().accent[1] = presets[i].g;
                Prefs().accent[2] = presets[i].b;
                ApplyPreferences();
                Notify("Accent updated", presets[i].name, Icon::Palette, Colors().accent);
            }

            const ImU32 swatch = Fade(ImGui::ColorConvertFloat4ToU32(
                ImVec4(presets[i].r, presets[i].g, presets[i].b, 1.0f)));

            const float radius = diameter * 0.5f * (1.0f + s->hover * 0.07f) - 2.0f;
            dl->AddCircleFilled(center, radius, swatch, 28);
            if (s->sel > 0.01f)
            {
                dl->AddCircle(center, radius + 3.5f, Alpha(c.text, s->sel), 28, 1.8f);
                DrawIcon(dl, Icon::Check, center, 13.0f,
                         Fade(Alpha(IM_COL32(255, 255, 255, 255), s->sel)), 2.2f);
            }
        }

        ImGui::SetCursorScreenPos(ImVec2(cursor.x, origin.y + diameter));
        ImGui::Dummy(ImVec2(0.0f, 12.0f));
    }

    void DrawSettingsPage()
    {
        const Palette& c = Colors();
        const bool desc = ShowDescriptions();

        SectionLabel("ACCENT", 2.0f);
        DrawAccentPicker();

        float accent[4] = { Accent().x, Accent().y, Accent().z, 1.0f };
        if (RowColor("Custom accent", desc ? "Pick any colour for the interface" : "", accent))
        {
            Prefs().accent[0] = accent[0];
            Prefs().accent[1] = accent[1];
            Prefs().accent[2] = accent[2];
            ApplyPreferences();
        }

        SectionLabel("INTERFACE");

        {
            static const char* kLayouts[] = { "Classic", "Dropdown" };
            if (Prefs().menuLayout < 0 || Prefs().menuLayout > 1)
                Prefs().menuLayout = 0;
            RowDropdown("Menu layout",
                        desc ? "Classic window, or category columns you can drag apart" : "",
                        &Prefs().menuLayout, kLayouts, 2);
        }

        RowToggle("Reduce motion", desc ? "Snap animations instead of easing them" : "",
                  &ReduceMotion());
        RowToggle("Show descriptions", desc ? "Display helper text under every option" : "",
                  &ShowDescriptions());
        RowToggle("Toggle notifications", desc ? "Pop a toast when a module changes state" : "",
                  &NotifyOnToggle());

        // Notification toasts live here (no module card).
        {
            SectionLabel("NOTIFICATIONS");

            RowToggle("In-game notifications",
                      desc ? "Show client notifications while playing" : "",
                      &Prefs().notificationsEnabled);
            RowToggle("Module Toggles",
                      desc ? "Notify when a cheat module is turned on or off." : "",
                      &Prefs().notifyModuleToggles);
            RowToggle("Profile Configs",
                      desc ? "Notify when configuration profiles are saved, loaded, or deleted." : "",
                      &Prefs().notifyProfileConfigs);
            RowToggle("Friend Toggles",
                      desc ? "Notify when players are added to or removed from the friends list." : "",
                      &Prefs().notifyFriendToggles);
            RowToggle("Play Sounds",
                      desc ? "Play feedback audio alongside notification popups." : "",
                      &Prefs().notifyPlaySounds);
        }

        SectionLabel("MENU");

        {
            static bool listening = false;
            if (RowKeybind("Open menu", desc ? "Key that shows and hides this window" : "",
                           &Menu::Keybind, &listening))
            {
                if (Menu::Keybind == 0) Menu::Keybind = VK_INSERT;
            }
        }

        if (RowAction("Detach",
                      desc ? "Detach the client from the game process" : "",
                      "Detach"))
        {
            s_requestDetach = true;
        }

        int enabledCount = 0;
        int totalCount = 0;
        for (const auto& owned : ModuleManager::All())
        {
            if (!owned->showInMenu())
                continue;
            ++totalCount;
            if (owned->isEnabled())
                ++enabledCount;
        }

        ImGui::Dummy(ImVec2(0.0f, 16.0f));

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const ImVec2 max(min.x + width, min.y + 56.0f);
        dl->AddRectFilled(min, max, c.card, 11.0f);

        char summary[128];
        std::snprintf(summary, sizeof(summary), "%d of %d modules enabled",
                      enabledCount, totalCount);
        DrawIcon(dl, Icon::Bolt, ImVec2(min.x + 30.0f, (min.y + max.y) * 0.5f), 18.0f, c.accent, 2.0f);
        Text(dl, FontRegular(), 13.0f, ImVec2(min.x + 50.0f, min.y + 13.0f), c.text, summary);
        Text(dl, FontRegular(), 11.0f, ImVec2(min.x + 50.0f, min.y + 32.0f), c.textMute,
             "DemokratikKongo - Minecraft 1.8.9");

        ImGui::Dummy(ImVec2(width, 68.0f));
    }

    std::string SanitizeProfileName(const std::string& in)
    {
        std::string out;
        out.reserve(in.size());
        for (char c : in)
        {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == ' ')
                out.push_back(c);
        }
        while (!out.empty() && out.front() == ' ') out.erase(out.begin());
        while (!out.empty() && out.back() == ' ') out.pop_back();
        if (out.size() > 32)
            out.resize(32);
        return out;
    }

    // listProfiles() runs SHGetKnownFolderPath + create_directories + a
    // directory scan. The Config page called it once per frame; cache the result
    // and refresh only after a save / delete actually changes the folder.
    std::vector<std::string> g_profileCache;
    bool g_profileCacheValid = false;

    const std::vector<std::string>& CachedProfiles()
    {
        if (!g_profileCacheValid)
        {
            g_profileCache = ProfileManager::listProfiles();
            g_profileCacheValid = true;
        }
        return g_profileCache;
    }

    void InvalidateProfileCache() { g_profileCacheValid = false; }

    // Compact list row: label on the left, one or more pill actions on the right.
    // Returns 1 = primary, 2 = secondary, 3 = danger, 0 = none.
    int ManagerRow(const char* id, const char* label, const char* meta,
                   const char* primary, const char* secondary = nullptr,
                   const char* danger = nullptr)
    {
        ImGui::PushID(id);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const Palette& c = Colors();

        const float width = ImGui::GetContentRegionAvail().x;
        const float height = meta && *meta ? 52.0f : 42.0f;
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max(min.x + width, min.y + height);

        dl->AddRectFilled(min, max, c.card, 10.0f);

        Text(dl, FontRegular(), 13.0f, ImVec2(min.x + 14.0f, meta && *meta ? min.y + 10.0f : min.y + 13.0f),
             c.text, label);
        if (meta && *meta)
            Text(dl, FontRegular(), 11.0f, ImVec2(min.x + 14.0f, min.y + 29.0f), c.textMute, meta);

        int hit = 0;
        float x = max.x - 12.0f;
        const float frameH = ImGui::GetFrameHeight();
        const float btnY = min.y + (height - frameH) * 0.5f;

        auto place = [&](const char* btnId, const char* text, bool filled, int code)
        {
            const float btnW = std::max(56.0f, TextSize(FontRegular(), 12.0f, text).x + 24.0f);
            x -= btnW;
            ImGui::SetCursorScreenPos(ImVec2(x, btnY));
            if (PillButton(btnId, text, ImVec2(btnW, frameH), filled))
                hit = code;
            x -= 6.0f;
        };

        if (danger && *danger)
            place("##danger", danger, false, 3);
        if (secondary && *secondary)
            place("##secondary", secondary, false, 2);
        if (primary && *primary)
            place("##primary", primary, true, 1);

        ImGui::SetCursorScreenPos(ImVec2(min.x, max.y + 8.0f));
        ImGui::Dummy(ImVec2(width, 0.0f));
        ImGui::PopID();
        return hit;
    }

    void DrawFriendsPage()
    {
        const bool desc = ShowDescriptions();

        SectionLabel("OPTIONS", 2.0f);
        RowToggle("Middle click",
                  desc ? "Add or remove a player by middle-clicking them in-game." : "",
                  &Friends::MiddleClick);

        SectionLabel("FRIEND LIST");

        static char nameBuf[32] = {};
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 72.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg,
                              ImGui::ColorConvertU32ToFloat4(Colors().widget));
        const bool submitted = ImGui::InputTextWithHint(
            "##friendname", "player name", nameBuf, sizeof(nameBuf),
            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 8.0f);
        const bool add = PillButton("##addfriend", "Add",
                                    ImVec2(64.0f, ImGui::GetFrameHeight()), true);

        if ((submitted || add) && nameBuf[0] != '\0')
        {
            const std::string name = Trimmer::trim(nameBuf);
            if (!name.empty() && !Friends::IsFriend(name))
            {
                Friends::ToggleFriend(name);
                if (Prefs().notifyFriendToggles)
                    Notify("Friends", "Added to friend list", Icon::Check, Colors().good);
            }
            nameBuf[0] = '\0';
        }

        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        std::vector<std::string> names(Friends::List.begin(), Friends::List.end());
        std::sort(names.begin(), names.end(),
                  [](const std::string& a, const std::string& b)
                  {
                      return _stricmp(a.c_str(), b.c_str()) < 0;
                  });

        if (names.empty())
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            Text(dl, FontRegular(), 12.0f, pos, Colors().textMute,
                 desc ? "No friends yet - add a name above or middle-click a player."
                      : "No friends yet.");
            ImGui::Dummy(ImVec2(0.0f, 28.0f));
        }
        else
        {
            for (const std::string& name : names)
            {
                if (ManagerRow(name.c_str(), name.c_str(), nullptr, "Remove") == 1)
                {
                    Friends::ToggleFriend(name);
                    if (Prefs().notifyFriendToggles)
                        Notify("Friends", "Removed from friend list", Icon::Close, Colors().textMute);
                }
            }
        }
    }

    void DrawConfigPage()
    {
        const bool desc = ShowDescriptions();
        const Palette& c = Colors();

        SectionLabel("ACTIVE", 2.0f);

        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            const ImVec2 max(min.x + width, min.y + 56.0f);
            dl->AddRectFilled(min, max, c.card, 11.0f);
            DrawIcon(dl, Icon::Folder, ImVec2(min.x + 28.0f, (min.y + max.y) * 0.5f),
                     16.0f, c.accent, 1.9f);
            Text(dl, FontRegular(), 13.0f, ImVec2(min.x + 48.0f, min.y + 12.0f),
                 c.text, ProfileManager::ActiveProfile.c_str());
            Text(dl, FontRegular(), 11.0f, ImVec2(min.x + 48.0f, min.y + 31.0f),
                 c.textMute, "Currently loaded profile");
            ImGui::Dummy(ImVec2(width, 64.0f));
        }

        SectionLabel("CREATE / SAVE AS");

        static char nameBuf[48] = {};
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 72.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(c.widget));
        const bool submitted = ImGui::InputTextWithHint(
            "##profilename", "profile name", nameBuf, sizeof(nameBuf),
            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 8.0f);
        const bool saveAs = PillButton("##saveas", "Save",
                                       ImVec2(64.0f, ImGui::GetFrameHeight()), true);

        if ((submitted || saveAs) && nameBuf[0] != '\0')
        {
            const std::string name = SanitizeProfileName(nameBuf);
            if (!name.empty())
            {
                if (ProfileManager::save(name))
                {
                    if (Prefs().notifyProfileConfigs)
                        Notify("Configs", "Profile saved", Icon::Check, c.good);
                    InvalidateProfileCache();
                    nameBuf[0] = '\0';
                }
                else
                {
                    Notify("Configs", "Failed to save profile", Icon::Close, c.danger);
                }
            }
        }

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        if (RowAction("Save active",
                      desc ? "Overwrite the currently loaded profile" : "",
                      "Save"))
        {
            if (ProfileManager::save(ProfileManager::ActiveProfile))
            {
                if (Prefs().notifyProfileConfigs)
                    Notify("Configs", "Active profile saved", Icon::Check, c.good);
            }
            else
                Notify("Configs", "Failed to save profile", Icon::Close, c.danger);
        }

        SectionLabel("PROFILES");

        const std::vector<std::string> profiles = CachedProfiles();
        if (profiles.empty())
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            Text(dl, FontRegular(), 12.0f, ImGui::GetCursorScreenPos(), c.textMute,
                 "No profiles on disk yet.");
            ImGui::Dummy(ImVec2(0.0f, 28.0f));
        }
        else
        {
            for (const std::string& name : profiles)
            {
                const bool active = name == ProfileManager::ActiveProfile;
                const int hit = ManagerRow(
                    name.c_str(),
                    name.c_str(),
                    active ? "Active" : nullptr,
                    "Load",
                    "Save",
                    name == "default" ? nullptr : "Delete");

                if (hit == 1)
                {
                    if (ProfileManager::load(name))
                    {
                        ApplyPreferences();
                        if (Prefs().notifyProfileConfigs)
                            Notify("Configs", "Profile loaded", Icon::Check, c.good);
                    }
                    else
                    {
                        Notify("Configs", "Failed to load profile", Icon::Close, c.danger);
                    }
                }
                else if (hit == 2)
                {
                    if (ProfileManager::save(name))
                    {
                        if (Prefs().notifyProfileConfigs)
                            Notify("Configs", "Profile saved", Icon::Check, c.good);
                    }
                    else
                        Notify("Configs", "Failed to save profile", Icon::Close, c.danger);
                }
                else if (hit == 3)
                {
                    if (ProfileManager::remove(name))
                    {
                        if (Prefs().notifyProfileConfigs)
                            Notify("Configs", "Profile deleted", Icon::Close, c.textMute);
                        InvalidateProfileCache();
                    }
                    else
                    {
                        Notify("Configs", "Failed to delete profile", Icon::Close, c.danger);
                    }
                }
            }
        }
    }

    // Full screen backdrop drawn behind the window: a dim wash plus slow
    // drifting orbs so the frozen game scene does not look static.
    void DrawBackdrop(float opacity)
    {
        if (opacity <= 0.004f)
            return;

        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        const ImVec2 screen = ImGui::GetIO().DisplaySize;
        const Palette& c = Colors();
        const float t = static_cast<float>(ImGui::GetTime());

        // Dim wash.
        dl->AddRectFilled(ImVec2(0.0f, 0.0f), screen,
                          Alpha(IM_COL32(6, 6, 10, 214), opacity));

        // Drifting accent orbs. Concentric rings fake a soft radial falloff
        // without needing a shader.
        struct Orb { float x, y, radius, speedX, speedY, phase, tint; };
        static const Orb kOrbs[] = {
            { 0.18f, 0.26f, 300.0f, 0.021f, 0.014f, 0.0f,  1.0f },
            { 0.82f, 0.20f, 250.0f, -0.017f, 0.019f, 1.7f, 0.7f },
            { 0.72f, 0.82f, 330.0f, 0.013f, -0.011f, 3.1f, 1.0f },
            { 0.24f, 0.78f, 220.0f, -0.023f, -0.016f, 4.6f, 0.6f },
        };

        for (const Orb& orb : kOrbs)
        {
            const float cx = screen.x * (orb.x + 0.05f * std::sin(t * orb.speedX * 6.0f + orb.phase));
            const float cy = screen.y * (orb.y + 0.05f * std::cos(t * orb.speedY * 6.0f + orb.phase));
            const float pulse = 0.88f + 0.12f * std::sin(t * 0.6f + orb.phase);
            const float radius = orb.radius * pulse;

            const ImU32 tint = orb.tint > 0.85f ? c.accent : IM_COL32(120, 150, 255, 255);

            // 8 rings x 24 segments instead of 16 x 40: the falloff still reads
            // as a soft gradient, but the per-frame vertex count for the four
            // orbs drops roughly 4x. Ring alpha is scaled up to compensate for
            // the halved layer count.
            const int rings = 8;
            for (int i = rings; i > 0; --i)
            {
                const float f = static_cast<float>(i) / static_cast<float>(rings);
                const float alpha = (1.0f - f) * (1.0f - f) * 0.10f * opacity * orb.tint;
                dl->AddCircleFilled(ImVec2(cx, cy), radius * f, Alpha(tint, alpha), 24);
            }
        }

        // Vignette so the window keeps focus.
        const int bands = 5;
        for (int i = 0; i < bands; ++i)
        {
            const float f = static_cast<float>(i) / static_cast<float>(bands);
            const float inset = f * 120.0f;
            const float alpha = (1.0f - f) * 0.10f * opacity;
            dl->AddRect(ImVec2(inset, inset), ImVec2(screen.x - inset, screen.y - inset),
                        Alpha(IM_COL32(0, 0, 0, 255), alpha), 0.0f, 0, 26.0f);
        }
    }

    // ------------------------------------------------------------------
    // Fake Login — a small centred card shown on the multiplayer server
    // list (Menu::FakeLoginOpen). Typing a name here swaps the session so
    // cracked / offline servers accept the connection under that name.
    // ------------------------------------------------------------------
    void DrawFakeLogin(float opacity)
    {
        const Palette& c = Colors();
        ImGuiIO& io = ImGui::GetIO();

        DrawBackdrop(opacity);

        const ImVec2 size(440.0f, 250.0f);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - size.x) * 0.5f,
                                       (io.DisplaySize.y - size.y) * 0.5f),
                                ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, opacity);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoSavedSettings;

        if (ImGui::Begin("##FakeLogin", &Menu::FakeLoginOpen, flags))
        {
            const ImVec2 wp = ImGui::GetWindowPos();
            const ImVec2 ws = ImGui::GetWindowSize();
            const float rise = (1.0f - opacity) * 14.0f;
            const ImVec2 winMin(wp.x, wp.y + rise);
            const ImVec2 winMax(wp.x + ws.x, wp.y + ws.y + rise);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImGui::GetBackgroundDrawList()->AddShadowRect(
                winMin, winMax, Alpha(IM_COL32(0, 0, 0, 190), opacity), 40.0f,
                ImVec2(0.0f, 10.0f), ImDrawFlags_ShadowCutOutShapeBackground, 15.0f);

            dl->AddRectFilled(winMin, winMax, c.background, 15.0f);
            dl->AddRect(winMin, winMax, Alpha(c.border, 0.9f), 15.0f, 0, 1.0f);

            const float pad = 24.0f;

            const ImVec2 tileMin(winMin.x + pad, winMin.y + 22.0f);
            const ImVec2 tileMax(tileMin.x + 40.0f, tileMin.y + 40.0f);
            dl->AddRectFilled(tileMin, tileMax, Alpha(c.accent, 0.16f), 11.0f);
            DrawIcon(dl, Icon::UserCheck,
                     ImVec2((tileMin.x + tileMax.x) * 0.5f, (tileMin.y + tileMax.y) * 0.5f),
                     18.0f, c.accent, 2.0f);

            Text(dl, FontRegular(), 15.0f, ImVec2(tileMax.x + 14.0f, tileMin.y + 2.0f),
                 c.text, "Fake Login");
            Text(dl, FontRegular(), 11.0f, ImVec2(tileMax.x + 14.0f, tileMin.y + 22.0f),
                 c.textMute, "Cracked / offline server name");

            ImGui::SetCursorScreenPos(ImVec2(winMax.x - 40.0f, winMin.y + 16.0f));
            if (IconButton("##flclose", Icon::Close, 28.0f, false))
                Menu::FakeLoginOpen = false;

            dl->AddLine(ImVec2(winMin.x + pad, winMin.y + 74.0f),
                        ImVec2(winMax.x - pad, winMin.y + 74.0f), Alpha(c.line, 0.95f), 1.0f);

            Text(dl, FontRegular(), 10.0f, ImVec2(winMin.x + pad, winMin.y + 90.0f),
                 c.textMute, "CURRENT USERNAME");
            const std::string current = FakeLoginCurrentUsername();
            Text(dl, FontRegular(), 14.0f, ImVec2(winMin.x + pad, winMin.y + 110.0f),
                 c.text, current.c_str());

            static char nameBuf[64] = {};
            ImGui::SetCursorScreenPos(ImVec2(winMin.x + pad, winMin.y + 140.0f));
            ImGui::SetNextItemWidth(ws.x - pad * 2.0f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(c.widget));
            const bool submitted = ImGui::InputTextWithHint(
                "##flname", "new username", nameBuf, sizeof(nameBuf),
                ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::PopStyleColor();

            ImGui::SetCursorScreenPos(ImVec2(winMin.x + pad, winMin.y + 178.0f));
            const bool login = PillButton("##flset", "Set Name",
                                          ImVec2(ws.x - pad * 2.0f, 36.0f), true);

            if ((submitted || login) && nameBuf[0] != '\0')
            {
                const std::string name = Trimmer::trim(nameBuf);
                if (!name.empty())
                {
                    if (FakeLoginSetUsername(name))
                        Notify("Fake Login", ("Username set to " + name).c_str(),
                               Icon::Check, c.good);
                    else
                        Notify("Fake Login", "Failed to set username",
                               Icon::Close, c.danger);
                }
                nameBuf[0] = '\0';
            }
        }
        ImGui::End();

        ImGui::PopStyleVar(3);
    }

    // ------------------------------------------------------------------
    // Item Log Manager — modal panel listing the recent Item Logger
    // entries. Opened from the Item Logger module's "Open Log Manager"
    // action, which sets Menu::ItemLogOpen.
    // ------------------------------------------------------------------
    void WrapLogLines(const char* text, float maxWidth, std::vector<std::string>& lines)
    {
        lines.clear();
        if (!text || !*text)
            return;

        ImFont* font = FontRegular();
        const float size = 12.0f;

        std::string remaining(text);
        while (!remaining.empty())
        {
            if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, remaining.c_str()).x <= maxWidth)
            {
                lines.push_back(remaining);
                return;
            }

            size_t cut = 0;
            size_t lastSpace = std::string::npos;
            while (cut < remaining.size() &&
                   font->CalcTextSizeA(size, FLT_MAX, 0.0f,
                                       remaining.substr(0, cut + 1).c_str()).x <= maxWidth)
            {
                if (remaining[cut] == ' ')
                    lastSpace = cut;
                ++cut;
            }

            if (cut == 0)
                cut = 1;

            const size_t lineEnd = (lastSpace != std::string::npos && lastSpace > 0) ? lastSpace : cut;
            std::string line = remaining.substr(0, lineEnd);
            while (!line.empty() && line.back() == ' ')
                line.pop_back();
            if (line.empty())
                line = remaining.substr(0, cut);
            lines.push_back(line);

            remaining.erase(0, lineEnd);
            while (!remaining.empty() && remaining.front() == ' ')
                remaining.erase(remaining.begin());
        }
    }

    void DrawItemLogger(float opacity)
    {
        const Palette& c = Colors();
        ImGuiIO& io = ImGui::GetIO();

        DrawBackdrop(opacity);

        const ImVec2 size(560.0f, 440.0f);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - size.x) * 0.5f,
                                       (io.DisplaySize.y - size.y) * 0.5f),
                                ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, opacity);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoSavedSettings;

        if (ImGui::Begin("##ItemLogger", &Menu::ItemLogOpen, flags))
        {
            const ImVec2 wp = ImGui::GetWindowPos();
            const ImVec2 ws = ImGui::GetWindowSize();
            const float rise = (1.0f - opacity) * 14.0f;
            const ImVec2 winMin(wp.x, wp.y + rise);
            const ImVec2 winMax(wp.x + ws.x, wp.y + ws.y + rise);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImGui::GetBackgroundDrawList()->AddShadowRect(
                winMin, winMax, Alpha(IM_COL32(0, 0, 0, 190), opacity), 40.0f,
                ImVec2(0.0f, 10.0f), ImDrawFlags_ShadowCutOutShapeBackground, 15.0f);

            dl->AddRectFilled(winMin, winMax, c.background, 15.0f);
            dl->AddRect(winMin, winMax, Alpha(c.border, 0.9f), 15.0f, 0, 1.0f);

            const float pad = 24.0f;

            // Header.
            const ImVec2 tileMin(winMin.x + pad, winMin.y + 22.0f);
            const ImVec2 tileMax(tileMin.x + 40.0f, tileMin.y + 40.0f);
            dl->AddRectFilled(tileMin, tileMax, Alpha(c.accent, 0.16f), 11.0f);
            DrawIcon(dl, Icon::Note,
                     ImVec2((tileMin.x + tileMax.x) * 0.5f, (tileMin.y + tileMax.y) * 0.5f),
                     18.0f, c.accent, 2.0f);

            Text(dl, FontRegular(), 15.0f, ImVec2(tileMax.x + 14.0f, tileMin.y + 2.0f),
                 c.text, "Log Manager");
            Text(dl, FontRegular(), 11.0f, ImVec2(tileMax.x + 14.0f, tileMin.y + 22.0f),
                 c.textMute, "Item Logger - recent drops & pickups");

            ImGui::SetCursorScreenPos(ImVec2(winMax.x - 40.0f, winMin.y + 16.0f));
            if (IconButton("##ilogclose", Icon::Close, 28.0f, false))
                Menu::ItemLogOpen = false;

            dl->AddLine(ImVec2(winMin.x + pad, winMin.y + 74.0f),
                        ImVec2(winMax.x - pad, winMin.y + 74.0f), Alpha(c.line, 0.95f), 1.0f);

            // Filter + clear.
            static char filter[64] = {};
            const float frameH = ImGui::GetFrameHeight();
            ImGui::SetCursorScreenPos(ImVec2(winMin.x + pad, winMin.y + 88.0f));
            ImGui::SetNextItemWidth(ws.x - pad * 2.0f - 78.0f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(c.widget));
            ImGui::InputTextWithHint("##ilogfilter", "filter logs", filter, sizeof(filter));
            ImGui::PopStyleColor();
            ImGui::SameLine(0.0f, 8.0f);
            if (PillButton("##ilogclear", "Clear", ImVec2(70.0f, frameH), false))
                ItemLogger::ClearLogs();

            // Snapshot + filter (newest first).
            const std::vector<std::string> logs = ItemLogger::SnapshotLogs();
            std::vector<std::string> filtered;
            for (auto it = logs.rbegin(); it != logs.rend(); ++it)
            {
                if (filter[0] != '\0' && !ContainsFold(it->c_str(), filter))
                    continue;
                filtered.push_back(*it);
            }

            const float listTop = winMin.y + 128.0f;
            const float footerH = 46.0f;
            const float listW = ws.x - pad * 2.0f;
            const float listH = std::max(40.0f, winMax.y - listTop - footerH);

            ImGui::SetCursorScreenPos(ImVec2(winMin.x + pad, listTop));
            ImGui::BeginChild("##iloglist", ImVec2(listW, listH), false,
                              ImGuiWindowFlags_NoBackground);
            ImDrawList* ldl = ImGui::GetWindowDrawList();

            if (filtered.empty())
            {
                const float cx = winMin.x + pad + listW * 0.5f;
                const float cy = listTop + listH * 0.5f - 12.0f;
                DrawIcon(ldl, Icon::Note, ImVec2(cx, cy - 26.0f), 30.0f,
                         Alpha(c.textMute, 0.8f), 2.0f);

                const char* msg = filter[0] ? "No logs match the filter" : "No item logs yet";
                const ImVec2 ts = TextSize(FontRegular(), 13.0f, msg);
                Text(ldl, FontRegular(), 13.0f, ImVec2(cx - ts.x * 0.5f, cy), c.textDim, msg);

                const char* sub = filter[0] ? "Adjust your filter" :
                                              "Valuable drops and pickups will appear here";
                const ImVec2 ss = TextSize(FontRegular(), 11.0f, sub);
                Text(ldl, FontRegular(), 11.0f, ImVec2(cx - ss.x * 0.5f, cy + 22.0f), c.textMute, sub);

                ImGui::Dummy(ImVec2(listW, listH));
            }
            else
            {
                for (size_t i = 0; i < filtered.size(); ++i)
                {
                    const ImVec2 rowMin = ImGui::GetCursorScreenPos();

                    std::vector<std::string> lines;
                    WrapLogLines(filtered[i].c_str(), listW - 34.0f, lines);

                    const float rowH = static_cast<float>(lines.size()) * 17.0f + 14.0f;
                    const ImVec2 rowMax(rowMin.x + listW, rowMin.y + rowH);

                    ldl->AddCircleFilled(ImVec2(rowMin.x + 7.0f, rowMin.y + 15.0f),
                                         3.0f, c.accent, 12);

                    float ty = rowMin.y + 8.0f;
                    for (const std::string& line : lines)
                    {
                        Text(ldl, FontRegular(), 12.0f, ImVec2(rowMin.x + 18.0f, ty),
                             c.text, line.c_str());
                        ty += 17.0f;
                    }

                    if (i + 1 < filtered.size())
                        ldl->AddLine(ImVec2(rowMin.x, rowMax.y - 3.0f),
                                     ImVec2(rowMax.x, rowMax.y - 3.0f),
                                     Alpha(c.line, 0.55f), 1.0f);

                    ImGui::Dummy(ImVec2(listW, rowH));
                }
            }

            ImGui::EndChild();

            // Footer.
            const float footerY = winMax.y - footerH;
            dl->AddLine(ImVec2(winMin.x + pad, footerY),
                        ImVec2(winMax.x - pad, footerY), Alpha(c.line, 0.95f), 1.0f);

            char count[64];
            std::snprintf(count, sizeof(count), "Showing %d of %d entries",
                          static_cast<int>(filtered.size()), static_cast<int>(logs.size()));
            Text(dl, FontRegular(), 11.0f, ImVec2(winMin.x + pad, footerY + 18.0f),
                 c.textMute, count);
        }
        ImGui::End();

        ImGui::PopStyleVar(3);
    }
}

void Gui::InitializeStyle()
{
    ApplyImGuiStyle();
    g.styled = true;
}

void Gui::Shutdown()
{
    ClearNotifications();
    ReleaseAnimationState();
    ResetDropdown();
    g = GuiState{};
}

void Gui::Render()
{
    if (!Menu::Initialized)
        return;

    if (!g.styled)
        InitializeStyle();

    MotionMultiplier() = ReduceMotion() ? 60.0f : 1.0f;

    // Keep animating while the menu closes so it can fade out.
    Ease(g.openAnim, Menu::Open ? 1.0f : 0.0f, 17.0f);

    // Toasts keep running even when the menu is hidden.
    const ImVec2 screenAnchor(ImGui::GetIO().DisplaySize.x - 22.0f,
                              ImGui::GetIO().DisplaySize.y - 22.0f);

    if (!Menu::Open && g.openAnim < 0.004f)
    {
        SetGlobalOpacity(0.0f);
        DrawNotifications(screenAnchor);
        return;
    }

    const float opacity = SmoothStep(g.openAnim);
    SetGlobalOpacity(opacity);

    if (Prefs().menuLayout < 0 || Prefs().menuLayout > 1)
        Prefs().menuLayout = 0;

    if (Prefs().menuLayout == 1)
    {
        RenderDropdown(opacity);
        DrawNotifications(screenAnchor);
        DrawTooltip();
        return;
    }

    DrawBackdrop(opacity);

    const Palette& c = Colors();
    ImGuiIO& io = ImGui::GetIO();

    const ImVec2 size(986.0f, 618.0f);
    ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - size.x) * 0.5f,
                                   (io.DisplaySize.y - size.y) * 0.5f),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(880.0f, 540.0f), ImVec2(1700.0f, 1100.0f));
    ImGui::SetNextWindowBgAlpha(0.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, opacity);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    // Rows are laid out flush and spaced explicitly, so ImGui must not add
    // any of its own vertical spacing between items.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("##DemokratikKongoGui", &Menu::Open, flags))
    {
        const ImVec2 wp = ImGui::GetWindowPos();
        const ImVec2 ws = ImGui::GetWindowSize();
        const float rise = (1.0f - opacity) * 16.0f;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 winMin(wp.x, wp.y + rise);
        const ImVec2 winMax(wp.x + ws.x, wp.y + ws.y + rise);

        ImGui::GetBackgroundDrawList()->AddShadowRect(
            winMin, winMax, Alpha(IM_COL32(0, 0, 0, 190), opacity), 56.0f, ImVec2(0.0f, 14.0f),
            ImDrawFlags_ShadowCutOutShapeBackground, 15.0f);

        const bool showSidebar = (g.shell == Shell_Home) || (g.search[0] != '\0');
        const float sidebarW = showSidebar ? kSidebarWidth : 0.0f;

        // Full window + continuous top bar share the same fill so Home's left/right
        // top chrome never shows the darker sidebar plate.
        dl->AddRectFilled(winMin, winMax, c.background, 15.0f);
        if (showSidebar)
        {
            dl->AddRectFilled(ImVec2(winMin.x, winMin.y + kTopBarHeight),
                              ImVec2(winMin.x + sidebarW, winMax.y), c.sidebar, 15.0f,
                              ImDrawFlags_RoundCornersBottomLeft);
            dl->AddLine(ImVec2(winMin.x + sidebarW, winMin.y + kTopBarHeight),
                        ImVec2(winMin.x + sidebarW, winMax.y - 1.0f), Alpha(c.line, 0.95f), 1.0f);
        }
        dl->AddLine(ImVec2(winMin.x, winMin.y + kTopBarHeight),
                    ImVec2(winMax.x, winMin.y + kTopBarHeight), Alpha(c.line, 0.95f), 1.0f);
        dl->AddRect(winMin, winMax, Alpha(c.border, 0.9f), 15.0f, 0, 1.0f);

        // ---------------------------------------------------- top shell tabs
        ImGui::SetCursorScreenPos(ImVec2(winMin.x + 16.0f, winMin.y + 14.0f));
        ImGui::BeginGroup();
        if (ExpandingTab("##home", "Home", Icon::Home, g.shell == Shell_Home, 36.0f))
        {
            g.shell = Shell_Home;
            g.search[0] = '\0';
        }
        ImGui::SameLine(0.0f, 8.0f);
        if (ExpandingTab("##friends", "Friends", Icon::Users, g.shell == Shell_Friends, 36.0f))
        {
            g.shell = Shell_Friends;
            g.search[0] = '\0';
        }
        ImGui::SameLine(0.0f, 8.0f);
        if (ExpandingTab("##configs", "Configs", Icon::Folder, g.shell == Shell_Config, 36.0f))
        {
            g.shell = Shell_Config;
            g.search[0] = '\0';
        }
        ImGui::SameLine(0.0f, 8.0f);
        if (ExpandingTab("##settings", "Settings", Icon::Gear, g.shell == Shell_Settings, 36.0f))
        {
            g.shell = Shell_Settings;
            g.search[0] = '\0';
        }
        ImGui::EndGroup();

        ImGui::SetCursorScreenPos(ImVec2(winMax.x - 56.0f, winMin.y + 16.0f));
        if (IconButton("##close", Icon::Close, 32.0f, false))
            Menu::Open = false;

        const float contentX = winMin.x + sidebarW;
        const float searchMaxW = std::max(140.0f, std::min(280.0f, ws.x - sidebarW - 460.0f));
        ImGui::SetCursorScreenPos(ImVec2(winMax.x - 112.0f - searchMaxW, winMin.y + 16.0f));
        SearchField("##search", g.search, sizeof(g.search), searchMaxW, 32.0f);

        ImGui::SetCursorScreenPos(ImVec2(winMax.x - 96.0f, winMin.y + 16.0f));
        if (IconButton("##dropdownLayout", Icon::List, 32.0f, false))
            Prefs().menuLayout = 1;
        if (ImGui::IsItemHovered())
            SetTooltip("Dropdown columns");

        // ------------------------------------------- left category rail (Home)
        if (showSidebar)
        {
            ImGui::SetCursorScreenPos(ImVec2(winMin.x + 10.0f, winMin.y + kTopBarHeight + 14.0f));
            ImGui::BeginGroup();
            for (int i = 0; i < kNavCount; ++i)
            {
                if (CategoryRailItem(kNav[i].label, kNav[i].icon,
                                     g.shell == Shell_Home && g.search[0] == '\0' && g.category == i,
                                     kSidebarWidth - 20.0f, 44.0f))
                {
                    g.shell = Shell_Home;
                    g.category = i;
                    g.search[0] = '\0';
                }
                ImGui::Dummy(ImVec2(0.0f, 2.0f));
            }

            DrawActiveModuleGrid(kSidebarWidth - 20.0f);
            ImGui::EndGroup();
        }

        // --------------------------------------------------------- content
        const int viewKey = (g.search[0] ? 1000 : 0) + g.shell * 10 + g.category;
        if (g.lastView != viewKey)
        {
            g.lastView = viewKey;
            g.pageFade = 0.0f;
        }
        Ease(g.pageFade, 1.0f, 11.0f);
        const float fade = SmoothStep(g.pageFade);

        const char* title = "Home";
        if (g.search[0])
            title = "Search results";
        else if (g.shell == Shell_Friends)
            title = "Friends";
        else if (g.shell == Shell_Config)
            title = "Configs";
        else if (g.shell == Shell_Settings)
            title = "Settings";
        else if (g.category >= 0 && g.category < kNavCount)
            title = kNav[g.category].label;

        Text(dl, FontRegular(), 15.0f,
             ImVec2(contentX + kContentPad, winMin.y + kTopBarHeight + 16.0f + (1.0f - fade) * 6.0f),
             Alpha(c.text, fade), title);

        ImGui::SetCursorScreenPos(ImVec2(contentX + kContentPad,
                                         winMin.y + kTopBarHeight + 44.0f + (1.0f - fade) * 10.0f));

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, opacity * fade);
        ImGui::BeginChild("##content",
                          ImVec2(ws.x - sidebarW - kContentPad * 2.0f,
                                 std::max(80.0f, ws.y - kTopBarHeight - 62.0f)),
                          0, ImGuiWindowFlags_NoBackground);

        if (g.search[0] != '\0')
            DrawModuleList();
        else if (g.shell == Shell_Settings)
            DrawSettingsPage();
        else if (g.shell == Shell_Friends)
            DrawFriendsPage();
        else if (g.shell == Shell_Config)
            DrawConfigPage();
        else
            DrawModuleList();

        ImGui::EndChild();
        ImGui::PopStyleVar();
    }
    ImGui::End();

    DrawNotifications(screenAnchor);
    DrawTooltip();

    ImGui::PopStyleVar(3);
}

void Gui::RenderFakeLogin()
{
    if (!Menu::Initialized)
        return;

    if (!g.styled)
        InitializeStyle();

    Ease(g.fakeLoginAnim, Menu::FakeLoginOpen ? 1.0f : 0.0f, 17.0f);

    if (!Menu::FakeLoginOpen && g.fakeLoginAnim < 0.004f)
        return;

    const float opacity = SmoothStep(g.fakeLoginAnim);
    SetGlobalOpacity(opacity);
    DrawFakeLogin(opacity);
}

void Gui::RenderItemLogger()
{
    if (!Menu::Initialized)
        return;

    if (!g.styled)
        InitializeStyle();

    Ease(g.itemLogAnim, Menu::ItemLogOpen ? 1.0f : 0.0f, 17.0f);

    if (!Menu::ItemLogOpen && g.itemLogAnim < 0.004f)
        return;

    const float opacity = SmoothStep(g.itemLogAnim);
    SetGlobalOpacity(opacity);
    DrawItemLogger(opacity);
}
