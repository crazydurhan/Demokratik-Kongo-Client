#pragma once

#include "../../module.h"
#include "../../../java/java.h"

/*
========================================================================
    FastPlace
------------------------------------------------------------------------
    Minecraft 1.8.9 varsayilan 4 tick'lik sag tik (blok koyma) gecikmesini
    rightClickDelayTimer alanina JNI ile azaltir veya kaldirir.

    crow / nobody-client / slinky_mappings ile ayni field tabanli yaklasim.
    Paket tabanli yontem kaldirildi (cift yerlestirme ve crash riski).
========================================================================
*/
class FastPlace : public Module
{
public:
    FastPlace();

    void onTick()    override;
    void onRender2D() override;
    void onDisable() override;
    std::string arrayListSuffix(SuffixDetail detail) const override;

private:
    bool canRun() const;
    bool isHoldingPlaceableBlock() const;
    bool isRightMouseActive() const;
    void trySwitchToBlockSlot();
    void applyAimLock() const;
    bool isBlockStack(JNIEnv* env, jobject stack, int* outCount = nullptr) const;
    static float normalizeYaw(float yaw);
    static float cardinalLockYaw(float yaw);

    // crow mantigi: delay 0 -> sifirla, 1-3 -> timer 4 iken indir, 4 -> vanilla
    void applyDelay(int targetDelay);

private:
    NumberSetting* m_delay        = nullptr;
    EnumSetting*   m_mode         = nullptr;
    BoolSetting*   m_blocksOnly   = nullptr;
    BoolSetting*   m_autoSwitch   = nullptr;
    BoolSetting*   m_aimLock      = nullptr;
    BoolSetting*   m_jitter       = nullptr;
    NumberSetting* m_jitterChance = nullptr;
};
