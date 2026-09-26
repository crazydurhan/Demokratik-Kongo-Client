#pragma once

// The one colour palette both binaries draw from. The launcher uses these
// values directly; the ClickGUI seeds its runtime (accent-editable, opacity-
// faded) palette from them, so the launcher and the in-game menu always agree
// on the same dark indigo look.

#include "imgui.h"

namespace dk::ui::palette {

// Surfaces, darkest to lightest.
constexpr ImU32 kBackground  = IM_COL32(13,  13,  18,  255);
constexpr ImU32 kSidebar     = IM_COL32(17,  17,  23,  255);
constexpr ImU32 kPanel       = IM_COL32(21,  21,  28,  255);
constexpr ImU32 kCard        = IM_COL32(27,  27,  36,  255);
constexpr ImU32 kCardHover   = IM_COL32(34,  34,  45,  255);
constexpr ImU32 kWidget      = IM_COL32(30,  30,  41,  255);
constexpr ImU32 kWidgetHover = IM_COL32(38,  38,  51,  255);
constexpr ImU32 kBorder      = IM_COL32(42,  42,  55,  255);
constexpr ImU32 kLine        = IM_COL32(36,  36,  47,  255);

// Text.
constexpr ImU32 kText     = IM_COL32(237, 237, 244, 255);
constexpr ImU32 kTextDim  = IM_COL32(150, 149, 170, 255);
constexpr ImU32 kTextMute = IM_COL32(100, 99,  122, 255);

// Accent (indigo) and semantic colours.
constexpr float kAccentR = 103.0f / 255.0f;
constexpr float kAccentG = 100.0f / 255.0f;
constexpr float kAccentB = 255.0f / 255.0f;

constexpr ImU32 kAccent     = IM_COL32(103, 100, 255, 255);
constexpr ImU32 kAccentSoft = IM_COL32(103, 100, 255, 46);
constexpr ImU32 kAccentGlow = IM_COL32(140, 137, 255, 255);
constexpr ImU32 kOnAccent   = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kGood       = IM_COL32(76,  207, 132, 255);
constexpr ImU32 kWarning    = IM_COL32(240, 178, 74,  255);
constexpr ImU32 kDanger     = IM_COL32(243, 101, 109, 255);

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
