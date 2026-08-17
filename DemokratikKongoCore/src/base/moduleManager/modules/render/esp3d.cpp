#include "esp3d.h"

#include "../combat/friends.h"
#include "../../../patcher/patcher.h"
#include "../../../util/format.h"
#include "../../../util/logger.h"

#include <sstream>
#include <string>

namespace
{
    std::string friendsCsv()
    {
        std::ostringstream oss;
        bool first = true;
        for (const auto& name : Friends::List)
        {
            if (name.empty()) continue;
            if (!first) oss << ',';
            oss << name;
            first = false;
        }
        return oss.str();
    }

    int javaModeFromIndex(int index)
    {
        (void)index;
        return 0;
    }
}

Esp3D::Esp3D()
    : Module("PlayerESP",
             "Minecraft player hitbox ESP rendered in world-space.",
             Category::Render)
{
    m_playerColor = &add<ColorSetting>("Player Color", Color{ 0.12f, 0.95f, 0.95f, 0.95f });
    m_mode = &add<EnumSetting>("Mode", std::vector<const char*>{ "Hitbox" }, 0);
    m_invisibles = &add<BoolSetting>("Invisibles", false);
    m_hideBots = &add<BoolSetting>("Hide Bots", true);

    setEnabled(false);
}

void Esp3D::onEnable()
{
    Logger::Info("PlayerESP", "Enabled — Java world pass (EspBridge hitbox mode)");
    pushAll();
}

void Esp3D::onDisable()
{
    Patcher::put("esp_enabled", "false");
    Logger::Info("PlayerESP", "Disabled");
}

void Esp3D::onTick()
{
    if (!isEnabled()) return;
    pushAll();
}

void Esp3D::pushAll()
{
    const Color& c = m_playerColor->value;

    Patcher::put("esp_enabled",   bstr(isEnabled()));
    Patcher::put("esp_mode",      std::to_string(javaModeFromIndex(m_mode->index)));
    Patcher::put("esp_color_r",   fstr(c.r));
    Patcher::put("esp_color_g",   fstr(c.g));
    Patcher::put("esp_color_b",   fstr(c.b));
    Patcher::put("esp_color_a",   fstr(c.a));
    Patcher::put("esp_invisibles", bstr(m_invisibles->value));
    Patcher::put("esp_hide_bots", bstr(m_hideBots->value));
    Patcher::put("esp_friends",   friendsCsv());
}
