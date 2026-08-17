#include "targetHud.h"
#include <Windows.h>
#include "../../../sdk/sdk.h"
#include "../../../sdk/strayCache.h"
#include "../../../sdk/net/minecraft/util/MovingObjectPosition.h"
#include "../../commonData.h"
#include "../../../java/java.h"
#include "../../../util/logger.h"
#include "../../../../../ext/imgui/imgui.h"
#include "../../../../../ext/imgui/imgui_internal.h"   // ImLerp
#include <algorithm>
#include <cmath>

TargetHUD::TargetHUD()
    : Module("TargetHUD", "Renders a modern HUD card displaying target player statistics.", Category::Render)
{
    m_posX = &add<NumberSetting>("X Pos", 500.0f, 0.0f, 1920.0f, 10.0f);
    m_posX->description = "X screen position of the TargetHUD card.";

    m_posY = &add<NumberSetting>("Y Pos", 350.0f, 0.0f, 1080.0f, 10.0f);
    m_posY->description = "Y screen position of the TargetHUD card.";

    m_showArmor = &add<BoolSetting>("Show Armor", true);
    m_showArmor->description = "Display armor status of target player.";

    setEnabled(false);
}

void TargetHUD::onEnable()
{
    Logger::Info("TargetHUD", "Enabled");
    m_hasTarget = false;
    m_lockedName.clear();
    m_prevAttackDown = false;
}

void TargetHUD::onDisable()
{
    Logger::Info("TargetHUD", "Disabled");
    m_hasTarget = false;
    m_lockedName.clear();
    m_prevAttackDown = false;
}

void TargetHUD::onTick()
{
    if (!isEnabled() || !SDK::Minecraft || !SDK::Minecraft->thePlayer)
    {
        m_hasTarget = false;
        m_lockedName.clear();
        m_prevAttackDown = false;
        return;
    }

    CEntityPlayerSP* thePlayer = SDK::Minecraft->thePlayer;
    if (!thePlayer->GetInstance())
    {
        m_hasTarget = false;
        m_lockedName.clear();
        m_prevAttackDown = false;
        return;
    }

    JNIEnv* env = Java::GetEnv();
    if (!env) return;

    // --- Lock-on detection: a fresh LMB press whose crosshair ray resolves to
    // a player marks that player as the HUD target. Using the rising edge of
    // the attack key matches both manual clicks and autoclicker hits, since
    // both route through Minecraft.clickMouse / objectMouseOver.
    const bool attackDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (attackDown && !m_prevAttackDown)
    {
        CMovingObjectPosition mouseOver = SDK::Minecraft->GetMouseOver();
        if (mouseOver.GetInstance() && mouseOver.IsTypeOfEntity())
        {
            CEntity hit = mouseOver.GetEntity();
            jobject hitObj = hit.GetInstance();
            if (hitObj && StrayCache::entityPlayer_class
                && env->IsInstanceOf(hitObj, StrayCache::entityPlayer_class)
                && !env->IsSameObject(hitObj, thePlayer->GetInstance()))
            {
                jstring nameJStr = (jstring)env->CallObjectMethod(hitObj, StrayCache::entity_getName);
                if (env->ExceptionCheck()) { env->ExceptionClear(); nameJStr = nullptr; }
                if (nameJStr)
                {
                    const char* nameChars = env->GetStringUTFChars(nameJStr, nullptr);
                    if (nameChars)
                    {
                        m_lockedName = nameChars;
                        env->ReleaseStringUTFChars(nameJStr, nameChars);
                    }
                    env->DeleteLocalRef(nameJStr);
                }
            }
        }
    }
    m_prevAttackDown = attackDown;

    if (m_lockedName.empty())
    {
        m_hasTarget = false;
        return;
    }

    // --- Resolve the locked player from the shared snapshot so we don't run
    // a per-tick JNI entity sweep of our own. The HUD keeps tracking the hit
    // player until they die or leave the world; hitting someone else relocks.
    Vector3 localPos = thePlayer->GetPos();

    const auto players = CommonData::SnapshotPlayers();
    const CommonData::PlayerSnapshot* target = nullptr;
    for (const auto& pd : players)
    {
        if (!pd.isLocalPlayer && pd.name == m_lockedName)
        {
            target = &pd;
            break;
        }
    }

    if (!target || target->health <= 0.0f)
    {
        m_hasTarget = false;
        m_lockedName.clear();
        return;
    }

    m_hasTarget = true;
    m_targetName = target->name;
    m_targetHealth = target->health;
    m_targetMaxHealth = target->maxHealth > 0.1f ? target->maxHealth : 20.0f;

    const double dx = target->pos.x - localPos.x;
    const double dy = target->pos.y - localPos.y;
    const double dz = target->pos.z - localPos.z;
    m_targetDistance = (float)std::sqrt(dx * dx + dy * dy + dz * dz);
}

