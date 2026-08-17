#include "guiWidgets.h"

#include "../util/keybindUtil.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace Gui
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;

        constexpr float kLabelSize = 13.0f;
        constexpr float kDescSize = 11.0f;
        constexpr float kValueSize = 12.0f;

        // Set by BeginRow; read via RowRightClicked() right after a Row* helper
        // returns so the caller can reset that setting on a right-click.
        bool g_lastRowRightClicked = false;

        void TickSpring(Spring& spring, float target)
        {
            spring.target = target;
            if (MotionMultiplier() > 1.5f)
                spring.Snap(target);
            else
                spring.Tick(DeltaTime());
        }

        struct Row
        {
            ImDrawList* dl = nullptr;
            ImGuiID id = 0;
            ImVec2 min;
            ImVec2 max;
            float width = 0.0f;
            float height = 0.0f;
            bool hovered = false;
            bool active = false;
            bool activated = false;
            bool pressed = false;
            ImVec2 cursorAfter;
        };

        Row BeginRow(const char* label, float height)
        {
            Row row;
            ImGui::PushID(label);

            row.dl = ImGui::GetWindowDrawList();
            row.min = ImGui::GetCursorScreenPos();
            row.width = ImGui::GetContentRegionAvail().x;
            if (row.width < 60.0f)
                row.width = 60.0f;
            row.height = height;
            row.max = ImVec2(row.min.x + row.width, row.min.y + height);

            row.id = ImGui::GetID("##row");
            ImGui::InvisibleButton("##row", ImVec2(row.width, height));

            row.hovered = ImGui::IsItemHovered();
            row.active = ImGui::IsItemActive();
            row.activated = ImGui::IsItemActivated();
            row.pressed = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            g_lastRowRightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
            row.cursorAfter = ImGui::GetCursorScreenPos();
            return row;
        }

        void EndRow(const Row& row, bool separator)
        {
            if (separator)
            {
                const float y = std::floor(row.max.y) - 0.5f;
                row.dl->AddLine(ImVec2(row.min.x, y), ImVec2(row.max.x, y),
                                Alpha(Colors().line, 0.85f), 1.0f);
            }
            ImGui::SetCursorScreenPos(row.cursorAfter);
            ImGui::PopID();
        }

        // Shared left hand side of every settings row.
        void DrawRowLabel(const Row& row, const char* label, const char* desc,
                          ImU32 labelColor, float reservedRight)
        {
            const Palette& c = Colors();
            const bool hasDesc = desc && *desc;
            const float maxWidth = row.width - reservedRight - 4.0f;

            if (hasDesc)
            {
                TextEllipsis(row.dl, FontRegular(), kLabelSize,
                             ImVec2(row.min.x, row.min.y + row.height * 0.5f - 15.0f),
                             maxWidth, labelColor, label);
                TextEllipsis(row.dl, FontRegular(), kDescSize,
                             ImVec2(row.min.x, row.min.y + row.height * 0.5f + 2.0f),
                             maxWidth, c.textMute, desc);
            }
            else
            {
                TextEllipsis(row.dl, FontRegular(), kLabelSize,
                             ImVec2(row.min.x, row.min.y + (row.height - kLabelSize) * 0.5f - 1.0f),
                             maxWidth, labelColor, label);
            }
        }

        float RowHeight(const char* desc, float compact, float tall)
        {
            return (desc && *desc) ? tall : compact;
        }

        void FormatValue(char* buffer, size_t size, float value, bool integer, const char* suffix)
        {
            if (integer)
                std::snprintf(buffer, size, "%d%s", static_cast<int>(std::lround(value)), suffix ? suffix : "");
            else
                std::snprintf(buffer, size, "%.2f%s", value, suffix ? suffix : "");
        }

        // Horizontal track shared by the slider and the range slider.
        // `min`/`max` are the visual bounds; knob centres are kept inside by
        // KnobTravel() so a handle at either end never spills past the edge.
        void DrawTrack(ImDrawList* dl, ImVec2 min, ImVec2 max, float fillFrom, float fillTo)
        {
            const Palette& c = Colors();
            const float radius = (max.y - min.y) * 0.5f;
            dl->AddRectFilled(min, max, c.widget, radius);
            if (fillTo > fillFrom)
                dl->AddRectFilled(ImVec2(fillFrom, min.y), ImVec2(fillTo, max.y), c.accent, radius);
        }

        // Knobs are drawn centred, so the usable travel is inset by the knob
        // radius on both sides.
        constexpr float kKnobRadius = 6.5f;
        constexpr float kKnobGrabScale = 1.22f;
        // Inset by the enlarged grab radius so the scaled knob never clips
        // past the track / parent clip rect at either end.
        constexpr float kKnobTravelInset = kKnobRadius * kKnobGrabScale;

        void KnobTravel(float trackMinX, float trackMaxX, float& outMinX, float& outSpan)
        {
            outMinX = trackMinX + kKnobTravelInset;
            outSpan = std::max(1.0f, (trackMaxX - kKnobTravelInset) - outMinX);
        }

        void DrawKnob(ImDrawList* dl, ImVec2 center, float radius)
        {
            dl->AddShadowRect(ImVec2(center.x - radius, center.y - radius),
                              ImVec2(center.x + radius, center.y + radius),
                              Fade(IM_COL32(0, 0, 0, 120)), 8.0f, ImVec2(0.0f, 1.5f),
                              ImDrawFlags_ShadowCutOutShapeBackground, radius);
            dl->AddCircleFilled(center, radius, Fade(IM_COL32(248, 248, 252, 255)), 24);
        }

        struct PopupStyle
        {
            PopupStyle()
            {
                const Palette& c = Colors();
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 3.0f));
                ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(c.panel));
            }
            ~PopupStyle()
            {
                ImGui::PopStyleColor();
                ImGui::PopStyleVar(3);
            }
        };

        // A single option inside a dropdown popup.
        // snapState: the popup only repaints its options while open, so the
        // per-option state never eases towards the current selection while
        // closed. Without snapping on the opening frame, the check mark and
        // selection highlight visibly fade in late on every open.
        bool PopupOption(const char* text, bool selected, float width, bool snapState)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const float height = 26.0f;
            const ImVec2 max(min.x + width, min.y + height);

            ImGui::PushID(text);
            const ImGuiID id = ImGui::GetID("##opt");
            ImGui::InvisibleButton("##opt", ImVec2(width, height));
            const bool hovered = ImGui::IsItemHovered();
            const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            ImGui::PopID();

            struct S { float highlight = 0.0f; float mark = 0.0f; };
            S* s = State<S>(id);
            if (snapState)
            {
                s->highlight = (hovered || selected) ? 1.0f : 0.0f;
                s->mark = selected ? 1.0f : 0.0f;
            }
            else
            {
                Ease(s->highlight, (hovered || selected) ? 1.0f : 0.0f, 14.0f);
                Ease(s->mark, selected ? 1.0f : 0.0f, 14.0f);
            }

            const Palette& c = Colors();
            if (s->highlight > 0.01f)
            {
                dl->AddRectFilled(min, max,
                                  Alpha(selected ? c.accent : c.widgetHover,
                                        selected ? s->highlight * 0.28f : s->highlight),
                                  7.0f);
            }
            if (s->mark > 0.01f)
                DrawIcon(dl, Icon::Check, ImVec2(min.x + 14.0f, (min.y + max.y) * 0.5f),
                         11.0f, Alpha(c.accent, s->mark), 1.9f);

            TextIn(dl, FontRegular(), kValueSize, ImVec2(min.x + 26.0f, min.y), max,
                   selected ? c.text : (hovered ? c.text : c.textDim), text);

            return clicked;
        }
    }

    // --------------------------------------------------------------- shapes

    void Panel(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 color, float rounding, bool shadow)
    {
        if (!dl)
            return;
        if (shadow)
        {
            dl->AddShadowRect(min, max, Fade(IM_COL32(0, 0, 0, 150)), 34.0f, ImVec2(0.0f, 8.0f),
                              ImDrawFlags_ShadowCutOutShapeBackground, rounding);
        }
        dl->AddRectFilled(min, max, color, rounding);
    }

    void Checkerboard(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding)
    {
        const float step = 5.0f;
        dl->AddRectFilled(min, max, Fade(IM_COL32(58, 58, 70, 255)), rounding);
        dl->PushClipRect(min, max, true);
        int rowIndex = 0;
        for (float y = min.y; y < max.y; y += step, ++rowIndex)
        {
            for (float x = min.x + ((rowIndex & 1) ? step : 0.0f); x < max.x; x += step * 2.0f)
            {
                dl->AddRectFilled(ImVec2(x, y),
                                  ImVec2(std::min(x + step, max.x), std::min(y + step, max.y)),
                                  Fade(IM_COL32(88, 88, 102, 255)));
            }
        }
        dl->PopClipRect();
    }

    void Chevron(ImDrawList* dl, ImVec2 center, float size, float openAmount, ImU32 color, float thickness)
    {
        // openAmount 0 -> pointing down, 1 -> pointing up.
        const float angle = kPi * openAmount;
        const float ca = std::cos(angle), sa = std::sin(angle);
        const float h = size * 0.5f;

        auto rotate = [&](float x, float y) {
            return ImVec2(center.x + x * ca - y * sa, center.y + x * sa + y * ca);
        };

        const ImVec2 left = rotate(-h, -h * 0.45f);
        const ImVec2 mid = rotate(0.0f, h * 0.45f);
        const ImVec2 right = rotate(h, -h * 0.45f);
        dl->AddLine(left, mid, color, thickness);
        dl->AddLine(mid, right, color, thickness);
    }

    void DrawSwitch(ImGuiID id, ImVec2 min, ImVec2 max, bool value, bool hovered, bool pressed)
    {
        struct S
        {
            bool initialised = false;
            float on = 0.0f;
            float hover = 0.0f;
            float press = 1.0f;
            Spring knob = Spring::Make(SpringStyle::Bouncy, 0.0f);
        };

        S* s = State<S>(id);
        if (!s->initialised)
        {
            s->initialised = true;
            s->on = value ? 1.0f : 0.0f;
            s->knob.Snap(value ? 1.0f : 0.0f);
        }

        Ease(s->on, value ? 1.0f : 0.0f, 15.0f);
        Ease(s->hover, hovered ? 1.0f : 0.0f, 13.0f);
        Ease(s->press, pressed ? 0.90f : 1.0f, 18.0f);
        TickSpring(s->knob, value ? 1.0f : 0.0f);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const Palette& c = Colors();

        const float height = max.y - min.y;
        const float radius = height * 0.5f;

        dl->AddRectFilled(min, max, c.widget, radius);
        if (s->hover > 0.01f)
            dl->AddRectFilled(min, max, Alpha(c.widgetHover, s->hover), radius);
        if (s->on > 0.01f)
            dl->AddRectFilled(min, max, Alpha(c.accent, s->on), radius);
        dl->AddRect(min, max, Alpha(c.border, 0.9f - s->on * 0.5f), radius, 0, 1.0f);

        const float travelMin = min.x + radius;
        const float travelMax = max.x - radius;
        const float t = Clamp01(s->knob.x);
        const float kx = travelMin + (travelMax - travelMin) * t;
        const float ky = (min.y + max.y) * 0.5f;

        // Squash the knob along its direction of travel, like the reference menu.
        const float speed = std::min(std::fabs(s->knob.v) * 0.045f, 0.32f);
        const float base = (radius - 3.0f) * s->press;
        const float hw = base * (1.0f + speed);
        const float hh = base * (1.0f - speed * 0.55f);

        dl->AddRectFilled(ImVec2(kx - hw, ky - hh), ImVec2(kx + hw, ky + hh),
                          Fade(IM_COL32(250, 250, 253, 255)), std::min(hw, hh));
    }

    // ---------------------------------------------------------------- rows

    bool RowRightClicked()
    {
        return g_lastRowRightClicked;
    }

    bool RowToggle(const char* label, const char* desc, bool* value)
    {
        Row row = BeginRow(label, RowHeight(desc, 36.0f, 46.0f));
        const Palette& c = Colors();

        const ImVec2 swMax(row.max.x, (row.min.y + row.max.y) * 0.5f + 11.0f);
        const ImVec2 swMin(swMax.x - 42.0f, swMax.y - 22.0f);

        bool changed = false;
        if (row.pressed)
        {
            *value = !*value;
            changed = true;
        }

        struct S { ImVec4 text; bool init = false; };
        S* s = State<S>(row.id);
        const ImVec4 targetText = ImGui::ColorConvertU32ToFloat4(*value ? c.text : c.textDim);
        if (!s->init) { s->init = true; s->text = targetText; }
        Ease(s->text, targetText, 16.0f);

        DrawRowLabel(row, label, desc, ImGui::ColorConvertFloat4ToU32(s->text), 58.0f);
        DrawSwitch(row.id, swMin, swMax, *value, row.hovered, row.active);

        EndRow(row, true);
        return changed;
    }

    bool RowSlider(const char* label, const char* desc, float* value,
                   float minValue, float maxValue, bool integer, const char* suffix)
    {
        Row row = BeginRow(label, RowHeight(desc, 50.0f, 62.0f));
        const Palette& c = Colors();

        struct S { bool grabbing = false; float fill = -1.0f; float knob = 1.0f; };
        S* s = State<S>(row.id);

        const float trackHeight = 6.0f;
        const ImVec2 trackMin(row.min.x, row.max.y - 16.0f);
        const ImVec2 trackMax(row.max.x, trackMin.y + trackHeight);

        const float span = (maxValue - minValue);
        bool changed = false;

        if (row.activated)
        {
            const float my = ImGui::GetIO().MousePos.y;
            s->grabbing = (my >= trackMin.y - 11.0f && my <= trackMax.y + 11.0f);
        }
        if (!row.active)
            s->grabbing = false;

        if (s->grabbing && row.active && span > 0.0f)
        {
            float travelMin = 0.0f, travelSpan = 1.0f;
            KnobTravel(trackMin.x, trackMax.x, travelMin, travelSpan);

            const float t = Clamp01((ImGui::GetIO().MousePos.x - travelMin) / travelSpan);
            float next = minValue + t * span;
            if (integer)
                next = std::round(next);
            if (next != *value)
            {
                *value = next;
                changed = true;
            }
        }

        // Mouse-wheel fine tuning while hovering the row.
        if (row.hovered && span > 0.0f)
        {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f)
            {
                const float step = integer ? 1.0f : std::max(0.01f, span / 100.0f);
                float next = *value + wheel * step;
                if (integer)
                    next = std::round(next);
                next = std::min(std::max(next, minValue), maxValue);
                if (next != *value)
                {
                    *value = next;
                    changed = true;
                }
            }
        }

        *value = std::min(std::max(*value, minValue), maxValue);
        const float ratio = span > 0.0f ? Clamp01((*value - minValue) / span) : 0.0f;

        if (s->fill < 0.0f)
            s->fill = ratio;
        Ease(s->fill, ratio, 20.0f);
        Ease(s->knob, s->grabbing && row.active ? kKnobGrabScale : 1.0f, 16.0f);

        // Label + live value.
        char buffer[64];
        FormatValue(buffer, sizeof(buffer), *value, integer, suffix);
        const ImVec2 valueSize = TextSize(FontRegular(), kValueSize, buffer);

        TextEllipsis(row.dl, FontRegular(), kLabelSize, ImVec2(row.min.x, row.min.y + 6.0f),
                     row.width - valueSize.x - 12.0f, c.text, label);

        // The value is click-to-type: hover hint + double-click opens an input.
        const ImVec2 valMin(row.max.x - valueSize.x - 6.0f, row.min.y + 3.0f);
        const ImVec2 valMax(row.max.x, row.min.y + 24.0f);
        const bool valHover = row.hovered && ImGui::IsMouseHoveringRect(valMin, valMax, false);
        Text(row.dl, FontRegular(), kValueSize,
             ImVec2(row.max.x - valueSize.x, row.min.y + 7.0f),
             (s->grabbing && row.active) ? c.accent : (valHover ? c.text : c.textDim), buffer);

        if (valHover && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            ImGui::OpenPopup("##sliderinput");

        if (desc && *desc)
        {
            TextEllipsis(row.dl, FontRegular(), kDescSize, ImVec2(row.min.x, row.min.y + 23.0f),
                         row.width - 12.0f, c.textMute, desc);
        }

        float travelMin = 0.0f, travelSpan = 1.0f;
        KnobTravel(trackMin.x, trackMax.x, travelMin, travelSpan);

        const float knobX = travelMin + travelSpan * s->fill;
        DrawTrack(row.dl, trackMin, trackMax, trackMin.x, knobX);
        DrawKnob(row.dl, ImVec2(knobX, (trackMin.y + trackMax.y) * 0.5f), kKnobRadius * s->knob);

        // Double-click value → exact text entry.
        {
            static char editBuf[48] = {};
            PopupStyle style;
            ImGui::SetNextWindowPos(ImVec2(row.max.x - 128.0f, row.max.y + 2.0f));
            ImGui::SetNextWindowSize(ImVec2(128.0f, 0.0f));
            if (ImGui::BeginPopup("##sliderinput"))
            {
                if (ImGui::IsWindowAppearing())
                    FormatValue(editBuf, sizeof(editBuf), *value, integer, "");
                ImGui::SetNextItemWidth(112.0f);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(c.widget));
                if (ImGui::InputText("##val", editBuf, sizeof(editBuf),
                                     ImGuiInputTextFlags_EnterReturnsTrue |
                                     ImGuiInputTextFlags_AutoSelectAll))
                {
                    const float parsed = static_cast<float>(std::atof(editBuf));
                    if (parsed >= minValue && parsed <= maxValue)
                    {
                        *value = integer ? std::round(parsed) : parsed;
                        changed = true;
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor();
                ImGui::EndPopup();
            }
        }

        EndRow(row, true);
        return changed;
    }

    bool RowRange(const char* label, const char* desc, float* low, float* high,
                  float minValue, float maxValue, bool integer, const char* suffix)
    {
        Row row = BeginRow(label, RowHeight(desc, 52.0f, 64.0f));
        const Palette& c = Colors();

        struct S { int handle = -1; float lo = -1.0f; float hi = -1.0f; };
        S* s = State<S>(row.id);

        // Handle ids. "Undecided" is used when both knobs sit on the same spot:
        // the mouse position alone cannot tell us which one the user wants
        // (ImGui snaps the cursor to whole pixels while knob centres are
        // fractional), so we wait until the drag reveals a direction.
        enum { HandleNone = -1, HandleLow = 0, HandleHigh = 1, HandleUndecided = 2 };

        const ImVec2 trackMin(row.min.x, row.max.y - 16.0f);
        const ImVec2 trackMax(row.max.x, trackMin.y + 6.0f);
        const float span = maxValue - minValue;

        float travelMin = 0.0f, travelSpan = 1.0f;
        KnobTravel(trackMin.x, trackMax.x, travelMin, travelSpan);

        bool changed = false;

        auto ratioOf = [&](float v) { return span > 0.0f ? Clamp01((v - minValue) / span) : 0.0f; };

        if (row.activated)
        {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            if (mouse.y >= trackMin.y - 11.0f && mouse.y <= trackMax.y + 11.0f)
            {
                const float loX = travelMin + ratioOf(*low) * travelSpan;
                const float hiX = travelMin + ratioOf(*high) * travelSpan;

                if (std::fabs(loX - hiX) < 2.0f)
                    s->handle = HandleUndecided;
                else
                    s->handle = (std::fabs(mouse.x - loX) <= std::fabs(mouse.x - hiX))
                                    ? HandleLow : HandleHigh;
            }
            else
            {
                s->handle = HandleNone;
            }
        }
        if (!row.active)
            s->handle = HandleNone;

        if (s->handle != HandleNone && row.active && span > 0.0f)
        {
            const float t = Clamp01((ImGui::GetIO().MousePos.x - travelMin) / travelSpan);
            float next = minValue + t * span;
            if (integer)
                next = std::round(next);

            if (s->handle == HandleUndecided)
            {
                // First movement away from the collapsed point decides which
                // knob is being dragged.
                if (next > *high)      s->handle = HandleHigh;
                else if (next < *low)  s->handle = HandleLow;
            }

            if (s->handle == HandleLow)
            {
                next = std::min(next, *high);
                if (next != *low) { *low = next; changed = true; }
            }
            else if (s->handle == HandleHigh)
            {
                next = std::max(next, *low);
                if (next != *high) { *high = next; changed = true; }
            }
        }

        *low = std::min(std::max(*low, minValue), maxValue);
        *high = std::min(std::max(*high, minValue), maxValue);

        const float loRatio = ratioOf(*low);
        const float hiRatio = ratioOf(*high);
        if (s->lo < 0.0f) { s->lo = loRatio; s->hi = hiRatio; }
        Ease(s->lo, loRatio, 20.0f);
        Ease(s->hi, hiRatio, 20.0f);

        char loText[32], hiText[32], combined[80];
        FormatValue(loText, sizeof(loText), *low, integer, "");
        FormatValue(hiText, sizeof(hiText), *high, integer, suffix);
        std::snprintf(combined, sizeof(combined), "%s - %s", loText, hiText);

        const ImVec2 valueSize = TextSize(FontRegular(), kValueSize, combined);
        TextEllipsis(row.dl, FontRegular(), kLabelSize, ImVec2(row.min.x, row.min.y + 6.0f),
                     row.width - valueSize.x - 12.0f, c.text, label);
        Text(row.dl, FontRegular(), kValueSize,
             ImVec2(row.max.x - valueSize.x, row.min.y + 7.0f),
             s->handle != HandleNone ? c.accent : c.textDim, combined);

        if (desc && *desc)
        {
            TextEllipsis(row.dl, FontRegular(), kDescSize, ImVec2(row.min.x, row.min.y + 23.0f),
                         row.width - 12.0f, c.textMute, desc);
        }

        const float loX = travelMin + s->lo * travelSpan;
        const float hiX = travelMin + s->hi * travelSpan;
        DrawTrack(row.dl, trackMin, trackMax, loX, hiX);
        const float centerY = (trackMin.y + trackMax.y) * 0.5f;
        DrawKnob(row.dl, ImVec2(loX, centerY), kKnobRadius);
        DrawKnob(row.dl, ImVec2(hiX, centerY), kKnobRadius);

        EndRow(row, true);
        return changed;
    }

    bool RowSlotPicker(const char* label, const char* desc, float* value,
                       int firstSlot, int lastSlot)
    {
        if (lastSlot < firstSlot)
            std::swap(firstSlot, lastSlot);

        const int count = lastSlot - firstSlot + 1;
        Row row = BeginRow(label, RowHeight(desc, 52.0f, 62.0f));
        const Palette& c = Colors();

        // Chips are laid out right aligned so the label keeps its column.
        const float chip = std::min(30.0f, std::max(20.0f, (row.width * 0.62f) / static_cast<float>(count) - 4.0f));
        const float gap = 4.0f;
        const float stripWidth = static_cast<float>(count) * chip + static_cast<float>(count - 1) * gap;
        const float stripLeft = row.max.x - stripWidth;
        const float stripTop = row.max.y - chip - 8.0f;

        DrawRowLabel(row, label, desc, c.text, 0.0f);

        int current = static_cast<int>(std::lround(*value));
        current = std::min(std::max(current, firstSlot), lastSlot);

        bool changed = false;

        for (int i = 0; i < count; ++i)
        {
            const int slot = firstSlot + i;
            const ImVec2 min(stripLeft + static_cast<float>(i) * (chip + gap), stripTop);
            const ImVec2 max(min.x + chip, min.y + chip);

            const bool hovered = row.hovered && ImGui::IsMouseHoveringRect(min, max, false);
            if (hovered && row.pressed && slot != current)
            {
                *value = static_cast<float>(slot);
                current = slot;
                changed = true;
            }

            const bool selected = slot == current;

            ImGui::PushID(slot);
            const ImGuiID id = ImGui::GetID("##slot");
            ImGui::PopID();

            struct ChipState { float sel = 0.0f; float hover = 0.0f; };
            ChipState* s = State<ChipState>(id);
            Ease(s->sel, selected ? 1.0f : 0.0f, 16.0f);
            Ease(s->hover, hovered ? 1.0f : 0.0f, 14.0f);

            // Selected chip lifts slightly, mirroring a hotbar highlight.
            const float lift = s->sel * 2.0f;
            const ImVec2 aMin(min.x, min.y - lift);
            const ImVec2 aMax(max.x, max.y - lift);

            row.dl->AddRectFilled(aMin, aMax,
                                  Mix(Mix(c.widget, c.widgetHover, s->hover), c.accent, s->sel), 7.0f);
            row.dl->AddRect(aMin, aMax,
                            Mix(Alpha(c.border, 0.9f), c.accent, s->sel), 7.0f, 0, 1.0f);

            char text[8];
            std::snprintf(text, sizeof(text), "%d", slot);
            TextIn(row.dl, FontRegular(), 11.0f, aMin, aMax,
                   selected ? c.onAccent : Mix(c.textDim, c.text, s->hover), text, ImVec2(0.5f, 0.5f));
        }

        (void)firstSlot;
        EndRow(row, true);
        return changed;
    }

    bool RowSlotMultiPicker(const char* label, const char* desc, std::string* csv)
    {
        const int firstSlot = 1;
        const int lastSlot = 9;
        const int count = lastSlot - firstSlot + 1;

        Row row = BeginRow(label, RowHeight(desc, 52.0f, 62.0f));
        const Palette& c = Colors();

        // Parse the comma separated membership into a fixed 9-slot bit field.
        bool selected[9] = { false, false, false, false, false, false, false, false, false };
        if (csv)
        {
            size_t start = 0;
            while (start <= csv->size())
            {
                const size_t comma = csv->find(',', start);
                const size_t end = (comma == std::string::npos) ? csv->size() : comma;

                size_t b = start, e = end;
                while (b < e && ((*csv)[b] == ' ' || (*csv)[b] == '\t' || (*csv)[b] == '\r' || (*csv)[b] == '\n')) ++b;
                while (e > b && ((*csv)[e - 1] == ' ' || (*csv)[e - 1] == '\t' || (*csv)[e - 1] == '\r' || (*csv)[e - 1] == '\n')) --e;

                bool digits = e > b;
                for (size_t i = b; digits && i < e; ++i)
                    if ((*csv)[i] < '0' || (*csv)[i] > '9') digits = false;

                if (digits)
                {
                    int slot = 0;
                    for (size_t i = b; i < e; ++i)
                        slot = slot * 10 + ((*csv)[i] - '0');
                    if (slot >= firstSlot && slot <= lastSlot)
                        selected[slot - firstSlot] = true;
                }

                if (comma == std::string::npos)
                    break;
                start = comma + 1;
            }
        }

        // Chips are laid out right aligned so the label keeps its column.
        const float chip = std::min(30.0f, std::max(20.0f, (row.width * 0.62f) / static_cast<float>(count) - 4.0f));
        const float gap = 4.0f;
        const float stripWidth = static_cast<float>(count) * chip + static_cast<float>(count - 1) * gap;
        const float stripLeft = row.max.x - stripWidth;
        const float stripTop = row.max.y - chip - 8.0f;

        DrawRowLabel(row, label, desc, c.text, 0.0f);

        bool changed = false;

        for (int i = 0; i < count; ++i)
        {
            const int slot = firstSlot + i;
            const ImVec2 min(stripLeft + static_cast<float>(i) * (chip + gap), stripTop);
            const ImVec2 max(min.x + chip, min.y + chip);

            const bool hovered = row.hovered && ImGui::IsMouseHoveringRect(min, max, false);
            if (hovered && row.pressed)
            {
                selected[slot - firstSlot] = !selected[slot - firstSlot];
                changed = true;
            }

            const bool isSel = selected[slot - firstSlot];

            ImGui::PushID(slot);
            const ImGuiID id = ImGui::GetID("##slotmulti");
            ImGui::PopID();

            struct ChipState { float sel = 0.0f; float hover = 0.0f; };
            ChipState* s = State<ChipState>(id);
            Ease(s->sel, isSel ? 1.0f : 0.0f, 16.0f);
            Ease(s->hover, hovered ? 1.0f : 0.0f, 14.0f);

            // Selected chip lifts slightly, mirroring a hotbar highlight.
            const float lift = s->sel * 2.0f;
            const ImVec2 aMin(min.x, min.y - lift);
            const ImVec2 aMax(max.x, max.y - lift);

            row.dl->AddRectFilled(aMin, aMax,
                                  Mix(Mix(c.widget, c.widgetHover, s->hover), c.accent, s->sel), 7.0f);
            row.dl->AddRect(aMin, aMax,
                            Mix(Alpha(c.border, 0.9f), c.accent, s->sel), 7.0f, 0, 1.0f);

            char text[8];
            std::snprintf(text, sizeof(text), "%d", slot);
            TextIn(row.dl, FontRegular(), 11.0f, aMin, aMax,
                   isSel ? c.onAccent : Mix(c.textDim, c.text, s->hover), text, ImVec2(0.5f, 0.5f));
        }

        if (changed && csv)
        {
            std::string out;
            for (int slot = firstSlot; slot <= lastSlot; ++slot)
            {
                if (!selected[slot - firstSlot]) continue;
                if (!out.empty()) out += ",";
                out += std::to_string(slot);
            }
            *csv = out;
        }

        EndRow(row, true);
        return changed;
    }

    bool RowDropdown(const char* label, const char* desc, int* index,
                     const char* const* items, int itemCount)
    {
        Row row = BeginRow(label, RowHeight(desc, 40.0f, 50.0f));
        const Palette& c = Colors();

        if (itemCount <= 0)
        {
            EndRow(row, true);
            return false;
        }
        *index = std::min(std::max(*index, 0), itemCount - 1);

        const float boxWidth = std::min(190.0f, std::max(110.0f, row.width * 0.46f));
        const ImVec2 boxMax(row.max.x, (row.min.y + row.max.y) * 0.5f + 14.0f);
        const ImVec2 boxMin(boxMax.x - boxWidth, boxMax.y - 28.0f);

        struct S { float open = 0.0f; float hover = 0.0f; };
        S* s = State<S>(row.id);

        const bool boxHovered = row.hovered &&
            ImGui::IsMouseHoveringRect(boxMin, boxMax, false);

        if (row.pressed)
            ImGui::OpenPopup("##dropdown");

        Ease(s->hover, boxHovered ? 1.0f : 0.0f, 14.0f);

        DrawRowLabel(row, label, desc, c.text, boxWidth + 12.0f);

        row.dl->AddRectFilled(boxMin, boxMax, Mix(c.widget, c.widgetHover, s->hover), 8.0f);
        row.dl->AddRect(boxMin, boxMax, Alpha(c.border, 0.9f), 8.0f, 0, 1.0f);

        TextEllipsis(row.dl, FontRegular(), kValueSize,
                     ImVec2(boxMin.x + 11.0f, (boxMin.y + boxMax.y) * 0.5f - kValueSize * 0.5f - 1.0f),
                     boxWidth - 34.0f, c.text, items[*index]);
        Chevron(row.dl, ImVec2(boxMax.x - 15.0f, (boxMin.y + boxMax.y) * 0.5f),
                9.0f, s->open, c.textDim, 1.8f);

        bool changed = false;
        {
            PopupStyle style;
            // Slide + fade in from the anchor as the popup opens.
            ImGui::SetNextWindowPos(ImVec2(boxMin.x,
                                           boxMax.y + 6.0f + (1.0f - SmoothStep(s->open)) * 6.0f));
            ImGui::SetNextWindowSize(ImVec2(boxWidth, 0.0f));
            if (ImGui::BeginPopup("##dropdown"))
            {
                const bool snapOptions = ImGui::IsWindowAppearing();
                Ease(s->open, 1.0f, 16.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, SmoothStep(s->open));
                const float innerWidth = boxWidth - 16.0f;
                for (int i = 0; i < itemCount; ++i)
                {
                    if (PopupOption(items[i], i == *index, innerWidth, snapOptions))
                    {
                        *index = i;
                        changed = true;
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::PopStyleVar();
                ImGui::EndPopup();
            }
            else
            {
                Ease(s->open, 0.0f, 16.0f);
            }
        }

        EndRow(row, true);
        return changed;
    }

    bool RowKeybind(const char* label, const char* desc, int* virtualKey, bool* listening)
    {
        Row row = BeginRow(label, RowHeight(desc, 40.0f, 50.0f));
        const Palette& c = Colors();

        struct S { int startFrame = 0; float glow = 0.0f; float hover = 0.0f; };
        S* s = State<S>(row.id);

        const float chipWidth = 104.0f;
        const ImVec2 chipMax(row.max.x, (row.min.y + row.max.y) * 0.5f + 13.0f);
        const ImVec2 chipMin(chipMax.x - chipWidth, chipMax.y - 26.0f);

        bool changed = false;

        if (row.pressed)
        {
            *listening = !*listening;
            s->startFrame = ImGui::GetFrameCount();
        }

        if (*listening)
        {
            // Give the click that opened the capture one frame to settle.
            if (ImGui::GetFrameCount() > s->startFrame + 1)
            {
                for (int vk = 0x01; vk <= 0xFE; ++vk)
                {
                    if (vk == VK_LBUTTON)
                        continue;
                    if (!(GetAsyncKeyState(vk) & 0x8000))
                        continue;

                    *virtualKey = (vk == VK_ESCAPE) ? 0 : vk;
                    *listening = false;
                    changed = true;
                    break;
                }
            }
        }

        const bool chipHovered = row.hovered && ImGui::IsMouseHoveringRect(chipMin, chipMax, false);
        Ease(s->hover, chipHovered ? 1.0f : 0.0f, 14.0f);
        Ease(s->glow, *listening ? 1.0f : 0.0f, 12.0f);

        DrawRowLabel(row, label, desc, c.text, chipWidth + 12.0f);

        if (s->glow > 0.01f)
        {
            const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 5.2f);
            row.dl->AddShadowRect(chipMin, chipMax,
                                  Alpha(c.accent, (0.22f + pulse * 0.16f) * s->glow),
                                  16.0f, ImVec2(0.0f, 0.0f),
                                  ImDrawFlags_ShadowCutOutShapeBackground, 8.0f);
        }

        row.dl->AddRectFilled(chipMin, chipMax,
                              Mix(Mix(c.widget, c.widgetHover, s->hover), c.accent, s->glow), 8.0f);
        row.dl->AddRect(chipMin, chipMax,
                        Mix(Alpha(c.border, 0.9f), c.accent, s->glow), 8.0f, 0, 1.0f);

        char keyName[64];
        const char* chipText = *listening
            ? "Press key..."
            : (*virtualKey ? KeybindUtil::FormatKeyName(*virtualKey, keyName, sizeof(keyName)) : "None");

        TextIn(row.dl, FontRegular(), 11.0f, chipMin, chipMax,
               *listening ? c.onAccent : (*virtualKey ? c.text : c.textMute),
               chipText, ImVec2(0.5f, 0.5f));

        EndRow(row, true);
        return changed;
    }

    bool RowColor(const char* label, const char* desc, float rgba[4])
    {
        Row row = BeginRow(label, RowHeight(desc, 40.0f, 50.0f));
        const Palette& c = Colors();

        struct S { float h = -1.0f, s = 0.0f, v = 0.0f; float hover = 0.0f; };
        S* st = State<S>(row.id);

        const float swatchRadius = 12.0f;
        const ImVec2 swCenter(row.max.x - swatchRadius, (row.min.y + row.max.y) * 0.5f);
        const ImVec2 swMin(swCenter.x - swatchRadius, swCenter.y - swatchRadius);
        const ImVec2 swMax(swCenter.x + swatchRadius, swCenter.y + swatchRadius);

        if (row.pressed)
        {
            ImGui::ColorConvertRGBtoHSV(rgba[0], rgba[1], rgba[2], st->h, st->s, st->v);
            ImGui::OpenPopup("##colour");
        }

        const bool swHovered = row.hovered && ImGui::IsMouseHoveringRect(swMin, swMax, false);
        Ease(st->hover, swHovered ? 1.0f : 0.0f, 14.0f);

        DrawRowLabel(row, label, desc, c.text, swatchRadius * 2.0f + 12.0f);

        // Fully circular swatch: the alpha checkerboard is clipped to the
        // circle so no background rectangle peeks out behind it.
        const float drawRadius = swatchRadius * (1.0f + st->hover * 0.06f);
        if (rgba[3] < 0.999f)
        {
            row.dl->PushClipRect(swMin, swMax, true);
            Checkerboard(row.dl, swMin, swMax, swatchRadius);
            row.dl->PopClipRect();
            row.dl->AddCircleFilled(swCenter, drawRadius, Fade(IM_COL32(58, 58, 70, 255)), 32);
        }
        row.dl->AddCircleFilled(swCenter, drawRadius,
                                ImGui::ColorConvertFloat4ToU32(
                                    ImVec4(rgba[0], rgba[1], rgba[2], rgba[3])), 32);
        row.dl->AddCircle(swCenter, drawRadius,
                          Alpha(IM_COL32(255, 255, 255, 255), 0.16f + st->hover * 0.22f), 32, 1.0f);

        bool changed = false;
        {
            PopupStyle style;
            ImGui::SetNextWindowPos(ImVec2(swMax.x - 216.0f, swMax.y + 6.0f));
            ImGui::SetNextWindowSize(ImVec2(216.0f, 0.0f));
            if (ImGui::BeginPopup("##colour"))
            {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const float width = 200.0f;

                if (st->h < 0.0f)
                    ImGui::ColorConvertRGBtoHSV(rgba[0], rgba[1], rgba[2], st->h, st->s, st->v);

                // ---- saturation / value square
                ImVec2 svMin = ImGui::GetCursorScreenPos();
                ImVec2 svMax(svMin.x + width, svMin.y + 130.0f);
                ImGui::InvisibleButton("##sv", ImVec2(width, 130.0f));
                if (ImGui::IsItemActive())
                {
                    const ImVec2 m = ImGui::GetIO().MousePos;
                    st->s = Clamp01((m.x - svMin.x) / width);
                    st->v = 1.0f - Clamp01((m.y - svMin.y) / 130.0f);
                    changed = true;
                }

                float hr, hg, hb;
                ImGui::ColorConvertHSVtoRGB(st->h, 1.0f, 1.0f, hr, hg, hb);
                const ImU32 hue = ImGui::ColorConvertFloat4ToU32(ImVec4(hr, hg, hb, 1.0f));
                const ImU32 white = IM_COL32(255, 255, 255, 255);
                const ImU32 clear = IM_COL32(0, 0, 0, 0);
                const ImU32 black = IM_COL32(0, 0, 0, 255);

                dl->AddRectFilledMultiColor(svMin, svMax, white, hue, hue, white);
                dl->AddRectFilledMultiColor(svMin, svMax, clear, clear, black, black);
                dl->AddRect(svMin, svMax, Alpha(white, 0.10f), 6.0f, 0, 1.0f);

                const ImVec2 svKnob(svMin.x + st->s * width, svMin.y + (1.0f - st->v) * 130.0f);
                dl->AddCircle(svKnob, 6.0f, white, 20, 2.0f);
                dl->AddCircle(svKnob, 7.5f, IM_COL32(0, 0, 0, 90), 20, 1.5f);

                // ---- hue bar
                ImGui::Dummy(ImVec2(0.0f, 2.0f));
                ImVec2 hueMin = ImGui::GetCursorScreenPos();
                ImVec2 hueMax(hueMin.x + width, hueMin.y + 14.0f);
                ImGui::InvisibleButton("##hue", ImVec2(width, 14.0f));
                if (ImGui::IsItemActive())
                {
                    st->h = Clamp01((ImGui::GetIO().MousePos.x - hueMin.x) / width);
                    changed = true;
                }

                const ImU32 hues[7] = {
                    IM_COL32(255, 0, 0, 255), IM_COL32(255, 255, 0, 255),
                    IM_COL32(0, 255, 0, 255), IM_COL32(0, 255, 255, 255),
                    IM_COL32(0, 0, 255, 255), IM_COL32(255, 0, 255, 255),
                    IM_COL32(255, 0, 0, 255),
                };
                for (int i = 0; i < 6; ++i)
                {
                    const float x0 = hueMin.x + width * (static_cast<float>(i) / 6.0f);
                    const float x1 = hueMin.x + width * (static_cast<float>(i + 1) / 6.0f);
                    dl->AddRectFilledMultiColor(ImVec2(x0, hueMin.y), ImVec2(x1, hueMax.y),
                                                hues[i], hues[i + 1], hues[i + 1], hues[i]);
                }
                dl->AddCircleFilled(ImVec2(hueMin.x + st->h * width, (hueMin.y + hueMax.y) * 0.5f),
                                    6.0f, white, 20);

                // ---- alpha bar
                ImGui::Dummy(ImVec2(0.0f, 2.0f));
                ImVec2 aMin = ImGui::GetCursorScreenPos();
                ImVec2 aMax(aMin.x + width, aMin.y + 14.0f);
                ImGui::InvisibleButton("##alpha", ImVec2(width, 14.0f));
                if (ImGui::IsItemActive())
                {
                    rgba[3] = Clamp01((ImGui::GetIO().MousePos.x - aMin.x) / width);
                    changed = true;
                }
                Checkerboard(dl, aMin, aMax, 4.0f);
                dl->AddRectFilledMultiColor(aMin, aMax, Alpha(hue, 0.0f), hue, hue, Alpha(hue, 0.0f));
                dl->AddCircleFilled(ImVec2(aMin.x + rgba[3] * width, (aMin.y + aMax.y) * 0.5f),
                                    6.0f, white, 20);

                // ---- hex field
                ImGui::Dummy(ImVec2(0.0f, 2.0f));
                char hex[16];
                std::snprintf(hex, sizeof(hex), "%02X%02X%02X",
                              static_cast<int>(rgba[0] * 255.0f + 0.5f),
                              static_cast<int>(rgba[1] * 255.0f + 0.5f),
                              static_cast<int>(rgba[2] * 255.0f + 0.5f));

                ImGui::SetNextItemWidth(width);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(c.widget));
                if (ImGui::InputText("##hex", hex, sizeof(hex),
                                     ImGuiInputTextFlags_CharsHexadecimal |
                                     ImGuiInputTextFlags_CharsUppercase |
                                     ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    char* parseEnd = nullptr;
                    const unsigned long packed = std::strtoul(hex, &parseEnd, 16);
                    if (parseEnd && parseEnd != hex)
                    {
                        rgba[0] = static_cast<float>((packed >> 16) & 0xFF) / 255.0f;
                        rgba[1] = static_cast<float>((packed >> 8) & 0xFF) / 255.0f;
                        rgba[2] = static_cast<float>(packed & 0xFF) / 255.0f;
                        ImGui::ColorConvertRGBtoHSV(rgba[0], rgba[1], rgba[2], st->h, st->s, st->v);
                        changed = true;
                    }
                }
                ImGui::PopStyleColor();

                if (changed)
                    ImGui::ColorConvertHSVtoRGB(st->h, st->s, st->v, rgba[0], rgba[1], rgba[2]);

                ImGui::EndPopup();
            }
        }

        EndRow(row, true);
        return changed;
    }

    bool RowTextInput(const char* label, const char* desc, std::string* value, size_t maxLength)
    {
        Row row = BeginRow(label, RowHeight(desc, 42.0f, 52.0f));
        const Palette& c = Colors();

        struct S { char buffer[256] = {}; bool primed = false; float hover = 0.0f; };
        S* s = State<S>(row.id);

        const size_t limit = std::min(maxLength + 1, sizeof(S::buffer));
        if (!s->primed)
        {
            s->primed = true;
            std::snprintf(s->buffer, sizeof(s->buffer), "%s", value->c_str());
        }

        const float boxWidth = std::min(210.0f, std::max(120.0f, row.width * 0.5f));
        const ImVec2 boxMax(row.max.x, (row.min.y + row.max.y) * 0.5f + 15.0f);
        const ImVec2 boxMin(boxMax.x - boxWidth, boxMax.y - 30.0f);

        const bool boxHovered = row.hovered && ImGui::IsMouseHoveringRect(boxMin, boxMax, false);
        Ease(s->hover, boxHovered ? 1.0f : 0.0f, 14.0f);

        DrawRowLabel(row, label, desc, c.text, boxWidth + 12.0f);

        row.dl->AddRectFilled(boxMin, boxMax, Mix(c.widget, c.widgetHover, s->hover), 8.0f);
        row.dl->AddRect(boxMin, boxMax, Alpha(c.border, 0.9f), 8.0f, 0, 1.0f);

        const ImVec2 restore = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(boxMin.x + 10.0f,
                                         (boxMin.y + boxMax.y) * 0.5f - ImGui::GetFrameHeight() * 0.5f));
        ImGui::SetNextItemWidth(boxWidth - 20.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));

        const bool edited = ImGui::InputText("##edit", s->buffer, limit);

        ImGui::PopStyleColor(3);
        ImGui::SetCursorScreenPos(restore);

        if (edited)
            *value = s->buffer;
        else if (!ImGui::IsItemActive() && *value != s->buffer)
            std::snprintf(s->buffer, sizeof(s->buffer), "%s", value->c_str());

        EndRow(row, true);
        return edited;
    }

    // ------------------------------------------------------------- item list

    namespace
    {
        // Presets offered by every "Limit to items" style setting. The stored
        // value is a plain comma separated keyword list, so anything typed by
        // hand keeps working exactly as before.
        struct ItemPreset { const char* keyword; const char* label; };

        const ItemPreset kItemPresets[] = {
            { "sword",   "Sword"    },
            { "axe",     "Axe"      },
            { "pickaxe", "Pickaxe"  },
            { "shovel",  "Shovel"   },
            { "hoe",     "Hoe"      },
            { "bow",     "Bow"      },
            { "rod",     "Rod"      },
            { "shears",  "Shears"   },
            { "potion",  "Potion"   },
            { "apple",   "Gapple"   },
            { "block",   "Block"    },
            { "pearl",   "Pearl"    },
        };

        constexpr int kItemPresetCount =
            static_cast<int>(sizeof(kItemPresets) / sizeof(kItemPresets[0]));

        constexpr float kChipHeight = 24.0f;
        constexpr float kChipGapX = 6.0f;
        constexpr float kChipGapY = 6.0f;

        std::string TrimCopy(const std::string& in)
        {
            size_t b = 0, e = in.size();
            while (b < e && (in[b] == ' ' || in[b] == '\t')) ++b;
            while (e > b && (in[e - 1] == ' ' || in[e - 1] == '\t')) --e;
            return in.substr(b, e - b);
        }

        std::vector<std::string> SplitCsv(const std::string& csv)
        {
            std::vector<std::string> out;
            size_t start = 0;
            while (start <= csv.size())
            {
                const size_t comma = csv.find(',', start);
                const size_t end = (comma == std::string::npos) ? csv.size() : comma;
                std::string token = TrimCopy(csv.substr(start, end - start));
                if (!token.empty())
                    out.push_back(token);
                if (comma == std::string::npos)
                    break;
                start = comma + 1;
            }
            return out;
        }

        std::string JoinCsv(const std::vector<std::string>& items)
        {
            std::string out;
            for (size_t i = 0; i < items.size(); ++i)
            {
                if (i)
                    out += ",";
                out += items[i];
            }
            return out;
        }

        bool EqualsFold(const std::string& a, const std::string& b)
        {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i)
            {
                if (std::tolower(static_cast<unsigned char>(a[i])) !=
                    std::tolower(static_cast<unsigned char>(b[i])))
                    return false;
            }
            return true;
        }

        bool ListContains(const std::vector<std::string>& items, const std::string& value)
        {
            for (const std::string& item : items)
                if (EqualsFold(item, value))
                    return true;
            return false;
        }

        float ChipWidth(const char* label)
        {
            return TextSize(FontRegular(), 11.0f, label).x + 26.0f;
        }

        // Lays chips out on wrapped rows and reports the total height.
        struct ChipLayout
        {
            struct Slot { float x, y, w; };
            std::vector<Slot> slots;
            float height = 0.0f;
        };

        ChipLayout LayoutChips(const std::vector<float>& widths, float maxWidth)
        {
            ChipLayout layout;
            layout.slots.reserve(widths.size());

            float x = 0.0f;
            float y = 0.0f;
            for (float w : widths)
            {
                if (x > 0.0f && x + w > maxWidth)
                {
                    x = 0.0f;
                    y += kChipHeight + kChipGapY;
                }
                layout.slots.push_back({ x, y, w });
                x += w + kChipGapX;
            }
            layout.height = widths.empty() ? 0.0f : y + kChipHeight;
            return layout;
        }

        // Builds the display order: presets first, then custom entries, then
        // the "Add custom" chip.
        void BuildChipModel(const std::string& csv,
                            std::vector<std::string>& outKeywords,
                            std::vector<std::string>& outLabels,
                            std::vector<bool>& outActive,
                            std::vector<bool>& outCustom)
        {
            const std::vector<std::string> selected = SplitCsv(csv);

            for (int i = 0; i < kItemPresetCount; ++i)
            {
                outKeywords.push_back(kItemPresets[i].keyword);
                outLabels.push_back(kItemPresets[i].label);
                outActive.push_back(ListContains(selected, kItemPresets[i].keyword));
                outCustom.push_back(false);
            }

            for (const std::string& entry : selected)
            {
                bool isPreset = false;
                for (int i = 0; i < kItemPresetCount; ++i)
                {
                    if (EqualsFold(entry, kItemPresets[i].keyword)) { isPreset = true; break; }
                }
                if (isPreset)
                    continue;

                outKeywords.push_back(entry);
                outLabels.push_back(entry);
                outActive.push_back(true);
                outCustom.push_back(true);
            }
        }

        float ItemListChipAreaHeight(const std::string& csv, float width)
        {
            std::vector<std::string> keywords, labels;
            std::vector<bool> active, custom;
            BuildChipModel(csv, keywords, labels, active, custom);

            std::vector<float> widths;
            widths.reserve(labels.size() + 1);
            for (size_t i = 0; i < labels.size(); ++i)
                widths.push_back(ChipWidth(labels[i].c_str()) + (custom[i] ? 14.0f : 0.0f));
            widths.push_back(ChipWidth("Add custom") + 6.0f);

            return LayoutChips(widths, std::max(60.0f, width)).height;
        }
    }

    float MeasureItemList(const char* desc, const std::string& csv, float width)
    {
        const float header = (desc && *desc) ? 38.0f : 24.0f;
        return header + ItemListChipAreaHeight(csv, width) + 12.0f;
    }

    bool RowItemList(const char* label, const char* desc, std::string* csv)
    {
        const float available = std::max(60.0f, ImGui::GetContentRegionAvail().x);
        const float header = (desc && *desc) ? 38.0f : 24.0f;

        std::vector<std::string> keywords, labels;
        std::vector<bool> active, custom;
        BuildChipModel(*csv, keywords, labels, active, custom);

        std::vector<float> widths;
        widths.reserve(labels.size() + 1);
        for (size_t i = 0; i < labels.size(); ++i)
            widths.push_back(ChipWidth(labels[i].c_str()) + (custom[i] ? 14.0f : 0.0f));
        widths.push_back(ChipWidth("Add custom") + 6.0f);

        const ChipLayout layout = LayoutChips(widths, available);

        Row row = BeginRow(label, header + layout.height + 12.0f);
        const Palette& c = Colors();

        TextEllipsis(row.dl, FontRegular(), kLabelSize, ImVec2(row.min.x, row.min.y + 4.0f),
                     row.width, c.text, label);
        if (desc && *desc)
        {
            TextEllipsis(row.dl, FontRegular(), kDescSize, ImVec2(row.min.x, row.min.y + 21.0f),
                         row.width, c.textMute, desc);
        }

        bool changed = false;
        std::vector<std::string> selected = SplitCsv(*csv);
        const float chipTop = row.min.y + header;

        for (size_t i = 0; i < labels.size(); ++i)
        {
            const ChipLayout::Slot& slot = layout.slots[i];
            const ImVec2 min(row.min.x + slot.x, chipTop + slot.y);
            const ImVec2 max(min.x + slot.w, min.y + kChipHeight);

            const bool hovered = row.hovered && ImGui::IsMouseHoveringRect(min, max, false);

            ImGui::PushID(static_cast<int>(i + 1));
            const ImGuiID id = ImGui::GetID("##chip");
            ImGui::PopID();

            struct ChipState { float on = 0.0f; float hover = 0.0f; };
            ChipState* s = State<ChipState>(id);
            Ease(s->on, active[i] ? 1.0f : 0.0f, 16.0f);
            Ease(s->hover, hovered ? 1.0f : 0.0f, 14.0f);

            if (hovered && row.pressed)
            {
                if (custom[i])
                {
                    // Custom entries are removed outright.
                    for (size_t k = 0; k < selected.size(); ++k)
                    {
                        if (EqualsFold(selected[k], keywords[i]))
                        {
                            selected.erase(selected.begin() + static_cast<long>(k));
                            break;
                        }
                    }
                }
                else if (active[i])
                {
                    for (size_t k = 0; k < selected.size(); ++k)
                    {
                        if (EqualsFold(selected[k], keywords[i]))
                        {
                            selected.erase(selected.begin() + static_cast<long>(k));
                            break;
                        }
                    }
                }
                else
                {
                    selected.push_back(keywords[i]);
                }
                changed = true;
            }

            row.dl->AddRectFilled(min, max,
                                  Mix(Mix(c.widget, c.widgetHover, s->hover), c.accent, s->on),
                                  kChipHeight * 0.5f);
            row.dl->AddRect(min, max,
                            Mix(Alpha(c.border, 0.9f), c.accent, s->on),
                            kChipHeight * 0.5f, 0, 1.0f);

            const ImU32 textCol = active[i] ? c.onAccent : Mix(c.textDim, c.text, s->hover);
            const float textLeft = min.x + 12.0f;
            Text(row.dl, FontRegular(), 11.0f,
                 ImVec2(textLeft, (min.y + max.y) * 0.5f - 6.0f), textCol, labels[i].c_str());

            if (custom[i])
            {
                DrawIcon(row.dl, Icon::Close, ImVec2(max.x - 11.0f, (min.y + max.y) * 0.5f),
                         8.0f, textCol, 1.5f);
            }
        }

        // ---- add custom chip
        {
            const ChipLayout::Slot& slot = layout.slots.back();
            const ImVec2 min(row.min.x + slot.x, chipTop + slot.y);
            const ImVec2 max(min.x + slot.w, min.y + kChipHeight);

            const bool hovered = row.hovered && ImGui::IsMouseHoveringRect(min, max, false);
            if (hovered && row.pressed)
                ImGui::OpenPopup("##additem");

            ImGui::PushID("addcustom");
            const ImGuiID id = ImGui::GetID("##add");
            ImGui::PopID();

            struct AddState { float hover = 0.0f; char buffer[48] = {}; };
            AddState* s = State<AddState>(id);
            Ease(s->hover, hovered ? 1.0f : 0.0f, 14.0f);

            row.dl->AddRectFilled(min, max, Alpha(c.widget, 0.55f + s->hover * 0.45f),
                                  kChipHeight * 0.5f);
            row.dl->AddRect(min, max, Alpha(c.border, 0.85f), kChipHeight * 0.5f, 0, 1.0f);
            DrawIcon(row.dl, Icon::Plus, ImVec2(min.x + 13.0f, (min.y + max.y) * 0.5f),
                     9.0f, Mix(c.textMute, c.text, s->hover), 1.7f);
            Text(row.dl, FontRegular(), 11.0f,
                 ImVec2(min.x + 23.0f, (min.y + max.y) * 0.5f - 6.0f),
                 Mix(c.textMute, c.text, s->hover), "Add custom");

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
            ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(c.panel));
            ImGui::SetNextWindowPos(ImVec2(min.x, max.y + 6.0f));
            if (ImGui::BeginPopup("##additem"))
            {
                ImGui::SetNextItemWidth(180.0f);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(c.widget));

                const bool submitted = ImGui::InputTextWithHint(
                    "##name", "item keyword", s->buffer, sizeof(s->buffer),
                    ImGuiInputTextFlags_EnterReturnsTrue);

                ImGui::PopStyleColor();
                ImGui::SameLine(0.0f, 8.0f);

                const float frameH = ImGui::GetFrameHeight();
                const bool add = PillButton("##addbtn", "Add", ImVec2(56.0f, frameH), true);

                if ((submitted || add) && s->buffer[0] != '\0')
                {
                    const std::string entry = TrimCopy(s->buffer);
                    if (!entry.empty() && !ListContains(selected, entry))
                    {
                        selected.push_back(entry);
                        changed = true;
                    }
                    s->buffer[0] = '\0';
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }

        if (changed)
            *csv = JoinCsv(selected);

        EndRow(row, true);
        return changed;
    }

    bool RowAction(const char* label, const char* desc, const char* buttonText)
    {
        Row row = BeginRow(label, RowHeight(desc, 40.0f, 50.0f));
        const Palette& c = Colors();

        struct S { float hover = 0.0f; float press = 1.0f; };
        S* s = State<S>(row.id);

        const float buttonWidth = std::max(72.0f,
            TextSize(FontRegular(), kValueSize, buttonText).x + 26.0f);
        const ImVec2 btnMax(row.max.x, (row.min.y + row.max.y) * 0.5f + 13.0f);
        const ImVec2 btnMin(btnMax.x - buttonWidth, btnMax.y - 26.0f);

        const bool btnHovered = row.hovered && ImGui::IsMouseHoveringRect(btnMin, btnMax, false);
        const bool fired = row.pressed && btnHovered;

        Ease(s->hover, btnHovered ? 1.0f : 0.0f, 14.0f);
        Ease(s->press, (row.active && btnHovered) ? 0.96f : 1.0f, 18.0f);

        DrawRowLabel(row, label, desc, c.text, buttonWidth + 12.0f);

        const ImVec2 center((btnMin.x + btnMax.x) * 0.5f, (btnMin.y + btnMax.y) * 0.5f);
        const ImVec2 half((btnMax.x - btnMin.x) * 0.5f * s->press,
                          (btnMax.y - btnMin.y) * 0.5f * s->press);
        const ImVec2 aMin(center.x - half.x, center.y - half.y);
        const ImVec2 aMax(center.x + half.x, center.y + half.y);

        row.dl->AddRectFilled(aMin, aMax, Mix(c.accent, IM_COL32(255, 255, 255, 255), s->hover * 0.12f), 8.0f);
        TextIn(row.dl, FontRegular(), kValueSize, aMin, aMax, c.onAccent, buttonText, ImVec2(0.5f, 0.5f));

        EndRow(row, true);
        return fired;
    }

    // -------------------------------------------------------------- chrome

    bool ExpandingTab(const char* id, const char* label, Icon icon, bool selected, float height)
    {
        ImGui::PushID(id);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const Palette& c = Colors();

        const ImGuiID wid = ImGui::GetID("##tab");
        struct S { float expand = 0.0f; float hover = 0.0f; float press = 1.0f; };
        S* s = State<S>(wid);

        const float iconBox = height;
        const float labelW = label && *label
            ? TextSize(FontRegular(), 13.0f, label).x + 14.0f
            : 0.0f;
        Ease(s->expand, selected ? 1.0f : 0.0f, 16.0f);
        const float width = iconBox + labelW * s->expand + 4.0f * s->expand;

        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max(min.x + width, min.y + height);

        ImGui::InvisibleButton("##tab", ImVec2(width, height));
        const bool hovered = ImGui::IsItemHovered();
        const bool pressed = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        Ease(s->hover, hovered ? 1.0f : 0.0f, 14.0f);
        // shadcn-like active:scale — subtle press feedback on shell tabs
        Ease(s->press, ImGui::IsItemActive() ? 0.94f : 1.0f, 20.0f);

        const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        const ImVec2 half((max.x - min.x) * 0.5f * s->press,
                          (max.y - min.y) * 0.5f * s->press);
        const ImVec2 aMin(center.x - half.x, center.y - half.y);
        const ImVec2 aMax(center.x + half.x, center.y + half.y);

        const float round = height * 0.42f;
        dl->AddRectFilled(aMin, aMax,
                          Mix(Mix(c.widget, c.widgetHover, s->hover),
                              Mix(c.accent, IM_COL32(255, 255, 255, 255), 0.08f), s->expand),
                          round);

        const ImU32 tint = s->expand > 0.55f
            ? c.onAccent
            : Mix(c.textDim, c.text, std::max(0.55f, std::max(s->hover, s->expand)));
        DrawIcon(dl, icon,
                 ImVec2(aMin.x + iconBox * 0.5f * s->press, center.y),
                 18.0f * s->press, tint, 1.8f, s->expand > 0.55f);

        if (s->expand > 0.05f && label && *label)
        {
            const ImU32 textCol = Alpha(tint, Clamp01(s->expand));
            Text(dl, FontRegular(), 13.0f,
                 ImVec2(aMin.x + iconBox * s->press - 2.0f, center.y - 7.0f),
                 textCol, label);
        }

        ImGui::PopID();
        return pressed;
    }

    bool CategoryRailItem(const char* label, Icon icon, bool selected, float railWidth, float height)
    {
        ImGui::PushID(label);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const Palette& c = Colors();

        const ImGuiID wid = ImGui::GetID("##cat");
        struct S { float sel = 0.0f; float hover = 0.0f; };
        S* s = State<S>(wid);
        Ease(s->sel, selected ? 1.0f : 0.0f, 16.0f);

        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max(min.x + railWidth, min.y + height);

        ImGui::InvisibleButton("##cat", ImVec2(railWidth, height));
        const bool hovered = ImGui::IsItemHovered();
        const bool pressed = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        Ease(s->hover, hovered ? 1.0f : 0.0f, 14.0f);

        if (s->sel > 0.01f || s->hover > 0.01f)
        {
            dl->AddRectFilled(min, max,
                              Mix(Alpha(c.widgetHover, s->hover * 0.85f),
                                  Alpha(c.accent, 0.18f), s->sel),
                              10.0f);
        }

        // Selected underline grows from the label center (tabs indicator feel).
        if (s->sel > 0.01f)
        {
            const float underlineW = TextSize(FontRegular(), 13.0f, label).x + 28.0f;
            const float midX = min.x + 14.0f + underlineW * 0.5f;
            const float halfW = underlineW * 0.5f * s->sel;
            dl->AddRectFilled(ImVec2(midX - halfW, max.y - 3.0f),
                              ImVec2(midX + halfW, max.y - 1.0f),
                              Alpha(c.accent, s->sel), 1.0f);
        }

        const ImU32 tint = Mix(c.textDim, c.text, std::max(0.55f, std::max(s->sel, s->hover)));
        DrawIcon(dl, icon, ImVec2(min.x + 22.0f, (min.y + max.y) * 0.5f),
                 18.0f, tint, 2.0f, selected);
        TextEllipsis(dl, FontRegular(), 13.0f,
                     ImVec2(min.x + 40.0f, (min.y + max.y) * 0.5f - 7.0f),
                     railWidth - 50.0f, tint, label);

        ImGui::PopID();
        return pressed;
    }

    bool NavItem(const char* label, Icon icon, bool selected, float width, float height)
    {
        ImGui::PushID(label);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const Palette& c = Colors();

        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max(min.x + width, min.y + height);

        const ImGuiID id = ImGui::GetID("##nav");
        ImGui::InvisibleButton("##nav", ImVec2(width, height));
        const bool hovered = ImGui::IsItemHovered();
        const bool pressed = ImGui::IsItemClicked(ImGuiMouseButton_Left);

        struct S { float sel = 0.0f; float hover = 0.0f; };
        S* s = State<S>(id);
        Ease(s->sel, selected ? 1.0f : 0.0f, 15.0f);
        Ease(s->hover, hovered ? 1.0f : 0.0f, 14.0f);

        if (s->hover > 0.01f || s->sel > 0.01f)
        {
            dl->AddRectFilled(min, max,
                              Mix(Alpha(c.widgetHover, s->hover * 0.85f),
                                  Alpha(c.accent, 0.16f), s->sel),
                              10.0f);
        }
        if (s->sel > 0.01f)
        {
            const float barHeight = height * 0.46f * s->sel;
            const float centerY = (min.y + max.y) * 0.5f;
            dl->AddRectFilled(ImVec2(min.x - 1.0f, centerY - barHeight * 0.5f),
                              ImVec2(min.x + 2.5f, centerY + barHeight * 0.5f),
                              c.accent, 2.0f);
        }

        const ImU32 tint = Mix(c.textDim, c.text, std::max(s->sel, s->hover));
        DrawIcon(dl, icon, ImVec2(min.x + 24.0f, (min.y + max.y) * 0.5f), 16.0f, tint, 2.0f);
        TextEllipsis(dl, FontRegular(), kLabelSize,
                     ImVec2(min.x + 42.0f, (min.y + max.y) * 0.5f - kLabelSize * 0.5f - 1.0f),
                     width - 50.0f, tint, label);

        ImGui::PopID();
        return pressed;
    }

    bool SearchField(const char* id, char* buffer, size_t bufferSize, float width, float height)
    {
        ImGui::PushID(id);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const Palette& c = Colors();

        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max(min.x + width, min.y + height);

        dl->AddRectFilled(min, max, c.widget, height * 0.5f);
        dl->AddRect(min, max, Alpha(c.border, 0.9f), height * 0.5f, 0, 1.0f);
        DrawIcon(dl, Icon::Search, ImVec2(min.x + 17.0f, (min.y + max.y) * 0.5f), 14.0f, c.textMute, 1.8f);

        const ImVec2 restore = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(min.x + 31.0f,
                                         (min.y + max.y) * 0.5f - ImGui::GetFrameHeight() * 0.5f));
        ImGui::SetNextItemWidth(width - 42.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImGui::ColorConvertU32ToFloat4(c.textMute));

        const bool changed = ImGui::InputTextWithHint("##search", "Search modules", buffer, bufferSize);

        ImGui::PopStyleColor(4);
        ImGui::SetCursorScreenPos(restore);
        ImGui::Dummy(ImVec2(width, height));

        ImGui::PopID();
        return changed;
    }

    bool IconButton(const char* id, Icon icon, float size, bool active)
    {
        ImGui::PushID(id);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const Palette& c = Colors();

        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max(min.x + size, min.y + size);

        const ImGuiID wid = ImGui::GetID("##ib");
        ImGui::InvisibleButton("##ib", ImVec2(size, size));
        const bool hovered = ImGui::IsItemHovered();
        const bool pressed = ImGui::IsItemClicked(ImGuiMouseButton_Left);

        struct S { float hover = 0.0f; float on = 0.0f; float press = 1.0f; };
        S* s = State<S>(wid);
        Ease(s->hover, hovered ? 1.0f : 0.0f, 14.0f);
        Ease(s->on, active ? 1.0f : 0.0f, 15.0f);
        Ease(s->press, ImGui::IsItemActive() ? 0.92f : 1.0f, 18.0f);

        const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        const float half = size * 0.5f * s->press;
        dl->AddRectFilled(ImVec2(center.x - half, center.y - half),
                          ImVec2(center.x + half, center.y + half),
                          Mix(Mix(c.widget, c.widgetHover, s->hover), c.accent, s->on), 9.0f);

        DrawIcon(dl, icon, center, size * 0.48f,
                 s->on > 0.5f ? c.onAccent : Mix(c.textDim, c.text, s->hover), 1.8f);

        ImGui::PopID();
        return pressed;
    }

    bool MiniIconToggle(const char* id, Icon icon, ImVec2 min, ImVec2 max,
                        bool active, const char* tooltip)
    {
        ImGui::PushID(id);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const Palette& c = Colors();

        const ImGuiID wid = ImGui::GetID("##mini");
        // Rect-only hit test: SetTooltip() would otherwise steal IsWindowHovered
        // and make the toggle ignore clicks while the tip is visible.
        const bool hovered = ImGui::IsMouseHoveringRect(min, max, false);
        const bool pressed = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);

        struct S { float hover = 0.0f; float on = 0.0f; };
        S* s = State<S>(wid);
        Ease(s->hover, hovered ? 1.0f : 0.0f, 14.0f);
        Ease(s->on, active ? 1.0f : 0.0f, 15.0f);

        if (s->hover > 0.01f || s->on > 0.01f)
        {
            dl->AddRectFilled(min, max,
                              Mix(Alpha(c.widgetHover, s->hover), Alpha(c.accent, 0.75f), s->on), 6.0f);
        }

        DrawIcon(dl, icon, ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f),
                 (max.y - min.y) * 0.56f,
                 s->on > 0.5f ? c.onAccent : Mix(c.textMute, c.text, s->hover), 1.7f);

        if (hovered && tooltip && *tooltip)
            SetTooltip("%s", tooltip);

        ImGui::PopID();
        return pressed;
    }

    bool PillButton(const char* id, const char* label, ImVec2 size, bool filled)
    {
        ImGui::PushID(id);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const Palette& c = Colors();

        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max(min.x + size.x, min.y + size.y);

        const ImGuiID wid = ImGui::GetID("##pill");
        ImGui::InvisibleButton("##pill", size);
        const bool hovered = ImGui::IsItemHovered();
        const bool pressed = ImGui::IsItemClicked(ImGuiMouseButton_Left);

        struct S { float hover = 0.0f; float press = 1.0f; };
        S* s = State<S>(wid);
        Ease(s->hover, hovered ? 1.0f : 0.0f, 14.0f);
        Ease(s->press, ImGui::IsItemActive() ? 0.96f : 1.0f, 18.0f);

        const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        const ImVec2 half(size.x * 0.5f * s->press, size.y * 0.5f * s->press);
        const ImVec2 aMin(center.x - half.x, center.y - half.y);
        const ImVec2 aMax(center.x + half.x, center.y + half.y);

        if (filled)
        {
            dl->AddRectFilled(aMin, aMax,
                              Mix(c.accent, IM_COL32(255, 255, 255, 255), s->hover * 0.12f), 8.0f);
        }
        else
        {
            dl->AddRectFilled(aMin, aMax, Mix(c.widget, c.widgetHover, s->hover), 8.0f);
            dl->AddRect(aMin, aMax, Alpha(c.border, 0.9f), 8.0f, 0, 1.0f);
        }

        TextIn(dl, FontRegular(), kValueSize, aMin, aMax,
               filled ? c.onAccent : Mix(c.textDim, c.text, s->hover), label, ImVec2(0.5f, 0.5f));

        ImGui::PopID();
        return pressed;
    }

    void SectionLabel(const char* text, float paddingTop)
    {
        ImGui::Dummy(ImVec2(0.0f, paddingTop));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        Text(dl, FontRegular(), 10.0f, ImVec2(pos.x, pos.y), Colors().textMute, text);
        ImGui::Dummy(ImVec2(0.0f, 16.0f));
    }

    // -------------------------------------------------------------- toasts

    namespace
    {
        struct Toast
        {
            std::string title;
            std::string message;
            Icon icon = Icon::Info;
            ImU32 tint = 0;
            float life = 0.0f;
            float alpha = 0.0f;
            float slide = 0.0f;
        };

        std::vector<Toast>& Toasts()
        {
            static std::vector<Toast> list;
            return list;
        }

        // Notify() is called from the client tick thread (LatencyAlerts,
        // module fault handler) while DrawNotifications() iterates on the
        // render thread — an unguarded push_back can reallocate mid-iteration.
        std::mutex& ToastMutex()
        {
            static std::mutex m;
            return m;
        }

        constexpr float kToastDuration = 3.0f;
    }

    void Notify(const char* title, const char* message, Icon icon, ImU32 tint)
    {
        Toast toast;
        toast.title = title ? title : "";
        toast.message = message ? message : "";
        toast.icon = icon;
        toast.tint = tint ? tint : Colors().accent;
        toast.life = kToastDuration;

        std::lock_guard<std::mutex> lock(ToastMutex());
        Toasts().push_back(std::move(toast));

        if (Toasts().size() > 4)
            Toasts().erase(Toasts().begin());

        if (Prefs().notifyPlaySounds)
            MessageBeep(MB_OK);
    }

    void ClearNotifications()
    {
        std::lock_guard<std::mutex> lock(ToastMutex());
        Toasts().clear();
    }

    void DrawNotifications(ImVec2 anchorBottomRight)
    {
        std::lock_guard<std::mutex> lock(ToastMutex());
        auto& toasts = Toasts();
        if (toasts.empty())
            return;

        // Toasts also show while the menu is closed, so they must ignore the
        // menu's fade-out opacity.
        const float savedOpacity = GlobalOpacity();
        SetGlobalOpacity(1.0f);

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const Palette& c = Colors();
        const float dt = DeltaTime();
        const float width = 258.0f;
        const float height = 60.0f;
        // Master switch: keep aging the queue so nothing accumulates, but do
        // not draw while in-game notifications are disabled.
        const bool render = Prefs().notificationsEnabled;

        float offset = 0.0f;
        for (size_t i = toasts.size(); i-- > 0;)
        {
            Toast& toast = toasts[i];
            toast.life -= dt;

            const bool dying = toast.life <= 0.35f;
            Ease(toast.alpha, dying ? 0.0f : 1.0f, 12.0f);
            Ease(toast.slide, dying ? 26.0f : 0.0f, 12.0f);

            if (toast.life <= -0.4f)
            {
                toasts.erase(toasts.begin() + static_cast<long>(i));
                continue;
            }

            if (!render)
                continue;

            const ImVec2 max(anchorBottomRight.x + toast.slide, anchorBottomRight.y - offset);
            const ImVec2 min(max.x - width, max.y - height);

            const float a = Clamp01(toast.alpha);
            dl->AddShadowRect(min, max, IM_COL32(0, 0, 0, static_cast<int>(120 * a)), 26.0f,
                              ImVec2(0.0f, 6.0f), ImDrawFlags_ShadowCutOutShapeBackground, 11.0f);
            dl->AddRectFilled(min, max, Alpha(c.panel, a), 11.0f);
            dl->AddRect(min, max, Alpha(c.border, a), 11.0f, 0, 1.0f);

            // Tinted icon badge instead of a flat bar down the left edge.
            const ImVec2 badgeMin(min.x + 14.0f, (min.y + max.y) * 0.5f - 15.0f);
            const ImVec2 badgeMax(badgeMin.x + 30.0f, badgeMin.y + 30.0f);
            dl->AddRectFilled(badgeMin, badgeMax, Alpha(toast.tint, a * 0.20f), 9.0f);
            DrawIcon(dl, toast.icon,
                     ImVec2((badgeMin.x + badgeMax.x) * 0.5f, (badgeMin.y + badgeMax.y) * 0.5f),
                     16.0f, Alpha(toast.tint, a), 2.0f);

            TextEllipsis(dl, FontRegular(), kLabelSize, ImVec2(badgeMax.x + 12.0f, min.y + 13.0f),
                         width - 70.0f, Alpha(c.text, a), toast.title.c_str());
            TextEllipsis(dl, FontRegular(), kDescSize, ImVec2(badgeMax.x + 12.0f, min.y + 32.0f),
                         width - 70.0f, Alpha(c.textMute, a), toast.message.c_str());

            // Remaining-time bar.
            const float progress = Clamp01(toast.life / kToastDuration);
            dl->AddRectFilled(ImVec2(min.x + 1.0f, max.y - 3.0f),
                              ImVec2(min.x + 1.0f + (width - 2.0f) * progress, max.y - 1.0f),
                              Alpha(toast.tint, a * 0.75f), 1.0f);

            offset += height + 9.0f;
        }

        SetGlobalOpacity(savedOpacity);
    }
}
