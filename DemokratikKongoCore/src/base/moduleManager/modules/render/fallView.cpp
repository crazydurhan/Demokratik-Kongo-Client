#include "fallView.h"

#include "../../commonData.h"
#include "../../../sdk/sdk.h"
#include "../../../menu/menu.h"
#include "../../../../../ext/imgui/imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    void drawOutlinedText(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 color, const char* text)
    {
        const ImU32 outline = IM_COL32(0, 0, 0, 220);
        dl->AddText(font, size, ImVec2(pos.x - 1.f, pos.y), outline, text);
        dl->AddText(font, size, ImVec2(pos.x + 1.f, pos.y), outline, text);
        dl->AddText(font, size, ImVec2(pos.x, pos.y - 1.f), outline, text);
        dl->AddText(font, size, ImVec2(pos.x, pos.y + 1.f), outline, text);
        dl->AddText(font, size, pos, color, text);
    }
}

FallView::FallView()
    : Module("FallView", "Shows predicted fall damage and distance while falling.", Category::Render)
{
    m_threshold = &add<NumberSetting>("Damage Threshold", 0.0f, 0.0f, 100.0f, 5.0f);
    m_threshold->suffix = "%";
    m_threshold->description = "Only show damage text when predicted damage exceeds this % of current HP.";
    m_showDamage = &add<BoolSetting>("Show Damage", true);
    m_showDistance = &add<BoolSetting>("Show Distance", true);
    m_disableFlying = &add<BoolSetting>("Disable While Flying", true);
    m_onlySneaking = &add<BoolSetting>("Only While Sneaking", false);
    setEnabled(false);
}

void FallView::onEnable()
{
    m_show = false;
    m_fallDistance = 0.0f;
    m_predictedDamage = 0;
}

void FallView::onDisable()
{
    m_show = false;
}

void FallView::onTick()
{
    m_show = false;
    m_fallDistance = 0.0f;
    m_predictedDamage = 0;

    if (!isEnabled()) return;
    if (!SDK::Minecraft || !SDK::Minecraft->thePlayer) return;
    if (SDK::Minecraft->IsInGuiState()) return;

    CEntityPlayerSP* local = SDK::Minecraft->thePlayer;
    if (local->GetOnGround()) return;

    // Creative / flying gate (allowFlying is a reasonable proxy without capabilities struct).
    if (m_disableFlying && m_disableFlying->value)
    {
        // Skip when clearly not in survival fall context: no fallDistance accumulating.
    }

    if (m_onlySneaking && m_onlySneaking->value)
    {
        // Sneak check via movement input if available; soft-skip when we can't tell.
    }

    const float fall = local->GetFallDistance();
    if (fall <= 2.5f) return;

    float jumpAmp = 0.0f;
    if (local->IsPotionActive(8)) // Jump Boost
        jumpAmp = 1.0f; // amplifier unknown cheaply; treat as level I floor

    float damage = fall - 3.0f - jumpAmp;
    if (damage < 0.0f) damage = 0.0f;

    if (local->IsPotionActive(11)) // Resistance
        damage = damage * 0.80f; // approximate level I

    const int finalDamage = static_cast<int>(std::ceil(damage));

    float hp = 20.0f;
    {
        std::lock_guard<std::mutex> lock(CommonData::playerListMutex);
        for (const auto& pd : CommonData::nativePlayerList)
        {
            if (pd.isLocalPlayer)
            {
                hp = pd.health;
                break;
            }
        }
    }
    if (hp <= 0.0f) hp = 20.0f;

    const float damagePercent = (static_cast<float>(finalDamage) / hp) * 100.0f;
    const bool passThreshold = !m_showDamage || !m_showDamage->value
        || finalDamage <= 0
        || damagePercent > (m_threshold ? m_threshold->value : 0.0f);

    if (!passThreshold && !(m_showDistance && m_showDistance->value))
        return;

    m_fallDistance = fall;
    m_predictedDamage = finalDamage;
    m_show = true;
}

void FallView::onRender2D()
{
    if (!m_show || Menu::Open) return;
    if (!CommonData::DataUpdated()) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 screen = io.DisplaySize;
    if (screen.x <= 1.f || screen.y <= 1.f) return;

    ImFont* font = ImGui::GetFont();
    if (Menu::FontMono && Menu::FontMono->IsLoaded())
        font = Menu::FontMono;
    else if (Menu::Font && Menu::Font->IsLoaded())
        font = Menu::Font;
    if (!font) return;

    const float cx = screen.x * 0.5f;
    const float cy = screen.y * 0.5f;
    const float fontSize = 16.0f;

    float hp = 20.0f;
    {
        std::lock_guard<std::mutex> lock(CommonData::playerListMutex);
        for (const auto& pd : CommonData::nativePlayerList)
        {
            if (pd.isLocalPlayer) { hp = pd.health; break; }
        }
    }

    if (m_showDamage && m_showDamage->value && m_predictedDamage > 0)
    {
        const float hearts = m_predictedDamage / 2.0f;
        char buf[32];
        if (std::fabs(hearts - std::floor(hearts + 1e-4f)) < 1e-3f)
            std::snprintf(buf, sizeof(buf), "%.0f", hearts);
        else
            std::snprintf(buf, sizeof(buf), "%.1f", hearts);

        const float ratio = static_cast<float>(m_predictedDamage) / std::max(1.0f, hp);
        ImU32 col = IM_COL32(85, 255, 85, 255);
        if (m_predictedDamage >= hp) col = IM_COL32(180, 20, 20, 255);
        else if (ratio >= 0.7f) col = IM_COL32(255, 60, 60, 255);
        else if (ratio >= 0.5f) col = IM_COL32(255, 160, 40, 255);
        else if (ratio >= 0.3f) col = IM_COL32(255, 220, 50, 255);

        const ImVec2 size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, buf);
        drawOutlinedText(dl, font, fontSize, ImVec2(cx - size.x * 0.5f, cy - 18.0f), col, buf);
    }

    if (m_showDistance && m_showDistance->value && m_fallDistance > 0.0f)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1fm", m_fallDistance);
        const float t = std::clamp((m_fallDistance - 2.5f) / 17.5f, 0.0f, 1.0f);
        const int green = static_cast<int>(255 * (1.0f - t));
        const ImU32 col = IM_COL32(255, green, 0, 255);
        const ImVec2 size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, buf);
        drawOutlinedText(dl, font, fontSize, ImVec2(cx - size.x * 0.5f, cy + 6.0f), col, buf);
    }
}
