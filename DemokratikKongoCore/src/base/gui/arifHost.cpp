#include "arifHost.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "arif/gui/ClickGUI.hpp"
#include "arif/gui/HostHooks.hpp"
#include "arif/gui/IconPack.hpp"
#include "arif/gui/Panels.hpp"

// NOTE: <Windows.h> (via rpcndr.h) defines `small`/`hyper` as macros, which
// would mangle the arif Fonts struct member `ImFont* small`. Keep it after
// the arif headers (menu.h includes it anyway).
#include <Windows.h>

#include "guiCore.h"
#include "../menu/menu.h"
#include "../moduleManager/moduleManager.h"
#include "../moduleManager/commonData.h"
#include "../moduleManager/modules/combat/friends.h"
#include "../config/config.h"

/*
========================================================================
    ArifGUI host bridge (see arifHost.h)
------------------------------------------------------------------------
    Everything the vendored ClickGUI needs from the live client is funnelled
    through this file:

      - ModuleManager::All() -> arif::gui::ModuleRegistry (modules +
        settings + keybinds)
      - Friends (static utility) <-> arif::gui::FriendManager
      - ProfileManager <-> arif::gui::ConfigManager
      - CommonData snapshot -> SessionStats (fps / username)

    Values are synchronised with a two-way diff every frame: every link
    remembers the last value it published, so a change coming from either
    side (GUI click or config load / in-game keybind) wins exactly once and
    never oscillates.
========================================================================
*/

