#pragma once

#include "../../module.h"
#include "../../../java/java.h"
#include "../../../util/math/geometry.h"

class Backtrack : public Module
{
public:
    Backtrack();

    void onEnable()  override;
    void onDisable() override;
    void onTick()    override;

    std::string arrayListSuffix(SuffixDetail detail) const override;

private:
    void      pushAll();
    void      endSession(long long now, bool startCooldown);
    long long rollSessionMs() const;
    bool      isHoldingWeapon(JNIEnv* env, jobject player);
    int       resolveEntityId(JNIEnv* env, jobject entity);

    // Distance & Timing
    NumberSetting*    m_minDistance       = nullptr;
    NumberSetting*    m_maxDistance       = nullptr;
    IntRangeSetting*  m_delay             = nullptr;
    NumberSetting*    m_maxHurtTime       = nullptr;
    NumberSetting*    m_cooldown          = nullptr;

    // Safety & Conditions
    BoolSetting*      m_disableOnHit      = nullptr;
    BoolSetting*      m_holdingWeaponOnly = nullptr;
    BoolSetting*      m_onlyWhenTargeting = nullptr;

    // Real Position Indicator (rendered Java-side in EspBridge.renderWorld)
    BoolSetting*      m_showRealPos       = nullptr;
    ColorSetting*     m_boxColor          = nullptr;
    NumberSetting*    m_lineWidth         = nullptr;
    BoolSetting*      m_filled            = nullptr;
    BoolSetting*      m_headRotation      = nullptr;

    // Runtime state
    bool      m_isActive      = false;
    long long m_lastActiveMs  = 0;
    long long m_activeStartMs = 0;
    long long m_lastSeenMs    = 0;
    long long m_sessionMs     = 0;
    int       m_targetId      = -1;
};
