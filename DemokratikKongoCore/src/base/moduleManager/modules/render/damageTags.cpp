#include "damageTags.h"
#include "../../../patcher/patcher.h"
#include "../../../util/logger.h"
#include <cstdio>

namespace {
    inline std::string bstr(bool v) { return v ? "true" : "false"; }
    inline std::string fstr(float v) { char b[32]; std::snprintf(b, sizeof(b), "%.0f", v); return b; }
    inline std::string fstr2(float v) { char b[32]; std::snprintf(b, sizeof(b), "%.2f", v); return b; }
}

DamageTags::DamageTags()
    : Module("DamageTags", "Floating damage / heal numbers above entities.", Category::Render)
{
    m_duration = &add<NumberSetting>("Duration", 1000.0f, 200.0f, 3000.0f, 50.0f);
    m_duration->suffix = " ms";
    m_scale = &add<NumberSetting>("Scale", 1.0f, 0.5f, 2.5f, 0.1f);
    m_shadow = &add<BoolSetting>("Text Shadow", true);
    setEnabled(false);
}

void DamageTags::onEnable() { Logger::Info("DamageTags", "Enabled"); pushAll(); }
void DamageTags::onDisable() { Logger::Info("DamageTags", "Disabled"); pushAll(); }
void DamageTags::onTick() { if (isEnabled()) pushAll(); else Patcher::put("damagetags_enabled", "false"); }

void DamageTags::pushAll()
{
    Patcher::put("damagetags_enabled", bstr(isEnabled()));
    Patcher::put("damagetags_duration_ms", fstr(m_duration ? m_duration->value : 1000.f));
    Patcher::put("damagetags_scale", fstr2(m_scale ? m_scale->value : 1.f));
    Patcher::put("damagetags_shadow", bstr(m_shadow && m_shadow->value));
}
