#pragma once

#include "../../module.h"
#include "../../commonData.h"

#include <string>

// Aim Assist — port of raven-bS's AimAssist (Normal mode).
// Settings and rotation math mirror Raven's AimAssist + RotationHelper +
// RotationUtils (multipoint aim point, backup face grid, smoothRotation with
// randomization and proximity slowdown, FOV gate, sort modes).
// Deliberately omitted (no infrastructure in this client):
//   - Silent mode / Keep move direction (requires server-rotation system)
//   - Hurt time sort (PlayerSnapshot has no hurtTime)
//   - Ignore teammates (no scoreboard team API)
class AimAssist : public Module
{
public:
    AimAssist();

    void onEnable() override;
    void onTick() override;
    void onDisable() override;

    std::string arrayListSuffix(SuffixDetail detail) const override;

private:
    struct AimTarget
    {
        CommonData::PlayerSnapshot player;
        Vector3 aimPoint{};
        float distanceSq = 0.0f;
        float yawDelta = 0.0f;
        float pitchDelta = 0.0f;
        std::string name;
    };

    bool conditionsMet();
    bool passesItemFilter() const;
    bool isBreakingAllowedBlock() const;

    bool getEnemy(AimTarget& out);
    bool candidatePassesDistance(const CommonData::PlayerSnapshot& pd, float range) const;

    Vector3 getAimPoint(const CommonData::PlayerSnapshot& pd, const BoundingBox& box) const;
    bool mainRayHitsTargetAabb(const Vector3& eye, const Vector3& point, const BoundingBox& targetBox, float range) const;
    std::vector<Vector3> buildBackupPoints(const BoundingBox& box, const Vector3& eye) const;

    bool findRotations(const CommonData::PlayerSnapshot& pd, float& outYaw, float& outPitch);
    void applyRotation(float yaw, float pitch);

    EnumSetting*   m_sortMode = nullptr;      // Health / Angle / Distance
    NumberSetting* m_speed = nullptr;         // 1-30, raven smoothing speed
    NumberSetting* m_multipointH = nullptr;   // 0-100 %
    NumberSetting* m_multipointV = nullptr;   // 0-100 %
    NumberSetting* m_randomization = nullptr; // 0-100 %
    NumberSetting* m_fov = nullptr;           // 15-360 deg
    NumberSetting* m_range = nullptr;         // 0-5 blocks

    BoolSetting*   m_aimInvis = nullptr;
    BoolSetting*   m_clickAim = nullptr;      // "Require mouse"
    BoolSetting*   m_ignoreBehindWalls = nullptr;
    BoolSetting*   m_ignoreBehindEntities = nullptr;
    BoolSetting*   m_stopWhenBreaking = nullptr;
    NumberSetting* m_hoverDelay = nullptr;    // ms, visible with stopWhenBreaking
    BoolSetting*   m_weaponOnly = nullptr;

    long long m_miningStartTime = -1;
};
