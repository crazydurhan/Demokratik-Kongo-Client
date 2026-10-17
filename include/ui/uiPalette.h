#pragma once

// The one colour palette both binaries draw from. The launcher uses these
// values directly; the ClickGUI seeds its runtime (accent-editable, opacity-
// faded) palette from them, so the launcher and the in-game menu always agree
// on the same ArifClient-style flat dark look.

#include "imgui.h"

namespace dk::ui::palette {

// Surfaces, darkest to lightest (ArifClient flat palette).
constexpr ImU32 kBackground  = IM_COL32(15,  15,  18,  255);  // #0F0F12
constexpr ImU32 kSidebar     = IM_COL32(15,  15,  18,  255);  // same as bg (flat)
constexpr ImU32 kPanel       = IM_COL32(23,  23,  28,  255);  // #17171C
constexpr ImU32 kPanelSoft   = IM_COL32(31,  31,  38,  255);  // #1F1F26 (hover)
constexpr ImU32 kCard        = IM_COL32(27,  27,  33,  255);  // #1B1B21
constexpr ImU32 kCardHover   = IM_COL32(32,  32,  39,  255);  // #202027
constexpr ImU32 kWidget      = IM_COL32(31,  31,  38,  255);  // #1F1F26 (same as panelSoft)
constexpr ImU32 kWidgetHover = IM_COL32(38,  38,  51,  255);
constexpr ImU32 kBorder      = IM_COL32(38,  38,  44,  255);  // #26262C
constexpr ImU32 kLine        = IM_COL32(38,  38,  44,  255);  // same as border

// Text.
constexpr ImU32 kText     = IM_COL32(255, 255, 255, 255);  // #FFFFFF
constexpr ImU32 kTextDim  = IM_COL32(138, 138, 147, 255);  // #8A8A93
constexpr ImU32 kTextMute = IM_COL32(107, 107, 116, 255);  // #6B6B74

// Accent (Sky blue) and semantic colours.
constexpr float kAccentR = 47.0f  / 255.0f;
constexpr float kAccentG = 128.0f / 255.0f;
constexpr float kAccentB = 237.0f / 255.0f;

constexpr ImU32 kAccent     = IM_COL32(47,  128, 237, 255);  // #2F80ED
constexpr ImU32 kAccent2    = IM_COL32(47,  128, 237, 255);  // flat: accent2 == accent
constexpr ImU32 kAccentSoft = IM_COL32(47,  128, 237, 46);
constexpr ImU32 kAccentGlow = IM_COL32(74,  150, 244, 255);
constexpr ImU32 kOnAccent   = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kGood       = IM_COL32(52,  211, 153, 255);  // #34D399
constexpr ImU32 kWarning    = IM_COL32(251, 191, 36,  255);  // #FBBF24
constexpr ImU32 kDanger     = IM_COL32(248, 113, 113, 255);  // #F87171

// Multiply an existing packed colour by an alpha factor (0..1).
inline ImU32 Alpha(ImU32 color, float alpha)
{
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    const float a = alpha * static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF);
    return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
}

// Linear blend of two packed colours, t clamped to 0..1.
inline ImU32 Mix(ImU32 a, ImU32 b, float t)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
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

inline ImVec4 ToVec4(ImU32 color) { return ImGui::ColorConvertU32ToFloat4(color); }

} // namespace dk::ui::palette