void TargetHUD::onRender2D()
{
    if (!isEnabled() || !m_hasTarget) return;

    // Smooth health interpolation. A fixed per-frame blend converged twice as
    // fast at 120 FPS as at 60, so derive the blend factor from frame time
    // instead (~10 units/s time constant).
    const float dt = ImGui::GetIO().DeltaTime;
    const float alpha = 1.0f - std::exp(-10.0f * (dt > 0.0f ? dt : 0.0f));
    m_displayHealth = ImLerp(m_displayHealth, m_targetHealth, alpha);

    ImGui::SetNextWindowPos(ImVec2(m_posX->value, m_posY->value), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(220, 75), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;

    if (ImGui::Begin("TargetHUDCard", nullptr, flags))
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetWindowPos();
        ImVec2 s = ImGui::GetWindowSize();

        // Outer card background
        drawList->AddRectFilled(p, ImVec2(p.x + s.x, p.y + s.y), IM_COL32(20, 22, 28, 220), 8.0f);
        drawList->AddRect(p, ImVec2(p.x + s.x, p.y + s.y), IM_COL32(60, 70, 90, 180), 8.0f, 0, 1.5f);

        // Header: Target Name
        std::string title = m_targetName.empty() ? "Target Player" : m_targetName;
        drawList->AddText(ImVec2(p.x + 12, p.y + 8), IM_COL32(255, 255, 255, 255), title.c_str());

        // Distance text
        char distBuf[32];
        snprintf(distBuf, sizeof(distBuf), "%.1fm", m_targetDistance);
        drawList->AddText(ImVec2(p.x + s.x - 45, p.y + 8), IM_COL32(180, 190, 210, 220), distBuf);

        // Health Bar Track
        float barX = p.x + 12;
        float barY = p.y + 32;
        float barW = s.x - 24;
        float barH = 12;

        drawList->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH), IM_COL32(40, 45, 55, 255), 4.0f);

        // Health Fill
        float maxH = m_targetMaxHealth > 0.1f ? m_targetMaxHealth : 20.0f;
        float ratio = std::clamp(m_displayHealth / maxH, 0.0f, 1.0f);
        float fillW = barW * ratio;

        ImU32 healthColor = IM_COL32(46, 204, 113, 255); // Green
        if (ratio < 0.35f) healthColor = IM_COL32(231, 76, 60, 255); // Red
        else if (ratio < 0.65f) healthColor = IM_COL32(241, 196, 15, 255); // Yellow

        if (fillW > 0.0f)
        {
            drawList->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + fillW, barY + barH), healthColor, 4.0f);
        }

        // Health Numeric Text
        char hpBuf[32];
        snprintf(hpBuf, sizeof(hpBuf), "%.1f / %.0f HP", m_targetHealth, maxH);
        drawList->AddText(ImVec2(p.x + 12, p.y + 50), IM_COL32(200, 210, 225, 255), hpBuf);
    }
    ImGui::End();
}