namespace
{
namespace agui = arif::gui;

agui::ClickGUI g_gui;
bool g_ready = false;

// ---------------------------------------------------------------------------
// ImGuiKey <-> virtual-key conversion (module/menu keybinds)
//
// The GUI stores/binds Dear ImGui key ids; the client (ModuleManager,
// wndProc) stores Win32 virtual-key codes. Only the keys the GameBind UI can
// actually produce are mapped; anything unmappable becomes 0 (unbound).
// ---------------------------------------------------------------------------
ImGuiKey vkToImGuiKey(int vk)
{
    if (vk >= 'A' && vk <= 'Z') return static_cast<ImGuiKey>(ImGuiKey_A + (vk - 'A'));
    if (vk >= '0' && vk <= '9') return static_cast<ImGuiKey>(ImGuiKey_0 + (vk - '0'));
    if (vk >= VK_F1 && vk <= VK_F24) return static_cast<ImGuiKey>(ImGuiKey_F1 + (vk - VK_F1));
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return static_cast<ImGuiKey>(ImGuiKey_Keypad0 + (vk - VK_NUMPAD0));

    switch (vk)
    {
    case VK_TAB:         return ImGuiKey_Tab;
    case VK_LEFT:        return ImGuiKey_LeftArrow;
    case VK_RIGHT:       return ImGuiKey_RightArrow;
    case VK_UP:          return ImGuiKey_UpArrow;
    case VK_DOWN:        return ImGuiKey_DownArrow;
    case VK_PRIOR:       return ImGuiKey_PageUp;
    case VK_NEXT:        return ImGuiKey_PageDown;
    case VK_HOME:        return ImGuiKey_Home;
    case VK_END:         return ImGuiKey_End;
    case VK_INSERT:      return ImGuiKey_Insert;
    case VK_DELETE:      return ImGuiKey_Delete;
    case VK_BACK:        return ImGuiKey_Backspace;
    case VK_SPACE:       return ImGuiKey_Space;
    case VK_RETURN:      return ImGuiKey_Enter;
    case VK_ESCAPE:      return ImGuiKey_Escape;
    case VK_OEM_7:       return ImGuiKey_Apostrophe;
    case VK_OEM_COMMA:   return ImGuiKey_Comma;
    case VK_OEM_MINUS:   return ImGuiKey_Minus;
    case VK_OEM_PERIOD:  return ImGuiKey_Period;
    case VK_OEM_2:       return ImGuiKey_Slash;
    case VK_OEM_1:       return ImGuiKey_Semicolon;
    case VK_OEM_PLUS:    return ImGuiKey_Equal;
    case VK_OEM_4:       return ImGuiKey_LeftBracket;
    case VK_OEM_5:       return ImGuiKey_Backslash;
    case VK_OEM_6:       return ImGuiKey_RightBracket;
    case VK_CAPITAL:     return ImGuiKey_CapsLock;
    case VK_SCROLL:      return ImGuiKey_ScrollLock;
    case VK_NUMLOCK:     return ImGuiKey_NumLock;
    case VK_SNAPSHOT:    return ImGuiKey_PrintScreen;
    case VK_PAUSE:       return ImGuiKey_Pause;
    case VK_MULTIPLY:    return ImGuiKey_KeypadMultiply;
    case VK_ADD:         return ImGuiKey_KeypadAdd;
    case VK_SUBTRACT:    return ImGuiKey_KeypadSubtract;
    case VK_DECIMAL:     return ImGuiKey_KeypadDecimal;
    case VK_DIVIDE:      return ImGuiKey_KeypadDivide;
    case VK_SHIFT:       return ImGuiKey_LeftShift;
    case VK_LSHIFT:      return ImGuiKey_LeftShift;
    case VK_RSHIFT:      return ImGuiKey_RightShift;
    case VK_CONTROL:     return ImGuiKey_LeftCtrl;
    case VK_LCONTROL:    return ImGuiKey_LeftCtrl;
    case VK_RCONTROL:    return ImGuiKey_RightCtrl;
    case VK_MENU:        return ImGuiKey_LeftAlt;
    case VK_LMENU:       return ImGuiKey_LeftAlt;
    case VK_RMENU:       return ImGuiKey_RightAlt;
    case VK_LBUTTON:     return ImGuiKey_MouseLeft;
    case VK_RBUTTON:     return ImGuiKey_MouseRight;
    case VK_MBUTTON:     return ImGuiKey_MouseMiddle;
    case VK_XBUTTON1:    return ImGuiKey_MouseX1;
    case VK_XBUTTON2:    return ImGuiKey_MouseX2;
    default:             return ImGuiKey_None;
    }
}

int imGuiKeyToVk(int key)
{
    if (key >= ImGuiKey_A && key <= ImGuiKey_Z) return 'A' + (key - ImGuiKey_A);
    if (key >= ImGuiKey_0 && key <= ImGuiKey_9) return '0' + (key - ImGuiKey_0);
    if (key >= ImGuiKey_F1 && key <= ImGuiKey_F24) return VK_F1 + (key - ImGuiKey_F1);
    if (key >= ImGuiKey_Keypad0 && key <= ImGuiKey_Keypad9) return VK_NUMPAD0 + (key - ImGuiKey_Keypad0);

    switch (static_cast<ImGuiKey>(key))
    {
    case ImGuiKey_Tab:            return VK_TAB;
    case ImGuiKey_LeftArrow:      return VK_LEFT;
    case ImGuiKey_RightArrow:     return VK_RIGHT;
    case ImGuiKey_UpArrow:        return VK_UP;
    case ImGuiKey_DownArrow:      return VK_DOWN;
    case ImGuiKey_PageUp:         return VK_PRIOR;
    case ImGuiKey_PageDown:       return VK_NEXT;
    case ImGuiKey_Home:           return VK_HOME;
    case ImGuiKey_End:            return VK_END;
    case ImGuiKey_Insert:         return VK_INSERT;
    case ImGuiKey_Delete:         return VK_DELETE;
    case ImGuiKey_Backspace:      return VK_BACK;
    case ImGuiKey_Space:          return VK_SPACE;
    case ImGuiKey_Enter:          return VK_RETURN;
    case ImGuiKey_Escape:         return VK_ESCAPE;
    case ImGuiKey_Apostrophe:     return VK_OEM_7;
    case ImGuiKey_Comma:          return VK_OEM_COMMA;
    case ImGuiKey_Minus:          return VK_OEM_MINUS;
    case ImGuiKey_Period:         return VK_OEM_PERIOD;
    case ImGuiKey_Slash:          return VK_OEM_2;
    case ImGuiKey_Semicolon:      return VK_OEM_1;
    case ImGuiKey_Equal:          return VK_OEM_PLUS;
    case ImGuiKey_LeftBracket:    return VK_OEM_4;
    case ImGuiKey_Backslash:      return VK_OEM_5;
    case ImGuiKey_RightBracket:   return VK_OEM_6;
    case ImGuiKey_CapsLock:       return VK_CAPITAL;
    case ImGuiKey_ScrollLock:     return VK_SCROLL;
    case ImGuiKey_NumLock:        return VK_NUMLOCK;
    case ImGuiKey_PrintScreen:    return VK_SNAPSHOT;
    case ImGuiKey_Pause:          return VK_PAUSE;
    case ImGuiKey_KeypadMultiply: return VK_MULTIPLY;
    case ImGuiKey_KeypadAdd:      return VK_ADD;
    case ImGuiKey_KeypadSubtract: return VK_SUBTRACT;
    case ImGuiKey_KeypadDecimal:  return VK_DECIMAL;
    case ImGuiKey_KeypadDivide:   return VK_DIVIDE;
    case ImGuiKey_LeftShift:      return VK_LSHIFT;
    case ImGuiKey_RightShift:     return VK_RSHIFT;
    case ImGuiKey_LeftCtrl:       return VK_LCONTROL;
    case ImGuiKey_RightCtrl:      return VK_RCONTROL;
    case ImGuiKey_LeftAlt:        return VK_LMENU;
    case ImGuiKey_RightAlt:       return VK_RMENU;
    case ImGuiKey_MouseLeft:      return VK_LBUTTON;
    case ImGuiKey_MouseRight:     return VK_RBUTTON;
    case ImGuiKey_MouseMiddle:    return VK_MBUTTON;
    case ImGuiKey_MouseX1:        return VK_XBUTTON1;
    case ImGuiKey_MouseX2:        return VK_XBUTTON2;
    default:                      return 0;
    }
}

// ---------------------------------------------------------------------------
// Value links
// ---------------------------------------------------------------------------
enum class RangeHandle { None, Low, High };

struct SettingLink
{
    agui::GUISetting* gui = nullptr;
    Setting* dk = nullptr;
    RangeHandle range = RangeHandle::None;

