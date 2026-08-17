#include "cameraClip.h"

#include "../../../patcher/patcher.h"
#include "../../../util/logger.h"

#include <cstdio>

namespace
{
    inline std::string fstr(float v)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2f", v);
        return buf;
    }
}

CameraClip::CameraClip()
    : Module("CameraClip", "Third-person camera passes through blocks and uses a custom distance.", Category::Render)
{
    m_distance = &add<NumberSetting>("Distance", 4.0f, 1.0f, 40.0f, 0.5f);
    m_distance->suffix = " blocks";
    m_distance->description = "Third-person camera pull-back distance (vanilla default is 4).";
    setEnabled(false);
}

std::string CameraClip::arrayListSuffix(SuffixDetail detail) const
{
    if (detail == SuffixDetail::None) return "";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1fb", m_distance->value);
    return buf;
}

void CameraClip::onEnable()
{
    Logger::Info("CameraClip", "Enabled");
    pushAll();
}

void CameraClip::onDisable()
{
    Logger::Info("CameraClip", "Disabled");
    // Restore vanilla distance so disabling does not leave an extended camera.
    Patcher::put("cameraclip_enabled", "false");
    Patcher::put("cameraclip_distance", "4.00");
}

void CameraClip::onTick()
{
    if (!isEnabled()) return;
    pushAll();
}

void CameraClip::pushAll()
{
    Patcher::put("cameraclip_enabled", isEnabled() ? "true" : "false");
    Patcher::put("cameraclip_distance", fstr(m_distance ? m_distance->value : 4.0f));
}
