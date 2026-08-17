#pragma once

#include "../../module.h"
#include "../../../java/java.h"

class AutoTool : public Module
{
public:
    AutoTool();

    void onEnable()  override;
    void onDisable() override;
    void onTick()    override { /* worker: no-op — runs on client thread */ }
    void clientTick();

private:
    void resetState(JNIEnv* env = nullptr);

    // Settings
    NumberSetting*   m_activationDelay = nullptr;
    BoolSetting*     m_switchBack      = nullptr;
    BoolSetting*     m_allowSword      = nullptr;
    BoolSetting*     m_allowTool       = nullptr;
    BoolSetting*     m_allowFists      = nullptr;
    BoolSetting*     m_allowOther      = nullptr;
    BoolSetting*     m_sneakOnly       = nullptr;
    BoolSetting*     m_restrictBlocks  = nullptr;
    EnumSetting*     m_blockFilterMode = nullptr;

    // Runtime state
    int       m_originalSlot  = -1;
    bool      m_isMining      = false;
    long long m_miningStartMs = 0;
};
