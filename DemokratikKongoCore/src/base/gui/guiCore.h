#pragma once

#include "imgui.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>

/*
========================================================================
    DemokratikKongo :: GUI core
------------------------------------------------------------------------
    Shared foundation for the ClickGUI:
      - framerate independent easing + spring animation
      - per-widget animation state keyed by ImGuiID
      - the colour palette / accent system
      - font helpers (the atlas only holds 3 fonts, so every size is
        rendered through ImDrawList::AddText(font, size, ...))
      - procedural vector icons (no icon font is embedded)
========================================================================
*/

namespace Gui
{
    // ------------------------------------------------------------ animation

    inline float DeltaTime()
    {
        float dt = ImGui::GetIO().DeltaTime;
        if (dt < 0.0f) dt = 0.0f;
        if (dt > 0.05f) dt = 0.05f;   // clamp alt-tab / loading stalls
        return dt;
    }

    // 1.0 normally. Raised hard when "reduce motion" is on so every easing
    // call lands on its target within a single frame.
    inline float& MotionMultiplier()
    {
        static float value = 1.0f;
        return value;
    }

    inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

    inline float SmoothStep(float v)
    {
        v = Clamp01(v);
        return v * v * (3.0f - 2.0f * v);
    }

    inline float EaseRate(float speed)
    {
        return 1.0f - std::exp(-speed * MotionMultiplier() * DeltaTime());
    }

    inline void Ease(float& value, float target, float speed)
    {
        value += (target - value) * EaseRate(speed);
    }

    inline void Ease(ImVec4& value, const ImVec4& target, float speed)
    {
        const float t = EaseRate(speed);
        value.x += (target.x - value.x) * t;
        value.y += (target.y - value.y) * t;
        value.z += (target.z - value.z) * t;
        value.w += (target.w - value.w) * t;
    }

    // Spring model lifted from the liquid-glass sample: gives toggles and
    // knobs their overshoot instead of a flat lerp.
    enum class SpringStyle { Fast, Critical, Bouncy, Overdamped };

    struct Spring
    {
        float x = 0.0f, v = 0.0f, target = 0.0f;
        float stiffness = 280.0f, damping = 0.0f, mass = 1.0f;

        static Spring Make(SpringStyle style, float start = 0.0f)
        {
            Spring sp;
            sp.x = start;
            sp.target = start;
            switch (style)
            {
                case SpringStyle::Fast:       sp.stiffness = 450.0f; sp.damping = 2.00f * std::sqrt(sp.stiffness * sp.mass); break;
                case SpringStyle::Critical:   sp.stiffness = 280.0f; sp.damping = 2.00f * std::sqrt(sp.stiffness * sp.mass); break;
                case SpringStyle::Bouncy:     sp.stiffness = 320.0f; sp.damping = 1.10f * std::sqrt(sp.stiffness * sp.mass); break;
                case SpringStyle::Overdamped: sp.stiffness = 220.0f; sp.damping = 3.20f * std::sqrt(sp.stiffness * sp.mass); break;
            }
            return sp;
        }

        void Tick(float dt)
        {
            if (damping <= 0.0f)
                damping = 2.0f * std::sqrt(stiffness * mass);

            const int steps = (dt > 1.0f / 120.0f) ? 2 : 1;
            const float h = dt / static_cast<float>(steps);
            for (int i = 0; i < steps; ++i)
            {
                const float force = -stiffness * (x - target) - damping * v;
                v += (force / mass) * h;
                x += v * h;
            }
        }

        void Snap(float to) { x = to; v = 0.0f; target = to; }
    };

    // ------------------------------------------------- per widget anim state

    namespace Detail
    {
        struct StateBlock
        {
            void* ptr = nullptr;
            void (*deleter)(void*) = nullptr;
        };

        inline std::unordered_map<uint64_t, StateBlock>& StateMap()
        {
            static std::unordered_map<uint64_t, StateBlock> map;
            return map;
        }

        inline int NextTypeId()
        {
            static int counter = 0;
            return counter++;
        }

        template <typename T>
        int TypeId()
        {
            static const int id = NextTypeId();
            return id;
        }
    }

    // Persistent animation state for a widget. The type is folded into the
    // key so two different widgets can never reinterpret each other's block.
    template <typename T>
    T* State(ImGuiID id)
    {
        const uint64_t key = (static_cast<uint64_t>(Detail::TypeId<T>()) << 32) | static_cast<uint64_t>(id);
        auto& map = Detail::StateMap();
        auto it = map.find(key);
        if (it != map.end())
            return static_cast<T*>(it->second.ptr);

        T* fresh = new T();
        map[key] = Detail::StateBlock{ fresh, [](void* p) { delete static_cast<T*>(p); } };
        return fresh;
    }

    void ReleaseAnimationState();

    // ---------------------------------------------------------------- theme

    struct Palette
    {
        ImU32 background;
        ImU32 sidebar;
        ImU32 panel;
        ImU32 card;
        ImU32 cardHover;
        ImU32 widget;
        ImU32 widgetHover;
        ImU32 border;
        ImU32 line;
        ImU32 text;
        ImU32 textDim;
        ImU32 textMute;
        ImU32 accent;
        ImU32 accentSoft;
        ImU32 onAccent;
        ImU32 good;
        ImU32 warning;
        ImU32 danger;
    };

    Palette& Colors();

    // Accent is user editable (settings page / colour picker).
    ImVec4& Accent();
    void    RefreshAccent();

    // Everything the GUI wants to survive a restart. Owned here so the
    // profile serialiser can read and write it without touching layout code.
    struct Preferences
    {
        float accent[3] = { 103.0f / 255.0f, 100.0f / 255.0f, 255.0f / 255.0f };
        bool  reduceMotion = false;
        bool  showDescriptions = true;
        bool  notifyOnToggle = true;

