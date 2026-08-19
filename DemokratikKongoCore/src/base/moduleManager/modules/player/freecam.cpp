#include "freecam.h"
#include "../../../patcher/patcher.h"
#include "../../../sdk/sdk.h"
#include "../../../util/logger.h"
#include "../../../menu/menu.h"
#include "../../../gui/guiWidgets.h"
#include "../../../gui/guiCore.h"
#include <Windows.h>
#include <cstdio>

namespace {
    inline std::string bstr(bool v) { return v ? "true" : "false"; }
    inline std::string fstr(float v) { char b[32]; std::snprintf(b, sizeof(b), "%.2f", v); return b; }
}

Freecam::Freecam()
    : Module("Freecam", "Detach camera and fly freely; body stays in place.", Category::Utility)
{
    m_speed = &add<NumberSetting>("Speed", 2.5f, 0.5f, 10.0f, 0.5f);
    m_disableOnDamage = &add<BoolSetting>("Disable On Damage", false);
    m_allowDig = &add<BoolSetting>("Allow Digging", false);
    m_allowPlace = &add<BoolSetting>("Allow Placing", false);
    m_allowInteract = &add<BoolSetting>("Allow Interacting", false);
    setEnabled(false);
}

void Freecam::onEnable()
{
    Logger::Info("Freecam", "Enabled");
    // Clear any stale damage marker so a fresh enable is never insta-killed.
    Patcher::putForce("freecam_damage_off", "false");
    pushAll();
}

void Freecam::onDisable()
{
    Logger::Info("Freecam", "Disabled");
    Patcher::put("freecam_enabled", "false");
    Patcher::put("freecam_forward", "0");
    Patcher::put("freecam_strafe", "0");
    Patcher::put("freecam_vertical", "0");
}

void Freecam::onTick()
{
    if (!isEnabled()) {
        Patcher::put("freecam_enabled", "false");
        return;
    }

    // FreecamBridge sets this marker when Disable On Damage fires; flip the
    // real module state off so the ghost is not re-created next tick.
    if (Patcher::get("freecam_damage_off") == "true") {
        Patcher::putForce("freecam_damage_off", "false");
        Logger::Info("Freecam", "Disabled on damage");
        Gui::Notify("Freecam", "Disabled on damage", Gui::Icon::Warning, Gui::Colors().danger);
        setEnabled(false);
        return;
    }

    if (Menu::Open) {
        pushAll();
        return;
    }

    float forward = 0.f, strafe = 0.f, vertical = 0.f;
    if (GetAsyncKeyState('W') & 0x8000) forward += 1.f;
    if (GetAsyncKeyState('S') & 0x8000) forward -= 1.f;
    if (GetAsyncKeyState('A') & 0x8000) strafe += 1.f;
    if (GetAsyncKeyState('D') & 0x8000) strafe -= 1.f;
    if (GetAsyncKeyState(VK_SPACE) & 0x8000) vertical += 1.f;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) vertical -= 1.f;

    Patcher::put("freecam_forward", fstr(forward));
    Patcher::put("freecam_strafe", fstr(strafe));
    Patcher::put("freecam_vertical", fstr(vertical));
    pushAll();
}

void Freecam::pushAll()
{
    Patcher::put("freecam_enabled", bstr(isEnabled()));
    Patcher::put("freecam_speed", fstr(m_speed ? m_speed->value : 2.5f));
    Patcher::put("freecam_disable_on_damage", bstr(m_disableOnDamage && m_disableOnDamage->value));
    Patcher::put("freecam_allow_dig", bstr(m_allowDig && m_allowDig->value));
    Patcher::put("freecam_allow_place", bstr(m_allowPlace && m_allowPlace->value));
    Patcher::put("freecam_allow_interact", bstr(m_allowInteract && m_allowInteract->value));
}
