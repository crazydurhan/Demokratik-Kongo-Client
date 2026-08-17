#include "delayRemover.h"

#include "../combat/combatBridge.h"
#include "../../../menu/menu.h"
#include "../../../sdk/sdk.h"
#include "../../../sdk/net/minecraft/client/Minecraft.h"
#include "../../../util/logger.h"

DelayRemover::DelayRemover()
    : Module("DelayRemover", "Remove or reduce certain Minecraft delays.", Category::Utility)
{
    m_noHitDelay = &add<BoolSetting>("NoHitDelay", true);
    m_noJumpDelay = &add<BoolSetting>("NoJumpDelay", true);
    setEnabled(false);
}

void DelayRemover::onEnable()
{
    Logger::Info("DelayRemover", "Enabled");
}

void DelayRemover::onDisable()
{
    Logger::Info("DelayRemover", "Disabled");
}

void DelayRemover::onTick()
{
    if (!isEnabled() || Menu::Open)
        return;

    if (!SDK::Minecraft || !SDK::Minecraft->IsReady())
        return;

    if (m_noHitDelay->value && SDK::Minecraft->GetLeftClickCounter() > 0)
        CombatBridge::RequestLeftClickCounterZero();

    if (m_noJumpDelay->value && SDK::Minecraft->thePlayer)
        CombatBridge::RequestJumpTicksZero();
}
