#pragma once

#include "guiCore.h"

#include <cstddef>
#include <string>

/*
========================================================================
    DemokratikKongo :: GUI widgets
------------------------------------------------------------------------
    Every "Row*" helper renders a full width settings row at the current
    cursor: label (+ optional description) on the left, control on the
    right, matching the reference menus we harvested the look from.

    Only the public ImGui API is used - hit testing is done manually
    against sub rectangles so a single InvisibleButton can host several
    interactive zones (row body, toggle, keybind chip, ...).
========================================================================
*/

namespace Gui
{
    // ----------------------------------------------------------- primitives

    void Panel(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 color,
               float rounding, bool shadow = false);

    void Checkerboard(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding);

    void Chevron(ImDrawList* dl, ImVec2 center, float size, float openAmount,
                 ImU32 color, float thickness = 2.0f);

    // Animated switch drawn inside an explicit rectangle.
    void DrawSwitch(ImGuiID id, ImVec2 min, ImVec2 max, bool value, bool hovered, bool pressed);

    // --------------------------------------------------------- setting rows

    bool RowToggle(const char* label, const char* desc, bool* value);

    bool RowSlider(const char* label, const char* desc, float* value,
                   float minValue, float maxValue, bool integer,
                   const char* suffix = "");

    // Discrete picker rendered as a row of numbered chips (hotbar style).
    bool RowSlotPicker(const char* label, const char* desc, float* value,
                       int firstSlot, int lastSlot);

    // Multi-select hotbar slot picker (chips 1-9); edits a CSV string.
    bool RowSlotMultiPicker(const char* label, const char* desc, std::string* csv);

    bool RowRange(const char* label, const char* desc, float* low, float* high,
                  float minValue, float maxValue, bool integer,
                  const char* suffix = "");

    bool RowDropdown(const char* label, const char* desc, int* index,
                     const char* const* items, int itemCount);

    bool RowKeybind(const char* label, const char* desc, int* virtualKey, bool* listening);

    bool RowColor(const char* label, const char* desc, float rgba[4]);

    bool RowTextInput(const char* label, const char* desc, std::string* value, size_t maxLength);

    // Comma separated keyword list rendered as preset chips plus user added
    // entries, with an "Add custom" affordance. Used by every "Limit to items"
    // style setting.
    bool RowItemList(const char* label, const char* desc, std::string* csv);

    // Height RowItemList will occupy for the given content width, so callers
    // can measure a panel before drawing it.
    float MeasureItemList(const char* desc, const std::string& csv, float width);

    bool RowAction(const char* label, const char* desc, const char* buttonText);

    // True when the most recently drawn settings row was right-clicked.
    // Used by the caller to reset that setting to its default value.
    bool RowRightClicked();

    // ------------------------------------------------------------ chrome

    // Top shell tabs (Home / Friends / Configs / Settings). Selected pill
    // expands horizontally to reveal the label; idle tabs stay icon-only.
    bool ExpandingTab(const char* id, const char* label, Icon icon, bool selected, float height);

    // Left category rail item while on Home. Selected shows icon + label
    // in a full-width pill; idle shows a compact icon square.
    bool CategoryRailItem(const char* label, Icon icon, bool selected, float railWidth, float height);

    bool NavItem(const char* label, Icon icon, bool selected, float width, float height);

    bool SearchField(const char* id, char* buffer, size_t bufferSize, float width, float height);

    bool IconButton(const char* id, Icon icon, float size, bool active = false);

    // Small square toggle used on module cards (e.g. ArrayList visibility).
    bool MiniIconToggle(const char* id, Icon icon, ImVec2 min, ImVec2 max,
                        bool active, const char* tooltip);

    bool PillButton(const char* id, const char* label, ImVec2 size, bool filled);

    void SectionLabel(const char* text, float paddingTop = 14.0f);

    // ----------------------------------------------------------- toasts

    void Notify(const char* title, const char* message, Icon icon, ImU32 tint);
    void DrawNotifications(ImVec2 anchorBottomRight);
    void ClearNotifications();
}
