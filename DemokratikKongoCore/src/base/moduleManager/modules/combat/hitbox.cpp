#include "hitbox.h"

#include "../../../patcher/patcher.h"
#include "../../../sdk/sdk.h"
#include "../../../util/format.h"
#include "../../../util/logger.h"

#include <string>

Hitbox::Hitbox()
    : Module("Hitbox", "Expand player hitboxes for easier hits.", Category::Combat)
{
    m_horizontalExpand = &add<NumberSetting>("Horizontal Expand", 0.15f, 0.0f, 1.0f, 0.05f);
    m_horizontalExpand->suffix = " blocks";
    m_verticalExpand = &add<NumberSetting>("Vertical Expand", 0.0f, 0.0f, 1.0f, 0.05f);
    m_verticalExpand->suffix = " blocks";
    m_onlyWhenMoving = &add<BoolSetting>("Only When Moving", false);
    m_onlyWhenMoving->description = "Only expand hitboxes while player is moving.";

    setEnabled(false);
}

void Hitbox::onEnable()
{
    pushAll();
    Logger::Info("Hitbox", "Enabled — persistent JVM boundingBox expansion");
}

void Hitbox::onDisable()
{
    Patcher::put("hitbox_enabled", "false");
    Patcher::put("hitbox_expand_h", "0");
    Patcher::put("hitbox_expand_v", "0");
    Logger::Info("Hitbox", "Disabled");
}

void Hitbox::onTick()
{
    if (isEnabled())
        pushAll();
}

void Hitbox::pushAll()
{
    bool active = isEnabled();
    if (active && m_onlyWhenMoving && m_onlyWhenMoving->value)
    {
        if (!SDK::Minecraft || !SDK::Minecraft->thePlayer || !SDK::Minecraft->thePlayer->IsMovingForward())
        {
            active = false;
        }
    }

    Patcher::put("hitbox_enabled",  bstr(active));
    Patcher::put("hitbox_expand_h",  fstr(active ? m_horizontalExpand->value : 0.0f));
    Patcher::put("hitbox_expand_v",  fstr(active ? m_verticalExpand->value : 0.0f));
}