    bool        lastBool = false;
    int         lastInt = 0;
    float       lastFloat = 0.0f;
    ImVec4      lastColor{};
    int         lastKeyGui = 0;
    int         lastKeyVk = 0;
    std::string lastText;

    SettingLink() = default;
    SettingLink(agui::GUISetting* g, Setting* s) : gui(g), dk(s) {}
};

struct ModuleLink
{
    agui::GUIModule* gui = nullptr;
    Module* dk = nullptr;
    bool lastEnabled = false;
    bool lastFavorite = false;
    std::vector<SettingLink> settings;
};

std::vector<ModuleLink> g_links;
size_t g_boundModuleCount = SIZE_MAX;

// friends / configs / stats sync state
uint64_t g_friendsVersion = 0;
bool g_configHooksSet = false;

// theme persistence (any thread writes; Render() applies)
int g_pendingTheme = 2;
bool g_themeDirty = false;

// keybind-capture bookkeeping (capture is cancelled when its panel closes)
uint64_t g_uiFrame = 0;
uint64_t g_optionsSeenFrame = 0;
bool g_menuBindListening = false;

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
std::string toLowerCopy(const std::string& value)
{
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::unordered_set<std::string> parseCsvSet(const std::string& csv)
{
    std::unordered_set<std::string> out;
    std::string item;
    auto flush = [&]() {
        // The legacy loader trimmed whitespace ("A, B" style values exist).
        const size_t begin = item.find_first_not_of(" \t");
        if (begin != std::string::npos) {
            const size_t end = item.find_last_not_of(" \t");
            out.insert(item.substr(begin, end - begin + 1));
        }
        item.clear();
    };
    for (char c : csv)
    {
        if (c == ',')
        {
            flush();
        }
        else
        {
            item.push_back(c);
        }
    }
    flush();
    return out;
}

agui::Category mapCategory(::Category category)
{
    switch (category)
    {
    case ::Category::Combat:   return agui::Category::Combat;
    case ::Category::Movement: return agui::Category::Movement;
    case ::Category::Render:   return agui::Category::Render;
    case ::Category::Utility:  return agui::Category::Utility;
    case ::Category::Misc:     return agui::Category::Misc;
    default:                   return agui::Category::Misc;
    }
}

// Vector icon used when the PNG pack / FA font has no glyph for the module.
agui::ModuleIcon iconForModule(const std::string& name, ::Category category)
{
    const std::string n = toLowerCopy(name);
    auto has = [&](const char* needle) { return n.find(needle) != std::string::npos; };

    if (has("fullbright") || has("bright"))                       return agui::ModuleIcon::Sun;
    if (has("killaura") || has("aura") || has("clicker") || has("crit")) return agui::ModuleIcon::Fist;
    if (has("refill") || has("armor") || has("pot"))              return agui::ModuleIcon::Potion;
    if (has("sprint") || has("speed") || has("velocity") || has("delay")) return agui::ModuleIcon::Arrows;
    if (has("blink") || has("teleport") || has("camera"))         return agui::ModuleIcon::Teleport;
    if (has("nametag") || has("friend"))                          return agui::ModuleIcon::Person;
    if (has("esp") || has("tracer") || has("render"))             return agui::ModuleIcon::Eye;
    if (has("search") || has("logger") || has("item") || has("macro")) return agui::ModuleIcon::Search;

    switch (category)
    {
    case ::Category::Combat:   return agui::ModuleIcon::Sword;
    case ::Category::Movement: return agui::ModuleIcon::Arrows;
    case ::Category::Render:   return agui::ModuleIcon::Eye;
    case ::Category::Utility:  return agui::ModuleIcon::Potion;
    case ::Category::Misc:     return agui::ModuleIcon::Generic;
    default:                   return agui::ModuleIcon::Generic;
    }
}

const char* floatFormatForStep(float step)
{
    if (step >= 0.999f) return "%.0f";
    if (step >= 0.099f) return "%.1f";
    if (step >= 0.0099f) return "%.2f";
    return "%.3f";
}

std::string labelWithSuffix(const char* name, const char* suffix)
{
    std::string label = name ? name : "";
    if (suffix && *suffix)
    {
        label += " (";
        label += suffix;
        label += ")";
    }
    return label;
}

// ---------------------------------------------------------------------------
// Binding construction
// ---------------------------------------------------------------------------
void bindSetting(ModuleLink& link, Setting* s)
{
    if (s == nullptr)
    {
        return;
    }

    switch (s->type)
    {
    case SettingType::Bool:
    {
        auto* b = static_cast<BoolSetting*>(s);
        agui::GUISetting& gui = link.gui->addBool(s->name, s->name, b->value, s->description);
        gui.visibleIf = [s] { return s->visible(); };
        SettingLink sl(&gui, s);
        sl.lastBool = b->value;
        link.settings.push_back(std::move(sl));
        break;
    }
    case SettingType::Number:
    {
        auto* n = static_cast<NumberSetting*>(s);
        const bool integer = n->slotPicker || n->step >= 0.999f;
        const std::string label = labelWithSuffix(s->name, n->suffix);
        if (integer)
        {
            agui::GUISetting& gui = link.gui->addInt(s->name, label,
                static_cast<int>(std::lround(n->value)),
                static_cast<int>(std::lround(n->min)),
                static_cast<int>(std::lround(n->max)), s->description);
            gui.visibleIf = [s] { return s->visible(); };
            SettingLink sl(&gui, s);
            sl.lastInt = static_cast<int>(std::lround(n->value));
            link.settings.push_back(std::move(sl));
        }
        else
        {
            agui::GUISetting& gui = link.gui->addFloat(s->name, label, n->value, n->min, n->max,
                                                       s->description, floatFormatForStep(n->step));
            gui.visibleIf = [s] { return s->visible(); };
            SettingLink sl(&gui, s);
            sl.lastFloat = n->value;
            link.settings.push_back(std::move(sl));
        }
        break;
    }
    case SettingType::Enum:
    {
        auto* e = static_cast<EnumSetting*>(s);
        std::vector<std::string> options;
        options.reserve(e->options.size());
        for (const char* option : e->options)
        {
            options.emplace_back(option ? option : "?");
        }
        agui::GUISetting& gui = link.gui->addEnum(s->name, s->name, e->index, std::move(options),
                                                  s->description);
        gui.visibleIf = [s] { return s->visible(); };
        SettingLink sl(&gui, s);
        sl.lastInt = e->index;
        link.settings.push_back(std::move(sl));
        break;
    }
    case SettingType::Keybind:
    {
        auto* k = static_cast<KeybindSetting*>(s);
        agui::GUISetting& gui = link.gui->addKeybind(s->name, s->name,
                                                     static_cast<int>(vkToImGuiKey(k->virtualKey)),
                                                     s->description);
        gui.visibleIf = [s] { return s->visible(); };
        SettingLink sl(&gui, s);
        sl.lastKeyGui = gui.keyValue;
        sl.lastKeyVk = k->virtualKey;
        link.settings.push_back(std::move(sl));
        break;
    }
    case SettingType::Color:
    {
        auto* c = static_cast<ColorSetting*>(s);
        const ImVec4 value(c->value.r, c->value.g, c->value.b, c->value.a);
        agui::GUISetting& gui = link.gui->addColor(s->name, s->name, value, s->description);
        gui.visibleIf = [s] { return s->visible(); };
        SettingLink sl(&gui, s);
        sl.lastColor = value;
        link.settings.push_back(std::move(sl));
        break;
    }
    case SettingType::String:
    {
        auto* t = static_cast<StringSetting*>(s);
        agui::GUISetting& gui = link.gui->addText(s->name, s->name, t->value, s->description);
        gui.visibleIf = [s] { return s->visible(); };
        std::snprintf(gui.textBuffer, sizeof(gui.textBuffer), "%s", t->value.c_str());
        SettingLink sl(&gui, s);
        sl.lastText = t->value;
        link.settings.push_back(std::move(sl));
        break;
    }
    case SettingType::IntRange:
    {
        auto* r = static_cast<IntRangeSetting*>(s);
        const std::string loLabel = labelWithSuffix((std::string(s->name) + " Min").c_str(), r->suffix);
        const std::string hiLabel = labelWithSuffix((std::string(s->name) + " Max").c_str(), r->suffix);

        agui::GUISetting& lo = link.gui->addInt(std::string(s->name) + "##lo", loLabel, r->low, r->min, r->max,
                                                s->description);
        agui::GUISetting& hi = link.gui->addInt(std::string(s->name) + "##hi", hiLabel, r->high, r->min, r->max,
                                                s->description);
        lo.visibleIf = [s] { return s->visible(); };
        hi.visibleIf = [s] { return s->visible(); };

        SettingLink slLo(&lo, s);
        slLo.range = RangeHandle::Low;
        slLo.lastInt = r->low;
        link.settings.push_back(std::move(slLo));

        SettingLink slHi(&hi, s);
        slHi.range = RangeHandle::High;
        slHi.lastInt = r->high;
        link.settings.push_back(std::move(slHi));
        break;
    }
    case SettingType::FloatRange:
    {
        auto* r = static_cast<FloatRangeSetting*>(s);
        const std::string loLabel = labelWithSuffix((std::string(s->name) + " Min").c_str(), r->suffix);
        const std::string hiLabel = labelWithSuffix((std::string(s->name) + " Max").c_str(), r->suffix);
        const char* format = floatFormatForStep(r->step);

        agui::GUISetting& lo = link.gui->addFloat(std::string(s->name) + "##lo", loLabel, r->low, r->min,
                                                  r->max, s->description, format);
        agui::GUISetting& hi = link.gui->addFloat(std::string(s->name) + "##hi", hiLabel, r->high, r->min,
                                                  r->max, s->description, format);
        lo.visibleIf = [s] { return s->visible(); };
        hi.visibleIf = [s] { return s->visible(); };

        SettingLink slLo(&lo, s);
        slLo.range = RangeHandle::Low;
        slLo.lastFloat = r->low;
        link.settings.push_back(std::move(slLo));

        SettingLink slHi(&hi, s);
        slHi.range = RangeHandle::High;
        slHi.lastFloat = r->high;
        link.settings.push_back(std::move(slHi));
        break;
    }
    case SettingType::Action:
    {
        auto* a = static_cast<ActionSetting*>(s);
        agui::GUISetting& gui = link.gui->addAction(s->name, s->name, a->label(),
                                                    [a] { if (a->onClick) a->onClick(); },
                                                    s->description);
        gui.visibleIf = [s] { return s->visible(); };
        SettingLink sl(&gui, s);
        link.settings.push_back(std::move(sl));
        break;
    }
    }
}

void buildBindings()
{
    g_links.clear();
    g_gui.invalidateSelection();
    g_gui.modules.modules.clear();

    const std::unordered_set<std::string> favorites = parseCsvSet(Gui::Prefs().favoritesCsv);

    for (const auto& owned : ModuleManager::All())
    {
        Module* m = owned.get();
        if (m == nullptr || !m->showInMenu())
        {
            continue;
        }

        agui::GUIModule& gui = g_gui.modules.add(m->name(), m->name(), m->description(),
                                                 mapCategory(m->category()));
        gui.icon = iconForModule(m->name(), m->category());
        gui.enabled = m->isEnabled();
        gui.favorite = favorites.count(m->name()) != 0;

        ModuleLink link;
        link.gui = &gui;
        link.dk = m;
        link.lastEnabled = gui.enabled;
        link.lastFavorite = gui.favorite;

        // The card only shows the toggle; the module keybind lives in the
        // settings panel (mirrors the client's "keybindEditable" modules).
        if (m->keybindEditable())
        {
            agui::GUISetting& bind = gui.addKeybind("##keybind", "Keybind",
                static_cast<int>(vkToImGuiKey(m->keybind().virtualKey)));
            SettingLink sl(&bind, &m->keybind());
            sl.lastKeyGui = bind.keyValue;
            sl.lastKeyVk = m->keybind().virtualKey;
            link.settings.push_back(std::move(sl));
        }

        for (Setting* s : m->settings())
        {
            bindSetting(link, s);
        }

        g_links.push_back(std::move(link));
    }

    g_boundModuleCount = ModuleManager::All().size();
}

// ---------------------------------------------------------------------------
// Per-frame synchronisation
// ---------------------------------------------------------------------------
void syncModuleLink(ModuleLink& link)
{
    agui::GUIModule& gui = *link.gui;
    Module& module = *link.dk;

    // enabled (two-way diff)
    if (gui.enabled != link.lastEnabled)
    {
        module.setEnabled(gui.enabled);
        link.lastEnabled = gui.enabled;
    }
    else if (module.isEnabled() != link.lastEnabled)
    {
        gui.enabled = module.isEnabled();
        link.lastEnabled = gui.enabled;
    }
}

void syncSettingLink(SettingLink& sl)
{
    agui::GUISetting& gui = *sl.gui;

    switch (sl.dk->type)
    {
    case SettingType::Bool:
    {
        auto* dk = static_cast<BoolSetting*>(sl.dk);
        if (gui.boolValue != sl.lastBool)
        {
            dk->value = gui.boolValue;
            sl.lastBool = gui.boolValue;
        }
        else if (dk->value != sl.lastBool)
        {
            gui.boolValue = dk->value;
            sl.lastBool = dk->value;
        }
        break;
    }
    case SettingType::Number:
    {
        auto* dk = static_cast<NumberSetting*>(sl.dk);
        if (gui.kind == agui::SettingKind::Int)
        {
            if (gui.intValue != sl.lastInt)
            {
                dk->value = static_cast<float>(gui.intValue);
                sl.lastInt = gui.intValue;
            }
            else
            {
                const int dkInt = static_cast<int>(std::lround(dk->value));
                if (dkInt != sl.lastInt)
                {
                    gui.intValue = dkInt;
                    sl.lastInt = dkInt;
                }
            }
        }
        else
        {
            if (std::fabs(gui.floatValue - sl.lastFloat) > 0.0005f)
            {
                dk->value = gui.floatValue;
                sl.lastFloat = gui.floatValue;
            }
            else if (std::fabs(dk->value - sl.lastFloat) > 0.0005f)
            {
                gui.floatValue = dk->value;
                sl.lastFloat = dk->value;
            }
        }
        break;
    }
    case SettingType::Enum:
    {
        auto* dk = static_cast<EnumSetting*>(sl.dk);
        if (gui.intValue != sl.lastInt)
        {
            if (gui.intValue >= 0 && gui.intValue < static_cast<int>(dk->options.size()))
            {
                dk->index = gui.intValue;
            }
            sl.lastInt = gui.intValue;
        }
        else if (dk->index != sl.lastInt)
        {
            gui.intValue = dk->index;
            sl.lastInt = dk->index;
        }
        break;
    }
    case SettingType::Keybind:
    {
        auto* dk = static_cast<KeybindSetting*>(sl.dk);
        if (gui.keyValue != sl.lastKeyGui)
        {
            dk->virtualKey = imGuiKeyToVk(gui.keyValue);
            sl.lastKeyGui = gui.keyValue;
            sl.lastKeyVk = dk->virtualKey;
        }
        else if (dk->virtualKey != sl.lastKeyVk)
        {
            gui.keyValue = static_cast<int>(vkToImGuiKey(dk->virtualKey));
            sl.lastKeyGui = gui.keyValue;
            sl.lastKeyVk = dk->virtualKey;
        }
        break;
    }
    case SettingType::Color:
    {
        auto* dk = static_cast<ColorSetting*>(sl.dk);
        const ImVec4 current(dk->value.r, dk->value.g, dk->value.b, dk->value.a);
        const float diff = std::fabs(gui.colorValue.x - sl.lastColor.x)
                         + std::fabs(gui.colorValue.y - sl.lastColor.y)
                         + std::fabs(gui.colorValue.z - sl.lastColor.z)
                         + std::fabs(gui.colorValue.w - sl.lastColor.w);
        if (diff > 0.002f)
        {
            dk->value.r = gui.colorValue.x;
            dk->value.g = gui.colorValue.y;
            dk->value.b = gui.colorValue.z;
            dk->value.a = gui.colorValue.w;
            sl.lastColor = gui.colorValue;
        }
        else
        {
            const float dkDiff = std::fabs(current.x - sl.lastColor.x)
                               + std::fabs(current.y - sl.lastColor.y)
                               + std::fabs(current.z - sl.lastColor.z)
                               + std::fabs(current.w - sl.lastColor.w);
            if (dkDiff > 0.002f)
            {
                gui.colorValue = current;
                sl.lastColor = current;
            }
        }
        break;
    }
    case SettingType::String:
    {
        auto* dk = static_cast<StringSetting*>(sl.dk);
        if (gui.textValue != sl.lastText)
        {
            dk->value = gui.textValue.substr(0, dk->maxLength);
            sl.lastText = dk->value;
        }
        else if (dk->value != sl.lastText)
        {
            gui.textValue = dk->value;
            sl.lastText = dk->value;
        }
        break;
    }
    case SettingType::IntRange:
    {
        auto* dk = static_cast<IntRangeSetting*>(sl.dk);
        int& field = (sl.range == RangeHandle::High) ? dk->high : dk->low;
        if (gui.intValue != sl.lastInt)
        {
            field = gui.intValue;
            sl.lastInt = gui.intValue;
        }
        else if (field != sl.lastInt)
        {
            gui.intValue = field;
            sl.lastInt = field;
        }
        break;
    }
    case SettingType::FloatRange:
    {
        auto* dk = static_cast<FloatRangeSetting*>(sl.dk);
        float& field = (sl.range == RangeHandle::High) ? dk->high : dk->low;
        if (std::fabs(gui.floatValue - sl.lastFloat) > 0.0005f)
        {
            field = gui.floatValue;
            sl.lastFloat = gui.floatValue;
        }
        else if (std::fabs(field - sl.lastFloat) > 0.0005f)
        {
            gui.floatValue = field;
            sl.lastFloat = field;
        }
        break;
    }
    case SettingType::Action:
    {
        // The provider can be expensive (ItemLogger deep-copies its log
        // list); refresh the button face only while the menu is visible.
        if (!Menu::Open)
        {
            break;
        }
        auto* dk = static_cast<ActionSetting*>(sl.dk);
        const std::string label = dk->label();
        if (gui.actionLabel != label)
        {
            gui.actionLabel = label;
        }
        break;
    }
    }
}

// Friends: the client's Friends utility is the source of truth. GUI edits
// from the previous frame are applied first (unless the client list changed
// on its own, e.g. a middle-click bind), then the manager is mirrored back.
void syncFriends(const std::vector<CommonData::PlayerSnapshot>& players)
{
    agui::FriendManager& fm = g_gui.friends;

    const uint64_t version = Friends::Version.load(std::memory_order_relaxed);
    const bool externalChange = (version != g_friendsVersion);

    if (!externalChange)
    {
        std::unordered_set<std::string> guiSet;
        guiSet.reserve(fm.friends.size());
        for (const auto& entry : fm.friends)
        {
            guiSet.insert(entry.name);
        }

        std::vector<std::string> toToggle;
        for (const auto& entry : fm.friends)
        {
            if (!Friends::IsFriend(entry.name))
            {
                toToggle.push_back(entry.name);   // added through the GUI
            }
        }
        for (const auto& name : Friends::List)
        {
            if (guiSet.find(name) == guiSet.end())
            {
                toToggle.push_back(name);          // removed through the GUI
            }
        }
        for (const auto& name : toToggle)
        {
            Friends::ToggleFriend(name);
        }
    }

    // Mirror the client -> GUI.
    fm.friends.clear();
    fm.friends.reserve(Friends::List.size());
    for (const auto& name : Friends::List)
    {
        fm.friends.push_back(agui::PlayerEntry{ name, 0 });
    }
    std::sort(fm.friends.begin(), fm.friends.end(),
              [](const agui::PlayerEntry& a, const agui::PlayerEntry& b) { return a.name < b.name; });

    // Online players come from the render-thread snapshot (never JNI here).
    fm.online.clear();
    for (const auto& player : players)
    {
        if (!player.isLocalPlayer && !player.name.empty())
        {
            fm.online.push_back(agui::PlayerEntry{ player.name, 0 });
        }
    }
    std::sort(fm.online.begin(), fm.online.end(),
              [](const agui::PlayerEntry& a, const agui::PlayerEntry& b) { return a.name < b.name; });

    // Targets are session-local: the client has no target list.
    g_friendsVersion = Friends::Version.load(std::memory_order_relaxed);
}

void syncConfigs()
{
    agui::ConfigManager& cm = g_gui.configs;

    if (!g_configHooksSet)
    {
        cm.onLoad = [](const std::string& name) { ProfileManager::load(name); };
        cm.onSave = [](const std::string& name) { ProfileManager::save(name); };
        cm.onDelete = [](const std::string& name) { ProfileManager::remove(name); };
        cm.onCreate = [](const std::string& name) { ProfileManager::save(name); };
        cm.onExport = []() -> std::string {
            std::ifstream in(ProfileManager::profilePath(ProfileManager::ActiveProfile), std::ios::binary);
            if (!in) return std::string();
            std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            return content;
        };
        cm.onImport = [](const std::string& text) -> bool {
            if (text.empty() || text.find('{') == std::string::npos) return false;
            std::string name = "Imported";
            const size_t key = text.find("\"profile\"");
            if (key != std::string::npos) {
                const size_t colon = text.find(':', key);
                const size_t openQuote = (colon != std::string::npos) ? text.find('"', colon + 1) : std::string::npos;
                const size_t closeQuote = (openQuote != std::string::npos) ? text.find('"', openQuote + 1) : std::string::npos;
                if (closeQuote != std::string::npos && closeQuote > openQuote + 1) {
                    name = text.substr(openQuote + 1, closeQuote - openQuote - 1);
                }
            }
            std::ofstream out(ProfileManager::profilePath(name), std::ios::binary);
            if (!out) return false;
            out << text;
            return true;
        };
        g_configHooksSet = true;
    }

    cm.profiles.clear();
    for (const std::string& name : ProfileManager::listProfiles())
    {
        cm.profiles.push_back(agui::ConfigProfile{ name, name == "default", name == ProfileManager::ActiveProfile });
    }
}

void syncStats(const std::vector<CommonData::PlayerSnapshot>& players)
{
    agui::SessionStats& stats = g_gui.stats;
    stats.fps = static_cast<int>(ImGui::GetIO().Framerate + 0.5f);
    stats.ping = 0;

    stats.username = "Player";
    for (const auto& player : players)
    {
        if (player.isLocalPlayer && !player.name.empty())
        {
            stats.username = player.name;
            break;
        }
    }
}

void pushFavorites()
{
    std::string csv;
    for (const ModuleLink& link : g_links)
    {
        if (!link.gui->favorite)
        {
            continue;
        }
        if (!csv.empty())
        {
            csv += ',';
        }
        csv += link.dk->name();
    }
    Gui::Prefs().favoritesCsv = std::move(csv);
}

} // namespace

// ---------------------------------------------------------------------------
// Options overlay extras (see HostHooks.hpp)
// ---------------------------------------------------------------------------
namespace arif::gui::host
{
    void optionsPanelRows()
    {
        panels::hostToggleRow("Middleclick friends", &Friends::MiddleClick);

        // Menu toggle key: the client stores a virtual-key code, the row
        // works with ImGui key ids - keep a mirrored value.
        g_optionsSeenFrame = g_uiFrame;   // capture auto-cancel (see Render)
        static bool initialized = false;
        static int guiKey = 0;
        static int lastVk = 0;

        if (!initialized)
        {
            initialized = true;
            lastVk = Menu::Keybind;
            guiKey = static_cast<int>(vkToImGuiKey(lastVk));
        }
        else if (Menu::Keybind != lastVk)
        {
            lastVk = Menu::Keybind;
            guiKey = static_cast<int>(vkToImGuiKey(lastVk));
        }

        panels::hostKeybindRow("##menu_bind", &guiKey, &g_menuBindListening);

        if (guiKey != static_cast<int>(vkToImGuiKey(lastVk)))
        {
            Menu::Keybind = imGuiKeyToVk(guiKey);
            lastVk = Menu::Keybind;
        }
    }
} // namespace arif::gui::host

// ---------------------------------------------------------------------------
// Public lifecycle
// ---------------------------------------------------------------------------
void ArifGUI::Init()
{
    if (g_ready)
    {
        return;
    }

    // ImGui context + overlay GL context must already be current (called at
    // the end of Menu::SetupImgui).
    g_gui.initialize();
    arif::gui::iconpack::initialize(nullptr);

    // Frameless title-strip controls: the in-game overlay has no OS window to
    // move/minimize, so both controls close the menu. The GUI itself is a
    // floating (non-fullscreen) panel and handles its own dragging, so
    // onDragDelta stays unbound.
    g_gui.onClose = [] { Menu::Open = false; };
    g_gui.onMinimize = [] { Menu::Open = false; };

    g_boundModuleCount = SIZE_MAX;   // bind on the first Render (modules may not be registered yet)
    g_ready = true;
}

void ArifGUI::Render()
{
    if (!g_ready)
    {
        return;
    }

    // The GUI window is the source of truth for visibility; Menu::Open is
    // flipped by the wndProc hook (Insert / ESC) and by the close button.
    g_gui.open = Menu::Open;
    ++g_uiFrame;

    if (g_themeDirty)
    {
        arif::gui::applyThemePreset(g_pendingTheme);
        g_themeDirty = false;
    }

    const size_t moduleCount = ModuleManager::All().size();
    if (moduleCount != g_boundModuleCount)
    {
        buildBindings();
    }

    bool favoritesChanged = false;
    const std::unordered_set<std::string> prefsFavorites = parseCsvSet(Gui::Prefs().favoritesCsv);

    for (ModuleLink& link : g_links)
    {
        // favourites (config can change them behind our back)
        const bool wanted = prefsFavorites.count(link.dk->name()) != 0;
        if (link.gui->favorite != link.lastFavorite)
        {
            link.lastFavorite = link.gui->favorite;
            favoritesChanged = true;
        }
        else if (wanted != link.lastFavorite)
        {
            link.gui->favorite = wanted;
            link.lastFavorite = wanted;
        }

        syncModuleLink(link);
        for (SettingLink& sl : link.settings)
        {
            syncSettingLink(sl);
        }
    }

    if (favoritesChanged)
    {
        pushFavorites();
    }

    // Keybind capture must never outlive its panel: closing the menu (or
    // switching away from the module panel) cancels it, so a stale
    // "PRESS A KEY..." can't swallow the next key press.
    if (!Menu::Open)
    {
        g_menuBindListening = false;
    }
    for (ModuleLink& link : g_links)
    {
        if (Menu::Open && g_gui.inspected == link.gui)
        {
            continue;   // its panel is visible: capture stays live
        }
        for (SettingLink& sl : link.settings)
        {
            if (sl.dk->type == SettingType::Keybind)
            {
                sl.gui->listeningKey = false;
            }
        }
    }
    if (Menu::Open && g_menuBindListening && g_optionsSeenFrame + 1 < g_uiFrame)
    {
        g_menuBindListening = false;   // Options overlay was closed while listening
    }

    if (Menu::Open)
    {
        // One cached snapshot per frame (render thread: never JNI here).
        const std::vector<CommonData::PlayerSnapshot> players = CommonData::SnapshotPlayers();
        syncFriends(players);
        syncConfigs();
        syncStats(players);
    }

    const float deltaTime = ImGui::GetIO().DeltaTime;
    g_gui.update(deltaTime);
    g_gui.RenderClickGUI();
}

void ArifGUI::OnGLContextLost()
{
    if (!g_ready)
    {
        return;
    }
    // The overlay GL context is gone (or is not current): do NOT call
    // glDeleteTextures here - the same texture names may belong to live
    // Minecraft textures in whatever context is current. Bookkeeping only;
    // OnGLContextRestored re-uploads into the new context.
    arif::gui::iconpack::forget();
}

void ArifGUI::OnGLContextRestored()
{
    if (!g_ready)
    {
        return;
    }
    arif::gui::iconpack::initialize(nullptr);
}

void ArifGUI::Shutdown()
{
    if (!g_ready)
    {
        return;
    }

    arif::gui::iconpack::shutdown();

    g_links.clear();
    g_gui.inspected = nullptr;
    g_gui.modules.modules.clear();
    g_gui.friends = arif::gui::FriendManager{};
    g_gui.configs = arif::gui::ConfigManager{};
    g_gui.stats = arif::gui::SessionStats{};
    g_gui.open = false;

    g_boundModuleCount = SIZE_MAX;
    g_friendsVersion = 0;
    g_configHooksSet = false;
    g_ready = false;
}

int ArifGUI::ThemeIndex()
{
    return arif::gui::currentThemeIndex();
}

void ArifGUI::SetThemeIndex(int index)
{
    g_pendingTheme = index;
    g_themeDirty = true;
}
