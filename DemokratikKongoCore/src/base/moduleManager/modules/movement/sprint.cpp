#include "sprint.h"
#include "../../../patcher/patcher.h"
#include "../../../util/logger.h"

Sprint::Sprint()
    : Module("Sprint", "Automatically sprint when moving forward.", Category::Movement)
{
    setEnabled(false);
}

void Sprint::onEnable()
{
    Patcher::put("sprint_enabled", "true");
    Logger::Info("Sprint", "Enabled");
}

void Sprint::onDisable()
{
    Patcher::put("sprint_enabled", "false");
    Logger::Info("Sprint", "Disabled");
}

void Sprint::onTick()
{
    // RuntimeBridge.handleSprintKey reads sprint_enabled on the client
    // walking-update path. Keep the flag in sync every tick.
    Patcher::put("sprint_enabled", isEnabled() ? "true" : "false");
}
