#include "trajectories.h"

#include "../../../patcher/patcher.h"
#include "../../../util/format.h"
#include "../../../util/logger.h"

#include <string>

Trajectories::Trajectories()
    : Module("Trajectories", "Predict projectile flight paths (bow, ender pearl).", Category::Render)
{
    m_color       = &add<ColorSetting> ("Color",        Color{ 1.0f, 1.0f, 1.0f, 0.85f });
    m_lineWidth   = &add<NumberSetting>("Line Width",   2.0f, 1.0f, 5.0f, 0.5f);
    m_showLanding = &add<BoolSetting>  ("Show Landing", true);
    m_showBow      = &add<BoolSetting>  ("Show Bows",     true);
    m_showPearl    = &add<BoolSetting>  ("Show Pearls",   true);
    m_showSnowball = &add<BoolSetting>  ("Show Snowballs & Eggs", true);

    m_lineWidth->suffix = "px";
    m_showLanding->description = "Draw a small cross at the predicted landing point.";

    setEnabled(false);
}

void Trajectories::onEnable()
{
    Logger::Info("Trajectories", "Enabled");
    m_dirty = true;
    pushAll();
}

void Trajectories::onDisable()
{
    Patcher::put("trajectories_enabled", "false");
    Logger::Info("Trajectories", "Disabled");
}

void Trajectories::onTick()
{
    if (!isEnabled()) return;

    const Color& c = m_color->value;
    if (c.r != m_lastColor.r || c.g != m_lastColor.g ||
        c.b != m_lastColor.b || c.a != m_lastColor.a ||
        m_lineWidth->value != m_lastLineWidth ||
        m_showLanding->value != m_lastShowLanding ||
        m_showBow->value != m_lastShowBow ||
        m_showPearl->value != m_lastShowPearl ||
        m_showSnowball->value != m_lastShowSnowball)
    {
        m_dirty = true;
    }

    if (!m_dirty) return;
    pushAll();
    m_dirty = false;

    m_lastColor = c;
    m_lastLineWidth = m_lineWidth->value;
    m_lastShowLanding = m_showLanding->value;
    m_lastShowBow = m_showBow->value;
    m_lastShowPearl = m_showPearl->value;
    m_lastShowSnowball = m_showSnowball->value;
}

void Trajectories::pushAll()
{
    const Color& c = m_color->value;

    Patcher::put("trajectories_enabled",      bstr(isEnabled()));
    Patcher::put("trajectories_color_r",      fstr(c.r));
    Patcher::put("trajectories_color_g",      fstr(c.g));
    Patcher::put("trajectories_color_b",      fstr(c.b));
    Patcher::put("trajectories_color_a",      fstr(c.a));
    Patcher::put("trajectories_line_width",   fstr(m_lineWidth->value));
    Patcher::put("trajectories_show_landing", bstr(m_showLanding->value));
    Patcher::put("trajectories_show_bow",      bstr(m_showBow->value));
    Patcher::put("trajectories_show_pearl",    bstr(m_showPearl->value));
    Patcher::put("trajectories_show_snowball", bstr(m_showSnowball->value));
}
