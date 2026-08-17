#include "guiCore.h"

#include "../menu/menu.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace Gui
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;

        Palette g_palette = {
            IM_COL32(13,  13,  18,  255),   // background
            IM_COL32(17,  17,  23,  255),   // sidebar
            IM_COL32(21,  21,  28,  255),   // panel
            IM_COL32(27,  27,  36,  255),   // card
            IM_COL32(34,  34,  45,  255),   // cardHover
            IM_COL32(30,  30,  41,  255),   // widget
            IM_COL32(38,  38,  51,  255),   // widgetHover
            IM_COL32(42,  42,  55,  255),   // border
            IM_COL32(36,  36,  47,  255),   // line
            IM_COL32(237, 237, 244, 255),   // text
            IM_COL32(150, 149, 170, 255),   // textDim
            IM_COL32(100, 99,  122, 255),   // textMute
            IM_COL32(103, 100, 255, 255),   // accent
            IM_COL32(103, 100, 255, 46),    // accentSoft
            IM_COL32(255, 255, 255, 255),   // onAccent
            IM_COL32(76,  207, 132, 255),   // good
            IM_COL32(240, 178, 74,  255),   // warning
            IM_COL32(243, 101, 109, 255),   // danger
        };

        ImVec4 g_accent(103.0f / 255.0f, 100.0f / 255.0f, 255.0f / 255.0f, 1.0f);

        Palette g_base = g_palette;
        float g_opacity = 1.0f;

        void ApplyOpacity(Palette& dst, const Palette& src, float o)
        {
            dst.background  = Alpha(src.background,  o);
            dst.sidebar     = Alpha(src.sidebar,     o);
            dst.panel       = Alpha(src.panel,       o);
            dst.card        = Alpha(src.card,        o);
            dst.cardHover   = Alpha(src.cardHover,   o);
            dst.widget      = Alpha(src.widget,      o);
            dst.widgetHover = Alpha(src.widgetHover, o);
            dst.border      = Alpha(src.border,      o);
            dst.line        = Alpha(src.line,        o);
            dst.text        = Alpha(src.text,        o);
            dst.textDim     = Alpha(src.textDim,     o);
            dst.textMute    = Alpha(src.textMute,    o);
            dst.accent      = Alpha(src.accent,      o);
            dst.accentSoft  = Alpha(src.accentSoft,  o);
            dst.onAccent    = Alpha(src.onAccent,    o);
            dst.good        = Alpha(src.good,        o);
            dst.warning     = Alpha(src.warning,     o);
            dst.danger      = Alpha(src.danger,      o);
        }

        const AccentPreset g_presets[] = {
            { "Indigo",  0.404f, 0.392f, 1.000f },
            { "Violet",  0.667f, 0.416f, 1.000f },
            { "Azure",   0.180f, 0.541f, 0.984f },
            { "Cyan",    0.180f, 0.780f, 0.851f },
            { "Emerald", 0.235f, 0.788f, 0.510f },
            { "Amber",   0.976f, 0.686f, 0.243f },
            { "Rose",    0.965f, 0.365f, 0.451f },
            { "Crimson", 0.878f, 0.204f, 0.318f },
        };

        ImFont* Fallback(ImFont* font)
        {
            if (font && font->IsLoaded())
                return font;
            return ImGui::GetFont();
        }
    }

    void ReleaseAnimationState()
    {
        auto& map = Detail::StateMap();
        for (auto& entry : map)
        {
            if (entry.second.ptr && entry.second.deleter)
                entry.second.deleter(entry.second.ptr);
        }
        map.clear();
    }

    Palette& Colors() { return g_palette; }

    ImVec4& Accent() { return g_accent; }

    void RefreshAccent()
    {
        g_accent.x = Clamp01(g_accent.x);
        g_accent.y = Clamp01(g_accent.y);
        g_accent.z = Clamp01(g_accent.z);
        g_accent.w = 1.0f;

        g_base.accent = ImGui::ColorConvertFloat4ToU32(g_accent);
        g_base.accentSoft = Alpha(g_base.accent, 0.18f);

        // Pick black or white text on top of the accent based on luminance so
        // bright accents (amber / cyan) stay readable.
        const float luma = 0.299f * g_accent.x + 0.587f * g_accent.y + 0.114f * g_accent.z;
        g_base.onAccent = luma > 0.62f ? IM_COL32(16, 16, 22, 255) : IM_COL32(255, 255, 255, 255);

        ApplyOpacity(g_palette, g_base, g_opacity);
    }

    void SetGlobalOpacity(float opacity)
    {
        g_opacity = Clamp01(opacity);
        ApplyOpacity(g_palette, g_base, g_opacity);
    }

    float GlobalOpacity()
    {
        return g_opacity;
    }

    const AccentPreset* AccentPresets(int& count)
    {
        count = static_cast<int>(sizeof(g_presets) / sizeof(g_presets[0]));
        return g_presets;
    }

    Preferences& Prefs()
    {
        static Preferences prefs;
        return prefs;
    }

    void ApplyPreferences()
    {
        Preferences& prefs = Prefs();
        g_accent = ImVec4(prefs.accent[0], prefs.accent[1], prefs.accent[2], 1.0f);
        RefreshAccent();
    }

    void ApplyImGuiStyle()
    {
        RefreshAccent();

        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 14.0f;
        style.ChildRounding = 12.0f;
        style.FrameRounding = 8.0f;
        style.PopupRounding = 12.0f;
        style.ScrollbarRounding = 10.0f;
        style.GrabRounding = 8.0f;
        style.WindowBorderSize = 0.0f;
        style.ChildBorderSize = 0.0f;
        style.PopupBorderSize = 0.0f;
        style.FrameBorderSize = 0.0f;
        style.WindowPadding = ImVec2(0.0f, 0.0f);
        style.FramePadding = ImVec2(10.0f, 7.0f);
        style.ItemSpacing = ImVec2(8.0f, 6.0f);
        style.ItemInnerSpacing = ImVec2(6.0f, 6.0f);
        style.ScrollbarSize = 6.0f;
        style.WindowMenuButtonPosition = ImGuiDir_None;

        auto toVec4 = [](ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); };

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text] = toVec4(g_palette.text);
        colors[ImGuiCol_TextDisabled] = toVec4(g_palette.textMute);
        colors[ImGuiCol_WindowBg] = toVec4(g_palette.background);
        colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
        colors[ImGuiCol_PopupBg] = toVec4(g_palette.panel);
        colors[ImGuiCol_Border] = toVec4(g_palette.border);
        colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
        colors[ImGuiCol_FrameBg] = toVec4(g_palette.widget);
        colors[ImGuiCol_FrameBgHovered] = toVec4(g_palette.widgetHover);
        colors[ImGuiCol_FrameBgActive] = toVec4(g_palette.widgetHover);
        colors[ImGuiCol_TitleBg] = toVec4(g_palette.sidebar);
        colors[ImGuiCol_TitleBgActive] = toVec4(g_palette.sidebar);
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
        colors[ImGuiCol_ScrollbarGrab] = toVec4(Alpha(g_palette.textMute, 0.45f));
        colors[ImGuiCol_ScrollbarGrabHovered] = toVec4(Alpha(g_palette.textDim, 0.65f));
        colors[ImGuiCol_ScrollbarGrabActive] = toVec4(g_palette.accent);
        colors[ImGuiCol_CheckMark] = toVec4(g_palette.accent);
        colors[ImGuiCol_SliderGrab] = toVec4(g_palette.accent);
        colors[ImGuiCol_SliderGrabActive] = toVec4(g_palette.accent);
        colors[ImGuiCol_Button] = toVec4(g_palette.widget);
        colors[ImGuiCol_ButtonHovered] = toVec4(g_palette.widgetHover);
        colors[ImGuiCol_ButtonActive] = toVec4(g_palette.accent);
        colors[ImGuiCol_Header] = toVec4(g_palette.widget);
        colors[ImGuiCol_HeaderHovered] = toVec4(g_palette.widgetHover);
        colors[ImGuiCol_HeaderActive] = toVec4(g_palette.accent);
        colors[ImGuiCol_Separator] = toVec4(g_palette.line);
        colors[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
        colors[ImGuiCol_NavHighlight] = ImVec4(0, 0, 0, 0);
    }

    // ---------------------------------------------------------------- fonts

    ImFont* FontRegular() { return Fallback(Menu::Font); }
    ImFont* FontBold() { return Fallback(Menu::FontBold ? Menu::FontBold : Menu::Font); }
    ImFont* FontMono() { return Fallback(Menu::FontMono ? Menu::FontMono : Menu::Font); }

    ImFont* FontIcon()
    {
        // Unlike the text fonts we never fall back to the default font: an
        // icon codepoint drawn with Inter would be a missing-glyph box. Return
        // null so callers can fall through to the PNG / vector icon paths.
        return (Menu::FontIcon && Menu::FontIcon->IsLoaded()) ? Menu::FontIcon : nullptr;
    }

    ImVec2 TextSize(ImFont* font, float size, const char* text)
    {
        if (!text || !*text)
            return ImVec2(0.0f, 0.0f);
        font = Fallback(font);
        return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    }

    void Text(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 color, const char* text)
    {
        if (!dl || !text || !*text || ((color >> IM_COL32_A_SHIFT) & 0xFF) == 0)
            return;
        dl->AddText(Fallback(font), size, pos, color, text);
    }

    void TextIn(ImDrawList* dl, ImFont* font, float size, ImVec2 min, ImVec2 max,
                ImU32 color, const char* text, ImVec2 align)
    {
        if (!dl || !text || !*text)
            return;

        const ImVec2 bounds = TextSize(font, size, text);
        const ImVec2 pos(min.x + (max.x - min.x - bounds.x) * align.x,
                         min.y + (max.y - min.y - bounds.y) * align.y);
        Text(dl, font, size, ImVec2(std::floor(pos.x), std::floor(pos.y)), color, text);
    }

    void TextEllipsis(ImDrawList* dl, ImFont* font, float size, ImVec2 pos,
                      float maxWidth, ImU32 color, const char* text)
    {
        if (!dl || !text || !*text || maxWidth <= 1.0f)
            return;

        font = Fallback(font);
        if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x <= maxWidth)
        {
            Text(dl, font, size, pos, color, text);
            return;
        }

        const float dots = font->CalcTextSizeA(size, FLT_MAX, 0.0f, "...").x;
        std::string clipped(text);
        while (!clipped.empty() &&
               font->CalcTextSizeA(size, FLT_MAX, 0.0f, clipped.c_str()).x + dots > maxWidth)
        {
            clipped.pop_back();
        }
        clipped += "...";
        Text(dl, font, size, pos, color, clipped.c_str());
    }

    // ---------------------------------------------------------------- icons

    ImU32 IconCodepoint(Icon icon)
    {
        // Font Awesome 6 Free (solid) codepoints, subset into fa_solid.h.
        switch (icon)
        {
        case Icon::Home:        return 0xF015; // house
        case Icon::Star:        return 0xF005;
        case Icon::Sword:       return 0xF66D; // khanda (combat)
        case Icon::Run:         return 0xF70C; // person-running
        case Icon::Eye:         return 0xF06E;
        case Icon::Wrench:      return 0xF0AD;
        case Icon::Grid:        return 0xF84C; // border-all
        case Icon::Gear:        return 0xF013;
        case Icon::Search:      return 0xF002; // magnifying-glass
        case Icon::ChevronDown: return 0xF078;
        case Icon::ChevronRight:return 0xF054;
        case Icon::Check:       return 0xF00C;
        case Icon::Close:       return 0xF00D; // xmark
        case Icon::Key:         return 0xF084;
        case Icon::Palette:     return 0xF53F;
        case Icon::Sliders:     return 0xF1DE;
        case Icon::Bolt:        return 0xF0E7;
        case Icon::Shield:      return 0xF132;
        case Icon::Cube:        return 0xF1B2;
        case Icon::Dots:        return 0xF142; // ellipsis-vertical
        case Icon::Plus:        return 0xF067;
        case Icon::Minus:       return 0xF068;
        case Icon::Refresh:     return 0xF2F1; // rotate
        case Icon::Info:        return 0xF05A; // circle-info
        case Icon::Warning:     return 0xF071; // triangle-exclamation
        case Icon::Power:       return 0xF011; // power-off
        case Icon::Pin:         return 0xF08D; // thumbtack

        case Icon::Crosshair:   return 0xF05B;
        case Icon::Target:      return 0xF140; // bullseye
        case Icon::Hitbox:      return 0xF0C8; // square
        case Icon::Arrow:       return 0xF062; // arrow-up (velocity)
        case Icon::Block:       return 0xF05E; // ban (auto block)
        case Icon::Bot:         return 0xF544; // robot
        case Icon::Rewind:      return 0xF04A; // backward
        case Icon::Pierce:      return 0xF061; // arrow-right
        case Icon::Reach:       return 0xF545; // ruler (distance)
        case Icon::Boot:        return 0xF70C; // person-running (sprint)
        case Icon::Inventory:   return 0xF466; // box
        case Icon::Footsteps:   return 0xF54B; // shoe-prints
        case Icon::Sun:         return 0xF185;
        case Icon::Tag:         return 0xF02B;
        case Icon::Line:        return 0xF124; // location-arrow (tracers)
        case Icon::List:        return 0xF03A;
        case Icon::Heart:       return 0xF004;
        case Icon::Camera:      return 0xF030;
        case Icon::Flask:       return 0xF0C3;
        case Icon::Curve:       return 0xF201; // chart-line (trajectories)
        case Icon::Gem:         return 0xF3A5;
        case Icon::Mouse:       return 0xF8CC; // computer-mouse
        case Icon::Timer:       return 0xF017; // clock
        case Icon::Pickaxe:     return 0xF6E3; // hammer (fast mine)
        case Icon::Hand:        return 0xF256; // hand (no interact)
        case Icon::Chest:       return 0xF49E; // box-open
        case Icon::Armor:       return 0xF132; // shield (auto armor)
        case Icon::Lock:        return 0xF023;
        case Icon::Note:        return 0xF249; // note-sticky
        case Icon::Bell:        return 0xF0F3;
        case Icon::Users:       return 0xF0C0;
        case Icon::Folder:      return 0xF07B;
        case Icon::Terminal:    return 0xF120;
        case Icon::UserCheck:   return 0xF4FC;
        case Icon::Window:      return 0xF84C; // border-all (clickgui)

        case Icon::Skull:       return 0xF54C;
        case Icon::ArrowDown:   return 0xF063;
        case Icon::EyeSlash:    return 0xF070;
        case Icon::Hourglass:   return 0xF254;
        case Icon::Gauge:       return 0xF624;
        case Icon::Video:       return 0xF03D;
        case Icon::Screwdriver: return 0xF7D9; // screwdriver-wrench

        case Icon::None:
        default:                return 0;
        }
    }

    void DrawIcon(ImDrawList* dl, Icon icon, ImVec2 c, float size, ImU32 col, float th, bool filled)
    {
        if (!dl || icon == Icon::None || ((col >> IM_COL32_A_SHIFT) & 0xFF) == 0)
            return;

        // Vector icon font first: crisp at any size, tintable, single texture.
        ImFont* ifont = FontIcon();
        if (ifont)
        {
            const ImU32 cp = IconCodepoint(icon);
            if (cp)
            {
                char glyph[8];
                int n = 0;
                if (cp < 0x80)          glyph[n++] = static_cast<char>(cp);
                else if (cp < 0x800)    { glyph[n++] = static_cast<char>(0xC0 | (cp >> 6)); glyph[n++] = static_cast<char>(0x80 | (cp & 0x3F)); }
                else                    { glyph[n++] = static_cast<char>(0xE0 | (cp >> 12)); glyph[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); glyph[n++] = static_cast<char>(0x80 | (cp & 0x3F)); }
                glyph[n] = 0;

                const ImVec2 tsize = ifont->CalcTextSizeA(size, FLT_MAX, 0.0f, glyph);
                dl->AddText(ifont, size,
                            ImVec2(std::floor(c.x - tsize.x * 0.5f), std::floor(c.y - tsize.y * 0.5f)),
                            col, glyph);
                return;
            }
        }

        const float r = size * 0.5f;
        if (r <= 0.5f)
            return;

        auto line = [&](float x0, float y0, float x1, float y1) {
            dl->AddLine(ImVec2(c.x + x0 * r, c.y + y0 * r),
                        ImVec2(c.x + x1 * r, c.y + y1 * r), col, th);
        };
        auto arc = [&](float cx, float cy, float rad, float a0, float a1, int seg = 20) {
            dl->PathArcTo(ImVec2(c.x + cx * r, c.y + cy * r), rad * r, a0, a1, seg);
            dl->PathStroke(col, 0, th);
        };
        auto dot = [&](float x, float y, float rad) {
            dl->AddCircleFilled(ImVec2(c.x + x * r, c.y + y * r), rad * r, col, 12);
        };

        switch (icon)
        {
        case Icon::Home:
        {
            // Icons8 ios7/ios11 "home" metaphor: pitched roof + body + door.
            ImVec2 roof[3] = {
                ImVec2(c.x, c.y - r * 0.92f),
                ImVec2(c.x - r * 0.92f, c.y - r * 0.08f),
                ImVec2(c.x + r * 0.92f, c.y - r * 0.08f),
            };
            if (filled)
            {
                dl->AddTriangleFilled(roof[0], roof[1], roof[2], col);
                dl->AddRectFilled(ImVec2(c.x - r * 0.62f, c.y - r * 0.08f),
                                  ImVec2(c.x + r * 0.62f, c.y + r * 0.90f), col, 2.0f);
                dl->AddRectFilled(ImVec2(c.x - r * 0.16f, c.y + r * 0.28f),
                                  ImVec2(c.x + r * 0.16f, c.y + r * 0.90f),
                                  IM_COL32(0, 0, 0, (col >> IM_COL32_A_SHIFT) & 0xFF), 1.0f);
            }
            else
            {
                dl->AddPolyline(roof, 3, col, 0, th);
                dl->AddRect(ImVec2(c.x - r * 0.62f, c.y - r * 0.08f),
                            ImVec2(c.x + r * 0.62f, c.y + r * 0.90f), col, 2.0f, 0, th);
                dl->AddRect(ImVec2(c.x - r * 0.16f, c.y + r * 0.28f),
                            ImVec2(c.x + r * 0.16f, c.y + r * 0.90f), col, 1.0f, 0, th);
            }
            break;
        }
        case Icon::Star:
        {
            ImVec2 pts[10];
            for (int i = 0; i < 10; ++i)
            {
                const float ang = -kPi * 0.5f + static_cast<float>(i) * kPi / 5.0f;
                const float rad = (i & 1) ? r * 0.42f : r * 0.98f;
                pts[i] = ImVec2(c.x + std::cos(ang) * rad, c.y + std::sin(ang) * rad);
            }
            dl->AddPolyline(pts, 10, col, ImDrawFlags_Closed, th);
            break;
        }
        case Icon::Sword:
            // Blade (two edges + point), crossguard, grip and pommel.
            line(-0.16f, -0.52f, -0.16f, 0.26f);
            line(0.16f, -0.52f, 0.16f, 0.26f);
            line(-0.16f, -0.52f, 0.0f, -0.94f);
            line(0.16f, -0.52f, 0.0f, -0.94f);
            line(-0.58f, 0.30f, 0.58f, 0.30f);
            line(0.0f, 0.34f, 0.0f, 0.74f);
            dot(0.0f, 0.85f, 0.14f);
            break;
        case Icon::Run:
            line(-0.90f, -0.45f, 0.35f, -0.45f);
            line(-0.55f, 0.00f, 0.90f, 0.00f);
            line(-0.90f, 0.45f, 0.55f, 0.45f);
            line(0.10f, -0.72f, 0.42f, -0.45f);
            line(0.10f, -0.18f, 0.42f, -0.45f);
            break;
        case Icon::Eye:
            arc(0.0f, 0.62f, 0.98f, -kPi * 0.82f, -kPi * 0.18f, 22);
            arc(0.0f, -0.62f, 0.98f, kPi * 0.18f, kPi * 0.82f, 22);
            dl->AddCircle(c, r * 0.30f, col, 16, th);
            break;
        case Icon::Wrench:
            arc(0.34f, -0.34f, 0.56f, kPi * 0.20f, kPi * 1.62f, 20);
            line(0.02f, 0.02f, -0.72f, 0.76f);
            line(-0.72f, 0.76f, -0.86f, 0.52f);
            break;
        case Icon::Grid:
        {
            const float s = r * 0.36f;
            const float o = r * 0.44f;
            for (int i = 0; i < 4; ++i)
            {
                const float x = c.x + ((i & 1) ? o : -o);
                const float y = c.y + ((i & 2) ? o : -o);
                dl->AddRect(ImVec2(x - s, y - s), ImVec2(x + s, y + s), col, s * 0.5f, 0, th);
            }
            break;
        }
        case Icon::Gear:
            if (filled)
            {
                dl->AddCircleFilled(c, r * 0.72f, col, 24);
                dl->AddCircleFilled(c, r * 0.28f, IM_COL32(0, 0, 0, (col >> IM_COL32_A_SHIFT) & 0xFF), 16);
                for (int i = 0; i < 8; ++i)
                {
                    const float a = static_cast<float>(i) * kPi / 4.0f;
                    const float dx = std::cos(a), dy = std::sin(a);
                    dl->AddLine(ImVec2(c.x + dx * r * 0.58f, c.y + dy * r * 0.58f),
                                ImVec2(c.x + dx * r * 0.99f, c.y + dy * r * 0.99f), col, th * 2.2f);
                }
            }
            else
            {
                dl->AddCircle(c, r * 0.30f, col, 16, th);
                dl->AddCircle(c, r * 0.70f, col, 24, th);
                for (int i = 0; i < 8; ++i)
                {
                    const float a = static_cast<float>(i) * kPi / 4.0f;
                    const float dx = std::cos(a), dy = std::sin(a);
                    dl->AddLine(ImVec2(c.x + dx * r * 0.70f, c.y + dy * r * 0.70f),
                                ImVec2(c.x + dx * r * 0.99f, c.y + dy * r * 0.99f), col, th * 1.35f);
                }
            }
            break;
        case Icon::Search:
            dl->AddCircle(ImVec2(c.x - r * 0.18f, c.y - r * 0.18f), r * 0.58f, col, 20, th);
            line(0.26f, 0.26f, 0.86f, 0.86f);
            break;
        case Icon::ChevronDown:
            line(-0.55f, -0.26f, 0.0f, 0.30f);
            line(0.55f, -0.26f, 0.0f, 0.30f);
            break;
        case Icon::ChevronRight:
            line(-0.26f, -0.55f, 0.30f, 0.0f);
            line(-0.26f, 0.55f, 0.30f, 0.0f);
            break;
        case Icon::Check:
            line(-0.70f, 0.02f, -0.20f, 0.56f);
            line(-0.20f, 0.56f, 0.72f, -0.52f);
            break;
        case Icon::Close:
            line(-0.62f, -0.62f, 0.62f, 0.62f);
            line(-0.62f, 0.62f, 0.62f, -0.62f);
            break;
        case Icon::Key:
            dl->AddCircle(ImVec2(c.x - r * 0.42f, c.y - r * 0.10f), r * 0.42f, col, 18, th);
            line(-0.10f, 0.06f, 0.88f, 0.06f);
            line(0.56f, 0.06f, 0.56f, 0.52f);
            line(0.86f, 0.06f, 0.86f, 0.44f);
            break;
        case Icon::Palette:
            arc(0.0f, 0.0f, 0.92f, kPi * 0.10f, kPi * 1.72f, 26);
            dot(-0.40f, -0.34f, 0.13f);
            dot(0.10f, -0.52f, 0.13f);
            dot(0.50f, -0.16f, 0.13f);
            break;
        case Icon::Sliders:
            line(-0.88f, -0.50f, 0.88f, -0.50f);
            line(-0.88f, 0.10f, 0.88f, 0.10f);
            line(-0.88f, 0.70f, 0.88f, 0.70f);
            dl->AddCircleFilled(ImVec2(c.x - r * 0.30f, c.y - r * 0.50f), th * 1.5f, col, 12);
            dl->AddCircleFilled(ImVec2(c.x + r * 0.36f, c.y + r * 0.10f), th * 1.5f, col, 12);
            dl->AddCircleFilled(ImVec2(c.x - r * 0.10f, c.y + r * 0.70f), th * 1.5f, col, 12);
            break;
        case Icon::Bolt:
            line(0.22f, -0.92f, -0.42f, 0.06f);
            line(-0.42f, 0.06f, 0.06f, 0.06f);
            line(0.06f, 0.06f, -0.20f, 0.92f);
            line(-0.20f, 0.92f, 0.44f, -0.10f);
            line(0.44f, -0.10f, -0.02f, -0.10f);
            line(-0.02f, -0.10f, 0.22f, -0.92f);
            break;
        case Icon::Shield:
            line(0.0f, -0.88f, -0.74f, -0.52f);
            line(0.0f, -0.88f, 0.74f, -0.52f);
            line(-0.74f, -0.52f, -0.74f, 0.10f);
            line(0.74f, -0.52f, 0.74f, 0.10f);
            arc(0.0f, 0.10f, 0.74f, 0.0f, kPi, 18);
            break;
        case Icon::Cube:
            line(0.0f, -0.92f, -0.80f, -0.46f);
            line(0.0f, -0.92f, 0.80f, -0.46f);
            line(-0.80f, -0.46f, -0.80f, 0.46f);
            line(0.80f, -0.46f, 0.80f, 0.46f);
            line(-0.80f, 0.46f, 0.0f, 0.92f);
            line(0.80f, 0.46f, 0.0f, 0.92f);
            line(0.0f, 0.0f, -0.80f, -0.46f);
            line(0.0f, 0.0f, 0.80f, -0.46f);
            line(0.0f, 0.0f, 0.0f, 0.92f);
            break;
        case Icon::Dots:
            dot(0.0f, -0.62f, 0.16f);
            dot(0.0f, 0.0f, 0.16f);
            dot(0.0f, 0.62f, 0.16f);
            break;
        case Icon::Plus:
            line(0.0f, -0.72f, 0.0f, 0.72f);
            line(-0.72f, 0.0f, 0.72f, 0.0f);
            break;
        case Icon::Minus:
            line(-0.72f, 0.0f, 0.72f, 0.0f);
            break;
        case Icon::Refresh:
            arc(0.0f, 0.0f, 0.74f, -kPi * 0.35f, kPi * 1.20f, 24);
            line(0.42f, -0.78f, 0.72f, -0.44f);
            line(0.72f, -0.44f, 0.34f, -0.24f);
            break;
        case Icon::Info:
            dl->AddCircle(c, r * 0.92f, col, 24, th);
            dot(0.0f, -0.42f, 0.11f);
            line(0.0f, -0.06f, 0.0f, 0.50f);
            break;
        case Icon::Warning:
            line(0.0f, -0.86f, -0.92f, 0.76f);
            line(0.0f, -0.86f, 0.92f, 0.76f);
            line(-0.92f, 0.76f, 0.92f, 0.76f);
            line(0.0f, -0.30f, 0.0f, 0.26f);
            dot(0.0f, 0.52f, 0.10f);
            break;
        case Icon::Power:
            arc(0.0f, 0.0f, 0.80f, -kPi * 0.32f, kPi * 1.32f, 24);
            line(0.0f, -0.92f, 0.0f, -0.12f);
            break;
        case Icon::Pin:
            dl->AddCircle(ImVec2(c.x, c.y - r * 0.28f), r * 0.46f, col, 18, th);
            line(0.0f, 0.18f, 0.0f, 0.92f);
            break;

        // ------------------------------------------------------ module icons
        case Icon::Crosshair:
            dl->AddCircle(c, r * 0.58f, col, 24, th);
            line(0.0f, -1.00f, 0.0f, -0.72f);
            line(0.0f, 1.00f, 0.0f, 0.72f);
            line(-1.00f, 0.0f, -0.72f, 0.0f);
            line(1.00f, 0.0f, 0.72f, 0.0f);
            dot(0.0f, 0.0f, 0.12f);
            break;
        case Icon::Target:
            dl->AddCircle(c, r * 0.95f, col, 26, th);
            dl->AddCircle(c, r * 0.52f, col, 20, th);
            dot(0.0f, 0.0f, 0.16f);
            break;
        case Icon::Hitbox:
            // Dashed selection box.
            for (int i = 0; i < 4; ++i)
            {
                const float s = (i & 1) ? 1.0f : -1.0f;
                if (i < 2)
                {
                    line(-0.88f, s * 0.88f, -0.34f, s * 0.88f);
                    line(0.34f, s * 0.88f, 0.88f, s * 0.88f);
                }
                else
                {
                    line(s * 0.88f, -0.88f, s * 0.88f, -0.34f);
                    line(s * 0.88f, 0.34f, s * 0.88f, 0.88f);
                }
            }
            break;
        case Icon::Arrow:
            line(-0.86f, 0.52f, 0.78f, -0.52f);
            line(0.78f, -0.52f, 0.30f, -0.52f);
            line(0.78f, -0.52f, 0.78f, -0.06f);
            break;
        case Icon::Block:
            line(0.0f, -0.90f, -0.72f, -0.52f);
            line(0.0f, -0.90f, 0.72f, -0.52f);
            line(-0.72f, -0.52f, -0.72f, 0.16f);
            line(0.72f, -0.52f, 0.72f, 0.16f);
            arc(0.0f, 0.16f, 0.72f, 0.0f, kPi, 18);
            line(-0.34f, -0.16f, 0.34f, -0.16f);
            break;
        case Icon::Bot:
            dl->AddRect(ImVec2(c.x - r * 0.74f, c.y - r * 0.42f),
                        ImVec2(c.x + r * 0.74f, c.y + r * 0.72f), col, r * 0.28f, 0, th);
            line(0.0f, -0.42f, 0.0f, -0.80f);
            dot(0.0f, -0.90f, 0.13f);
            dot(-0.34f, 0.06f, 0.13f);
            dot(0.34f, 0.06f, 0.13f);
            break;
        case Icon::Rewind:
            line(-0.10f, -0.62f, -0.86f, 0.0f);
            line(-0.86f, 0.0f, -0.10f, 0.62f);
            line(0.86f, -0.62f, 0.10f, 0.0f);
            line(0.10f, 0.0f, 0.86f, 0.62f);
            break;
        case Icon::Pierce:
            line(-0.92f, 0.0f, 0.92f, 0.0f);
            line(0.92f, 0.0f, 0.46f, -0.34f);
            line(0.92f, 0.0f, 0.46f, 0.34f);
            dl->AddCircle(ImVec2(c.x - r * 0.16f, c.y), r * 0.44f, col, 18, th);
            break;
        case Icon::Reach:
            line(-0.88f, 0.44f, 0.88f, -0.44f);
            line(-0.88f, 0.44f, -0.88f, 0.06f);
            line(-0.88f, 0.44f, -0.48f, 0.44f);
            line(0.88f, -0.44f, 0.88f, -0.06f);
            line(0.88f, -0.44f, 0.48f, -0.44f);
            break;
        case Icon::Boot:
            line(-0.62f, -0.86f, -0.62f, 0.42f);
            line(-0.62f, -0.86f, -0.16f, -0.86f);
            line(-0.16f, -0.86f, -0.16f, 0.06f);
            line(-0.16f, 0.06f, 0.74f, 0.42f);
            line(0.74f, 0.42f, 0.74f, 0.80f);
            line(-0.62f, 0.42f, -0.62f, 0.80f);
            line(-0.62f, 0.80f, 0.74f, 0.80f);
            break;
        case Icon::Inventory:
            dl->AddRect(ImVec2(c.x - r * 0.88f, c.y - r * 0.62f),
                        ImVec2(c.x + r * 0.88f, c.y + r * 0.62f), col, r * 0.18f, 0, th);
            line(-0.30f, -0.62f, -0.30f, 0.62f);
            line(0.30f, -0.62f, 0.30f, 0.62f);
            break;
        case Icon::Footsteps:
            dl->AddRect(ImVec2(c.x - r * 0.72f, c.y - r * 0.86f),
                        ImVec2(c.x - r * 0.12f, c.y + r * 0.10f), col, r * 0.30f, 0, th);
            dl->AddRect(ImVec2(c.x + r * 0.12f, c.y - r * 0.10f),
                        ImVec2(c.x + r * 0.72f, c.y + r * 0.86f), col, r * 0.30f, 0, th);
            break;
        case Icon::Sun:
            dl->AddCircle(c, r * 0.44f, col, 20, th);
            for (int i = 0; i < 8; ++i)
            {
                const float a = static_cast<float>(i) * kPi / 4.0f;
                const float dx = std::cos(a), dy = std::sin(a);
                dl->AddLine(ImVec2(c.x + dx * r * 0.66f, c.y + dy * r * 0.66f),
                            ImVec2(c.x + dx * r * 0.98f, c.y + dy * r * 0.98f), col, th);
            }
            break;
        case Icon::Tag:
            line(-0.86f, -0.16f, -0.16f, -0.86f);
            line(-0.16f, -0.86f, 0.86f, -0.86f);
            line(0.86f, -0.86f, 0.86f, 0.16f);
            line(0.86f, 0.16f, -0.16f, 0.86f);
            line(-0.16f, 0.86f, -0.86f, 0.16f);
            dot(0.42f, -0.42f, 0.14f);
            break;
        case Icon::Line:
            line(-0.90f, 0.78f, 0.90f, -0.78f);
            dot(-0.90f, 0.78f, 0.16f);
            dot(0.90f, -0.78f, 0.16f);
            break;
        case Icon::List:
            for (int i = 0; i < 3; ++i)
            {
                const float y = -0.60f + static_cast<float>(i) * 0.60f;
                dot(-0.76f, y, 0.11f);
                line(-0.40f, y, 0.86f, y);
            }
            break;
        case Icon::Heart:
            arc(-0.40f, -0.26f, 0.42f, kPi, kPi * 2.0f, 16);
            arc(0.40f, -0.26f, 0.42f, kPi, kPi * 2.0f, 16);
            line(-0.82f, -0.20f, 0.0f, 0.84f);
            line(0.82f, -0.20f, 0.0f, 0.84f);
            break;
        case Icon::Camera:
            dl->AddRect(ImVec2(c.x - r * 0.90f, c.y - r * 0.50f),
                        ImVec2(c.x + r * 0.90f, c.y + r * 0.66f), col, r * 0.20f, 0, th);
            dl->AddCircle(c, r * 0.32f, col, 18, th);
            line(-0.42f, -0.50f, -0.22f, -0.80f);
            line(-0.22f, -0.80f, 0.22f, -0.80f);
            line(0.22f, -0.80f, 0.42f, -0.50f);
            break;
        case Icon::Flask:
            line(-0.30f, -0.90f, -0.30f, -0.20f);
            line(0.30f, -0.90f, 0.30f, -0.20f);
            line(-0.30f, -0.90f, 0.30f, -0.90f);
            line(-0.30f, -0.20f, -0.80f, 0.72f);
            line(0.30f, -0.20f, 0.80f, 0.72f);
            arc(0.0f, 0.34f, 0.86f, kPi * 0.18f, kPi * 0.82f, 16);
            break;
        case Icon::Curve:
            dl->PathLineTo(ImVec2(c.x - r * 0.90f, c.y + r * 0.72f));
            dl->PathBezierCubicCurveTo(ImVec2(c.x - r * 0.30f, c.y - r * 1.10f),
                                       ImVec2(c.x + r * 0.30f, c.y - r * 1.10f),
                                       ImVec2(c.x + r * 0.90f, c.y + r * 0.72f), 24);
            dl->PathStroke(col, 0, th);
            break;
        case Icon::Gem:
            line(-0.86f, -0.28f, -0.50f, -0.86f);
            line(-0.50f, -0.86f, 0.50f, -0.86f);
            line(0.50f, -0.86f, 0.86f, -0.28f);
            line(0.86f, -0.28f, 0.0f, 0.88f);
            line(-0.86f, -0.28f, 0.0f, 0.88f);
            line(-0.86f, -0.28f, 0.86f, -0.28f);
            break;
        case Icon::Mouse:
            dl->AddRect(ImVec2(c.x - r * 0.56f, c.y - r * 0.92f),
                        ImVec2(c.x + r * 0.56f, c.y + r * 0.92f), col, r * 0.56f, 0, th);
            line(0.0f, -0.92f, 0.0f, -0.30f);
            break;
        case Icon::Timer:
            dl->AddCircle(c, r * 0.82f, col, 24, th);
            line(0.0f, -0.42f, 0.0f, 0.02f);
            line(0.0f, 0.02f, 0.40f, 0.30f);
            line(-0.30f, -1.00f, 0.30f, -1.00f);
            break;
        case Icon::Pickaxe:
            arc(0.0f, -0.36f, 0.86f, kPi * 1.10f, kPi * 1.90f, 20);
            line(0.0f, -0.30f, 0.0f, 0.90f);
            break;
        case Icon::Hand:
            line(-0.52f, 0.86f, -0.52f, -0.30f);
            line(-0.18f, 0.86f, -0.18f, -0.74f);
            line(0.18f, 0.86f, 0.18f, -0.86f);
            line(0.54f, 0.86f, 0.54f, -0.46f);
            arc(0.0f, 0.62f, 0.60f, 0.0f, kPi, 16);
            break;
        case Icon::Chest:
            dl->AddRect(ImVec2(c.x - r * 0.88f, c.y - r * 0.56f),
                        ImVec2(c.x + r * 0.88f, c.y + r * 0.72f), col, r * 0.16f, 0, th);
            line(-0.88f, -0.06f, 0.88f, -0.06f);
            dot(0.0f, 0.16f, 0.15f);
            break;
        case Icon::Armor:
            line(0.0f, -0.90f, -0.78f, -0.54f);
            line(0.0f, -0.90f, 0.78f, -0.54f);
            line(-0.78f, -0.54f, -0.78f, 0.14f);
            line(0.78f, -0.54f, 0.78f, 0.14f);
            arc(0.0f, 0.14f, 0.78f, 0.0f, kPi, 18);
            line(0.0f, -0.54f, 0.0f, 0.62f);
            break;
        case Icon::Lock:
            dl->AddRect(ImVec2(c.x - r * 0.68f, c.y - r * 0.10f),
                        ImVec2(c.x + r * 0.68f, c.y + r * 0.86f), col, r * 0.18f, 0, th);
            arc(0.0f, -0.10f, 0.44f, kPi, kPi * 2.0f, 18);
            break;
        case Icon::Note:
            dl->AddRect(ImVec2(c.x - r * 0.70f, c.y - r * 0.90f),
                        ImVec2(c.x + r * 0.70f, c.y + r * 0.90f), col, r * 0.16f, 0, th);
            line(-0.42f, -0.44f, 0.42f, -0.44f);
            line(-0.42f, 0.0f, 0.42f, 0.0f);
            line(-0.42f, 0.44f, 0.10f, 0.44f);
            break;
        case Icon::Bell:
            arc(0.0f, 0.10f, 0.66f, kPi, kPi * 2.0f, 18);
            line(-0.66f, 0.10f, -0.66f, 0.48f);
            line(0.66f, 0.10f, 0.66f, 0.48f);
            line(-0.86f, 0.48f, 0.86f, 0.48f);
            line(0.0f, -0.56f, 0.0f, -0.86f);
            arc(0.0f, 0.70f, 0.26f, 0.0f, kPi, 12);
            break;
        case Icon::Users:
            if (filled)
            {
                dl->AddCircleFilled(ImVec2(c.x - r * 0.30f, c.y - r * 0.38f), r * 0.36f, col, 18);
                dl->PathArcTo(ImVec2(c.x - r * 0.30f, c.y + r * 0.52f), r * 0.62f, kPi, kPi * 2.0f, 18);
                dl->PathFillConvex(col);
                dl->AddCircleFilled(ImVec2(c.x + r * 0.52f, c.y - r * 0.44f), r * 0.26f, col, 16);
            }
            else
            {
                dl->AddCircle(ImVec2(c.x - r * 0.30f, c.y - r * 0.38f), r * 0.36f, col, 18, th);
                arc(-0.30f, 0.52f, 0.62f, kPi, kPi * 2.0f, 18);
                dl->AddCircle(ImVec2(c.x + r * 0.52f, c.y - r * 0.44f), r * 0.26f, col, 16, th);
                arc(0.52f, 0.44f, 0.44f, kPi * 1.20f, kPi * 2.0f, 14);
            }
            break;
        case Icon::Folder:
            if (filled)
            {
                dl->AddRectFilled(ImVec2(c.x - r * 0.88f, c.y - r * 0.30f),
                                  ImVec2(c.x + r * 0.88f, c.y + r * 0.70f), col, r * 0.12f);
                dl->AddRectFilled(ImVec2(c.x - r * 0.88f, c.y - r * 0.62f),
                                  ImVec2(c.x - r * 0.02f, c.y - r * 0.18f), col, r * 0.10f);
            }
            else
            {
                line(-0.88f, 0.70f, -0.88f, -0.62f);
                line(-0.88f, -0.62f, -0.20f, -0.62f);
                line(-0.20f, -0.62f, -0.02f, -0.30f);
                line(-0.02f, -0.30f, 0.88f, -0.30f);
                line(0.88f, -0.30f, 0.88f, 0.70f);
                line(-0.88f, 0.70f, 0.88f, 0.70f);
            }
            break;
        case Icon::Terminal:
            dl->AddRect(ImVec2(c.x - r * 0.92f, c.y - r * 0.72f),
                        ImVec2(c.x + r * 0.92f, c.y + r * 0.72f), col, r * 0.18f, 0, th);
            line(-0.58f, -0.30f, -0.20f, 0.02f);
            line(-0.20f, 0.02f, -0.58f, 0.36f);
            line(0.06f, 0.36f, 0.58f, 0.36f);
            break;
        case Icon::UserCheck:
            dl->AddCircle(ImVec2(c.x - r * 0.26f, c.y - r * 0.40f), r * 0.38f, col, 18, th);
            arc(-0.26f, 0.52f, 0.64f, kPi, kPi * 2.0f, 18);
            line(0.36f, 0.20f, 0.60f, 0.46f);
            line(0.60f, 0.46f, 0.98f, -0.14f);
            break;
        case Icon::Window:
            dl->AddRect(ImVec2(c.x - r * 0.90f, c.y - r * 0.76f),
                        ImVec2(c.x + r * 0.90f, c.y + r * 0.76f), col, r * 0.18f, 0, th);
            line(-0.90f, -0.34f, 0.90f, -0.34f);
            dot(-0.58f, -0.56f, 0.10f);
            dot(-0.26f, -0.56f, 0.10f);
            break;

        default:
            break;
        }
    }

    Icon IconForModule(const char* moduleName, int category)
    {
        struct Entry { const char* name; Icon icon; };
        static const Entry kMap[] = {
            // Combat
            { "Aim Assist",       Icon::Crosshair },
            { "Reach",            Icon::Reach     },
            { "Hitbox",           Icon::Hitbox    },
            { "Velocity",         Icon::Arrow     },
            { "Auto Block",       Icon::Block     },
            { "AntiBot",          Icon::Bot       },
            { "Backtrack",        Icon::Rewind    },
            { "Piercing",         Icon::Pierce    },
            { "Knockback Delay",  Icon::Timer     },
            { "AutoClicker",      Icon::Mouse     },
            // Movement
            { "Sprint",           Icon::Boot      },
            { "InvMove",          Icon::Inventory },
            { "SafeWalk",         Icon::Footsteps },
            // Render / visual
            { "Fullbright",       Icon::Sun       },
            { "PlayerESP",        Icon::Cube      },
            { "NameTags",         Icon::Tag       },
            { "Tracers",          Icon::Line      },
            { "ArrayList",        Icon::List      },
            { "TargetHUD",        Icon::Target    },
            { "Health",           Icon::Heart     },
            { "NoHurtCam",        Icon::Camera    },
            { "CameraClip",       Icon::Camera    },
            { "Freelook",         Icon::Video     },
            { "FallView",         Icon::ArrowDown },
            { "DamageTags",       Icon::Skull     },
            { "Anti Debuff",      Icon::Flask     },
            { "Trajectories",     Icon::Curve     },
            { "ItemESP",          Icon::Gem       },
            { "LatencyAlerts",    Icon::Gauge     },
            { "Blink",            Icon::EyeSlash  },
            { "FakeLag",          Icon::Hourglass },
            { "Freecam",          Icon::Camera    },
            // Utility
            { "No Hit Delay",     Icon::Timer     },
            { "DelayRemover",     Icon::Timer     },
            { "FastPlace",        Icon::Hand      },
            { "Fast Mine",        Icon::Pickaxe   },
            { "NoInteract",       Icon::Hand      },
            { "ChestStealer",     Icon::Chest     },
            { "Refill",           Icon::Chest     },
            { "SafeAutoArmor",    Icon::Armor     },
            { "Auto Tool",        Icon::Screwdriver },
            { "Item Lock",        Icon::Lock      },
            { "Item Logger",      Icon::Note      },
            // Misc
            { "Macros",           Icon::Terminal  },
            { "FakeLogin",        Icon::UserCheck },
        };

        if (moduleName)
        {
            for (const Entry& entry : kMap)
            {
                if (std::strcmp(entry.name, moduleName) == 0)
                    return entry.icon;
            }
        }

        // Fall back to the category icon (matches the sidebar).
        switch (category)
        {
        case 0:  return Icon::Sword;    // Combat
        case 1:  return Icon::Run;      // Movement
        case 2:  return Icon::Eye;      // Render
        case 3:  return Icon::Wrench;   // Utility
        default: return Icon::Grid;     // Misc
        }
    }

    // -------------------------------------------------------------- tooltip

    namespace
    {
        std::string g_tooltipText;
        bool        g_tooltipDirty = false;
        float       g_tooltipAlpha = 0.0f;
    }

    void SetTooltip(const char* fmt, ...)
    {
        if (!fmt)
            return;
        char buf[512];
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        g_tooltipText = buf;
        g_tooltipDirty = true;
    }

    void DrawTooltip()
    {
        Ease(g_tooltipAlpha, g_tooltipDirty ? 1.0f : 0.0f, 22.0f);
        if (g_tooltipDirty)
            g_tooltipDirty = false;
        if (g_tooltipAlpha < 0.02f || g_tooltipText.empty())
            return;

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const float size = 12.0f;
        const ImVec2 pad(9.0f, 6.0f);
        const ImVec2 textSize = TextSize(FontRegular(), size, g_tooltipText.c_str());

        ImVec2 min(mouse.x + 14.0f, mouse.y + 20.0f);
        ImVec2 max(min.x + textSize.x + pad.x * 2.0f, min.y + textSize.y + pad.y * 2.0f);

        const ImVec2 screen = ImGui::GetIO().DisplaySize;
        if (max.x > screen.x - 8.0f)
            min.x = mouse.x - 14.0f - (max.x - min.x);
        if (max.y > screen.y - 8.0f)
            min.y = mouse.y - 20.0f - (max.y - min.y);
        max.x = min.x + textSize.x + pad.x * 2.0f;
        max.y = min.y + textSize.y + pad.y * 2.0f;

        const float a = Clamp01(g_tooltipAlpha);
        const Palette& c = Colors();
        dl->AddShadowRect(min, max, IM_COL32(0, 0, 0, static_cast<int>(90.0f * a)), 14.0f,
                          ImVec2(0.0f, 2.0f), ImDrawFlags_ShadowCutOutShapeBackground, 7.0f);
        dl->AddRectFilled(min, max, Alpha(c.panel, a), 7.0f);
        dl->AddRect(min, max, Alpha(c.border, a), 7.0f, 0, 1.0f);
        Text(dl, FontRegular(), size, ImVec2(min.x + pad.x, min.y + pad.y),
             Alpha(c.text, a), g_tooltipText.c_str());
    }
}
