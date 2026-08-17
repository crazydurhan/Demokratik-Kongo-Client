#include "healthDisplay.h"

#include "../../moduleManager.h"
#include "../../../menu/menu.h"
#include "../../commonData.h"
#include "../../../../../ext/imgui/imgui.h"

#include <cmath>
#include <cstdio>

namespace
{
    // Vape Health thresholds (hearts): >=7 green, >4 yellow, else red.
    constexpr ImU32 kColLow  = IM_COL32(255, 20, 20, 255);
    constexpr ImU32 kColMid  = IM_COL32(255, 249, 18, 255);
    constexpr ImU32 kColHigh = IM_COL32(2, 190, 58, 255);
    constexpr ImU32 kColAbs  = IM_COL32(255, 170, 0, 255); // §6 gold heart when absorption

    void drawOutlinedText(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 color, const char* text)
    {
        const ImU32 outline = IM_COL32(0, 0, 0, 220);
        dl->AddText(font, size, ImVec2(pos.x - 1.f, pos.y), outline, text);
        dl->AddText(font, size, ImVec2(pos.x + 1.f, pos.y), outline, text);
        dl->AddText(font, size, ImVec2(pos.x, pos.y - 1.f), outline, text);
        dl->AddText(font, size, ImVec2(pos.x, pos.y + 1.f), outline, text);
        dl->AddText(font, size, pos, color, text);
    }

    // Compact filled heart (Unicode ❤ substitute — Inter may lack dingbats).
    void drawHeart(ImDrawList* dl, ImVec2 center, float size, ImU32 color)
    {
        const float half = size * 0.5f;
        dl->PathClear();
        dl->PathLineTo(ImVec2(center.x, center.y + half));
        dl->PathBezierCubicCurveTo(
            ImVec2(center.x - size * 0.7f, center.y + size * 0.1f),
            ImVec2(center.x - size * 0.8f, center.y - size * 0.4f),
            ImVec2(center.x - size * 0.3f, center.y - size * 0.6f));
        dl->PathBezierCubicCurveTo(
            ImVec2(center.x - size * 0.1f, center.y - size * 0.7f),
            ImVec2(center.x, center.y - size * 0.4f),
            ImVec2(center.x, center.y - size * 0.3f));
        dl->PathBezierCubicCurveTo(
            ImVec2(center.x, center.y - size * 0.4f),
            ImVec2(center.x + size * 0.1f, center.y - size * 0.7f),
            ImVec2(center.x + size * 0.3f, center.y - size * 0.6f));
        dl->PathBezierCubicCurveTo(
            ImVec2(center.x + size * 0.8f, center.y - size * 0.4f),
            ImVec2(center.x + size * 0.7f, center.y + size * 0.1f),
            ImVec2(center.x, center.y + half));
        dl->PathFillConvex(color);
    }

    // Vape NumberFormat("#.#") + floor((hearts+0.25)/0.5)*0.5, strip trailing .0
    void formatHearts(char* out, size_t n, double hearts)
    {
        const double rounded = std::floor((hearts + 0.25) / 0.5) * 0.5;
        if (std::fabs(rounded - std::floor(rounded + 1e-9)) < 1e-6)
            std::snprintf(out, n, "%.0f", rounded);
        else
            std::snprintf(out, n, "%.1f", rounded);
    }
}

HealthDisplay::HealthDisplay()
    : Module("Health", "Displays your health in the center of your screen.", Category::Render)
{
    m_offsetY = &add<NumberSetting>("Offset Y", 10.0f, 0.0f, 100.0f, 1.0f);
    m_scale   = &add<NumberSetting>("Scale", 1.0f, 0.5f, 3.0f, 0.1f);

    m_offsetY->suffix = "px";
    m_offsetY->description = "Vertical distance below the crosshair center (Vape default: 10).";
    m_scale->description = "Text / heart size multiplier.";

    setEnabled(false);
}

void HealthDisplay::onRender2D()
{
    if (Menu::Open) return;
    if (!CommonData::DataUpdated()) return;

    float hp  = 0.0f;
    float abs = 0.0f;
    {
        std::lock_guard<std::mutex> lock(CommonData::playerListMutex);
        for (const auto& pd : CommonData::nativePlayerList) {
            if (pd.isLocalPlayer) {
                hp  = pd.health;
                abs = pd.absorptionAmount;
                break;
            }
        }
    }

    if (hp <= 0.0f && abs <= 0.0f) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 screen = io.DisplaySize;
    if (screen.x <= 1.f || screen.y <= 1.f) return;

    ImFont* font = nullptr;
    if (Menu::Font && Menu::Font->IsLoaded())
        font = Menu::Font;
    else
        font = ImGui::GetFont();
    if (!font) return;

    // Vape: hearts = health/2; absorption adds to displayed hearts and golds the heart.
    double hearts = static_cast<double>(hp) / 2.0;
    const bool hasAbs = abs > 0.0f;
    if (hasAbs)
        hearts += static_cast<double>(abs) / 2.0;

    char numBuf[16];
    formatHearts(numBuf, sizeof(numBuf), hearts);

    ImU32 textCol = kColLow;
    if (hearts >= 7.0)
        textCol = kColHigh;
    else if (hearts > 4.0)
        textCol = kColMid;

    const ImU32 heartCol = hasAbs ? kColAbs : kColLow;

    const float scale = m_scale->value;
    const float fontSize = 16.0f * scale;
    const float heartSize = 11.0f * scale;
    const float spacing = 4.0f * scale;
    const float offsetY = m_offsetY->value;

    const ImVec2 numSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, numBuf);
    const float totalW = numSize.x + spacing + heartSize;
    const float cx = screen.x * 0.5f;
    const float cy = screen.y * 0.5f;

    const float startX = cx - totalW * 0.5f;
    const float textY = cy + offsetY;
    const float textX = startX;
    const float heartX = startX + numSize.x + spacing + heartSize * 0.5f;
    const float heartY = textY + numSize.y * 0.5f;

    drawOutlinedText(dl, font, fontSize, ImVec2(textX, textY), textCol, numBuf);
    drawHeart(dl, ImVec2(heartX, heartY), heartSize, heartCol);
}