        // Comma separated list of favourited module names (pinned to the top
        // of the module list).
        std::string favoritesCsv;

        // Notification toasts (formerly the Notifications module).
        bool  notificationsEnabled = true;
        bool  notifyModuleToggles = true;
        bool  notifyProfileConfigs = true;
        bool  notifyFriendToggles = true;
        bool  notifyPlaySounds = true;

        // 0 = classic shell, 1 = dropdown columns.
        int   menuLayout = 0;
    };

    Preferences& Prefs();

    // Pushes Prefs().accent into the live palette.
    void ApplyPreferences();

    // Whole-menu fade. Every palette entry is rebuilt from the base colours
    // with its alpha scaled, so the open/close animation reaches everything
    // that draws through Colors().
    void  SetGlobalOpacity(float opacity);
    float GlobalOpacity();

    struct AccentPreset
    {
        const char* name;
        float r, g, b;
    };

    const AccentPreset* AccentPresets(int& count);

    // Multiply an existing packed colour by an alpha factor.
    inline ImU32 Alpha(ImU32 color, float alpha)
    {
        const float a = Clamp01(alpha) * static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF);
        return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
    }

    // Fades a hard coded colour with the current menu opacity.
    inline ImU32 Fade(ImU32 color)
    {
        return Alpha(color, GlobalOpacity());
    }

    inline ImU32 Mix(ImU32 a, ImU32 b, float t)
    {
        t = Clamp01(t);
        auto channel = [&](int shift) {
            const float ca = static_cast<float>((a >> shift) & 0xFF);
            const float cb = static_cast<float>((b >> shift) & 0xFF);
            return static_cast<ImU32>(ca + (cb - ca) * t) & 0xFFu;
        };
        return (channel(IM_COL32_R_SHIFT) << IM_COL32_R_SHIFT)
             | (channel(IM_COL32_G_SHIFT) << IM_COL32_G_SHIFT)
             | (channel(IM_COL32_B_SHIFT) << IM_COL32_B_SHIFT)
             | (channel(IM_COL32_A_SHIFT) << IM_COL32_A_SHIFT);
    }

    void ApplyImGuiStyle();

    // ---------------------------------------------------------------- fonts

    ImFont* FontRegular();
    ImFont* FontBold();
    ImFont* FontMono();
    ImFont* FontIcon();

    ImVec2 TextSize(ImFont* font, float size, const char* text);
    void   Text(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 color, const char* text);

    // Draws text aligned inside a box. align (0,0)=top-left, (0.5,0.5)=centre.
    void TextIn(ImDrawList* dl, ImFont* font, float size, ImVec2 min, ImVec2 max,
                ImU32 color, const char* text, ImVec2 align = ImVec2(0.0f, 0.5f));

    // Draws text clipped to a maximum width, appending an ellipsis.
    void TextEllipsis(ImDrawList* dl, ImFont* font, float size, ImVec2 pos,
                      float maxWidth, ImU32 color, const char* text);

    // ---------------------------------------------------------------- icons

    enum class Icon
    {
        None = 0,
        Home,
        Star,
        Sword,
        Run,
        Eye,
        Wrench,
        Grid,
        Gear,
        Search,
        ChevronDown,
        ChevronRight,
        Check,
        Close,
        Key,
        Palette,
        Sliders,
        Bolt,
        Shield,
        Cube,
        Dots,
        Plus,
        Minus,
        Refresh,
        Info,
        Warning,
        Power,
        Pin,

        // Module specific
        Crosshair,      // aim assist
        Target,         // target hud
        Hitbox,         // hitbox
        Arrow,          // velocity / knockback
        Block,          // autoblock / blocking
        Bot,            // antibot
        Rewind,         // backtrack
        Pierce,         // piercing
        Reach,          // reach
        Boot,           // sprint / movement
        Inventory,      // inv move
        Footsteps,      // safewalk
        Sun,            // fullbright
        Tag,            // nametags
        Line,           // tracers
        List,           // arraylist
        Heart,          // health display
        Camera,         // camera clip / no hurt cam
        Flask,          // anti debuff
        Curve,          // trajectories
        Gem,            // item esp
        Mouse,          // autoclicker
        Timer,          // delays
        Pickaxe,        // fast mine
        Hand,           // fast place / no interact
        Chest,          // chest stealer / refill
        Armor,          // auto armor
        Lock,           // item lock
        Note,           // item logger
        Bell,           // notifications
        Users,          // friends
        Folder,         // configs
        Terminal,       // macros
        UserCheck,      // fake login
        Window,         // clickgui

        // Distinct glyphs for modules that previously shared a generic icon.
        Skull,          // damage tags
        ArrowDown,      // fall view
        EyeSlash,       // blink
        Hourglass,      // fake lag
        Gauge,          // latency alerts
        Video,          // freelook
        Screwdriver,    // auto tool
    };

    // filled=false → outline/line (passive), filled=true → solid (active/selected)
    void DrawIcon(ImDrawList* dl, Icon icon, ImVec2 center, float size, ImU32 color,
                  float thickness = 2.0f, bool filled = false);

    // Maps a module by name (falling back to its category) to a dedicated icon.
    Icon IconForModule(const char* moduleName, int category);

    // Font Awesome 6 solid codepoint for an icon, or 0 when unavailable.
    ImU32 IconCodepoint(Icon icon);

    // ------------------------------------------------------------- tooltip

    // Replaces ImGui::SetTooltip: records a styled tooltip for the current
    // frame, drawn on top by DrawTooltip().
    void SetTooltip(const char* fmt, ...);
    void DrawTooltip();
}
