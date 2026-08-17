#pragma once

#include "../../module.h"
#include "../../commonData.h"

#include <string>

class AimAssist : public Module
{
public:
    AimAssist();

    void onEnable() override;
    void onRender3D(float partialTicks) override;
    void onDisable() override;

    std::string arrayListSuffix(SuffixDetail detail) const override;

private:
    struct TargetInfo;

    bool isLeftMouseHeld() const;
    bool passesItemFilter() const;
    bool isBreakingAllowedBlock() const;
    bool isStrafing(const TargetInfo& target) const;
    bool isTargetVisible(const Vector3& eyes, const Vector3& aimPoint) const;

    bool findTarget(TargetInfo& outTarget);
    bool findLockedTarget(TargetInfo& outTarget) const;
    bool buildTarget(const CommonData::PlayerSnapshot& pd, TargetInfo& outTarget) const;

    float scoreTarget(const TargetInfo& target) const;

    void tickAim();
    void applyAim(const TargetInfo& target);
    void clearLock();

    EnumSetting*   m_mode = nullptr;
    BoolSetting*   m_requireMouseDown = nullptr;
    BoolSetting*   m_targetLock = nullptr;
    NumberSetting* m_strafeIncrease = nullptr;
    BoolSetting*   m_checkBlockBreak = nullptr;
    StringSetting* m_breakBlocksWhitelist = nullptr;

    BoolSetting*   m_aimVertically = nullptr;
    NumberSetting* m_verticalSpeed = nullptr;
    NumberSetting* m_horizontalSpeed = nullptr;

    NumberSetting* m_maxAngle = nullptr;
    NumberSetting* m_distance = nullptr;

    BoolSetting*   m_limitItemsEnabled = nullptr;
    StringSetting* m_allowedItems = nullptr;

    EnumSetting*   m_targetArea = nullptr;
    EnumSetting*   m_targetMode = nullptr;

    BoolSetting*   m_wallCheck = nullptr;
    NumberSetting* m_predictionTicks = nullptr;

    std::string m_lockedTargetName;
    long long   m_lockValidMs = 0;
    bool        m_wasMouseHeld = false;
    Vector3     m_lastLocalPos{};
    long long   m_lastLocalPosMs = 0;
    bool        m_haveLastLocalPos = false;
    long long   m_lastAimMs = 0;
    long long   m_lastApplyMs = 0;   // dt normalization for per-frame aim

    // Target name cache — findTarget (expensive) runs at 20Hz, but deltas
    // are recomputed from fresh player data every tick for smooth aim.
    std::string m_cachedTargetName;
    bool        m_hasCachedTarget = false;
    long long   m_lastTargetScanMs = 0;
};
