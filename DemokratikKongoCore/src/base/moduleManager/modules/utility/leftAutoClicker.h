#pragma once

#include "../../module.h"
#include "../../../java/java.h"

#include <atomic>
#include <thread>

/*
    AutoClicker (Slinky-style) — rebuilt from scratch.

    Left-clicks automatically while the left mouse button is held. Supports
    Jitter (default) and Butterfly click patterns, a single Target CPS,
    randomized intervals, simulated exhaustion, block-mining protection and
    SHIFT-to-click inventory support.
*/
class LeftAutoClicker : public Module
{
public:
    LeftAutoClicker();

    void onEnable() override;
    void onDisable() override;
    std::string arrayListSuffix(SuffixDetail detail) const override;

private:
    void threadMain();
    void stopThread();

    int  getRealCPS() const;
    bool isLeftMouseHeld() const;
    long long sampleDelayUs(bool inventory);

    // Condition helpers
    bool isHoldingWeapon(JNIEnv* env, jobject player);
    bool isUsingItem(JNIEnv* env, jobject player);
    bool isTargetingBlock(JNIEnv* env);

    // Core
    EnumSetting*   m_clickPattern = nullptr; // Jitter / Butterfly
    NumberSetting* m_targetCps    = nullptr; // single target speed
    BoolSetting*   m_randomize    = nullptr;
    BoolSetting*   m_exhaust      = nullptr;
    BoolSetting*   m_breakBlocks  = nullptr;

    // Inventory
    BoolSetting*   m_inventory    = nullptr;
    BoolSetting*   m_invRandomize = nullptr;

    // Conditions
    BoolSetting*   m_holdingWeapon = nullptr;
    BoolSetting*   m_notUsingItem = nullptr;

    long long m_nextClickUs            = 0;   // QPC microseconds
    long long m_cpsTimestamps[60]      = { 0 };
    int       m_cpsCount               = 0;

    bool      m_butterflyPendingSecond = false; // double-click state

    long long m_exhaustUntilUs         = 0;
    int       m_clicksSinceExhaust     = 0;

    std::thread        m_thread;
    std::atomic<bool>  m_threadRunning{ false };
};
