#include "freelook.h"

#include "../../../sdk/sdk.h"
#include "../../../patcher/patcher.h"
#include "../../../util/logger.h"
#include "../../../menu/menu.h"

#include <Windows.h>
#include <cstdio>

namespace
{
    inline std::string fstr(float v)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2f", v);
        return buf;
    }

    inline std::string bstr(bool v) { return v ? "true" : "false"; }
}

Freelook::Freelook()
    : Module("Freelook", "Hold/toggle free camera look without turning your player.", Category::Render)
{
    // Left Alt — matches raven default feel on Windows VK.
    m_key = &add<KeybindSetting>("Key", VK_LMENU);
    m_hold = &add<BoolSetting>("Hold", true);
    m_invertPitch = &add<BoolSetting>("Invert Pitch", false);
    m_lockPitch = &add<BoolSetting>("Lock Pitch", true);
    m_customFov = &add<BoolSetting>("Custom FOV", false);
    m_fov = &add<NumberSetting>("FOV", 90.0f, 30.0f, 150.0f, 1.0f);
    m_fov->visible = [this] { return m_customFov && m_customFov->value; };
    setEnabled(false);
}

bool Freelook::isKeyDown() const
{
    const int vk = m_key ? m_key->virtualKey : 0;
    if (vk <= 0) return false;
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

void Freelook::enterPerspective()
{
    if (!SDK::Minecraft || !SDK::Minecraft->gameSettings) return;
    m_savedPerspective = SDK::Minecraft->gameSettings->GetThirdPersonView();
    m_savedFov = SDK::Minecraft->gameSettings->GetFOV();
    SDK::Minecraft->gameSettings->SetThirdPersonView(1);
    if (m_customFov && m_customFov->value)
        SDK::Minecraft->gameSettings->SetFOV(m_fov->value);
    m_active = true;
}

void Freelook::resetPerspective()
{
    if (!m_active) return;
    if (SDK::Minecraft && SDK::Minecraft->gameSettings)
    {
        SDK::Minecraft->gameSettings->SetThirdPersonView(m_savedPerspective);
        SDK::Minecraft->gameSettings->SetFOV(m_savedFov);
    }
    m_active = false;
}

void Freelook::onEnable()
{
    Logger::Info("Freelook", "Enabled");
    m_prevKey = false;
    pushAll();
}

void Freelook::onDisable()
{
    Logger::Info("Freelook", "Disabled");
    resetPerspective();
    m_prevKey = false;
    Patcher::put("freelook_enabled", "false");
    Patcher::put("freelook_active", "false");
}

void Freelook::onTick()
{
    if (!isEnabled()) return;

    if (Menu::Open || (SDK::Minecraft && SDK::Minecraft->IsInGuiState()))
    {
        if (m_hold && m_hold->value && m_active)
            resetPerspective();
        pushAll();
        return;
    }

    const bool down = isKeyDown();
    if (down != m_prevKey)
    {
        if (down)
        {
            if (m_active)
                resetPerspective();
            else
                enterPerspective();
        }
        else if (m_hold && m_hold->value)
        {
            resetPerspective();
        }
        m_prevKey = down;
    }

    if (m_active && m_customFov && m_customFov->value && SDK::Minecraft && SDK::Minecraft->gameSettings)
        SDK::Minecraft->gameSettings->SetFOV(m_fov->value);

    pushAll();
}

void Freelook::pushAll()
{
    Patcher::put("freelook_enabled", bstr(isEnabled()));
    Patcher::put("freelook_active", bstr(isEnabled() && m_active));
    Patcher::put("freelook_invert_pitch", bstr(m_invertPitch && m_invertPitch->value));
    Patcher::put("freelook_lock_pitch", bstr(m_lockPitch && m_lockPitch->value));
}
