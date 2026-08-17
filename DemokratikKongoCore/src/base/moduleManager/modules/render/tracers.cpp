#include "tracers.h"

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
}

Tracers::Tracers()
    : Module("Tracers", "Draw lines from crosshair to players.", Category::Render)
{
    m_invisibles = &add<BoolSetting>("Invisibles", false);
    m_colorByDistance = &add<BoolSetting>("Color By Distance", false);
    m_highlightFocusing = &add<BoolSetting>("Highlight If Focusing", false);
    m_hideBots = &add<BoolSetting>("Hide Bots", true);
    m_ignoreFriends = &add<BoolSetting>("Ignore Friends", true);
    m_color = &add<ColorSetting>("Color", Color{ 0.0f, 1.0f, 0.0f, 0.85f });

    setEnabled(false);
}

void Tracers::onEnable()
{
    Logger::Info("Tracers", "Enabled — Java world pass (EspBridge tracers)");
    pushAll();
}

void Tracers::onDisable()
{
    Patcher::put("tracers_enabled", "false");
    Logger::Info("Tracers", "Disabled");
}

void Tracers::onTick()
{
    if (!isEnabled()) return;
    pushAll();
}

void Tracers::pushAll()
{
    const Color& c = m_color->value;

    Patcher::put("tracers_enabled",           bstr(isEnabled()));
    Patcher::put("tracers_invisibles",        bstr(m_invisibles->value));
    Patcher::put("tracers_color_by_distance", bstr(m_colorByDistance->value));
    Patcher::put("tracers_highlight_focus",   bstr(m_highlightFocusing->value));
    Patcher::put("tracers_hide_bots",         bstr(m_hideBots->value));
    Patcher::put("tracers_ignore_friends",    bstr(m_ignoreFriends->value));
    Patcher::put("tracers_color_r",           fstr(c.r));
    Patcher::put("tracers_color_g",           fstr(c.g));
    Patcher::put("tracers_color_b",           fstr(c.b));
    Patcher::put("tracers_color_a",           fstr(c.a));
    Patcher::put("tracers_friends",           friendsCsv());
}
