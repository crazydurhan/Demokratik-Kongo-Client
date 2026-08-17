#include "aimAssist.h"

#include "combatBridge.h"
#include "friends.h"
#include "itemWhitelist.h"
#include "../../commonData.h"
#include "../../../sdk/sdk.h"
#include "../../../util/math/math.h"

#include <Windows.h>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <mutex>
#include <vector>

namespace
{
    constexpr float kMaxPitch = 90.0f;
    constexpr float kMinPitch = -90.0f;
    constexpr float kAimDeadzoneDeg = 0.15f;
    constexpr float kMinStableAimHorizontal = 0.08f;
    constexpr long long kLockGraceMs = 500;   // hedef kaybında grace penceresi

    inline long long nowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    inline float horizontalLength(const Vector3& v)
    {
        return std::sqrt(v.x * v.x + v.z * v.z);
    }

    inline float clampf(float v, float lo, float hi)
    {
        return std::max(lo, std::min(v, hi));
    }

    inline Vector3 boxCenter(const BoundingBox& box)
    {
        return Vector3{
            static_cast<float>((box.minX + box.maxX) * 0.5),
            static_cast<float>((box.minY + box.maxY) * 0.5),
            static_cast<float>((box.minZ + box.maxZ) * 0.5)
        };
    }

    inline Vector3 closestPointOnBox(const Vector3& point, const BoundingBox& box)
    {
        return Vector3{
            clampf(point.x, static_cast<float>(box.minX), static_cast<float>(box.maxX)),
            clampf(point.y, static_cast<float>(box.minY), static_cast<float>(box.maxY)),
            clampf(point.z, static_cast<float>(box.minZ), static_cast<float>(box.maxZ))
        };
    }

    inline Vector3 stableClosestPointOnBox(const Vector3& eyes, const BoundingBox& box)
    {
        Vector3 closest = closestPointOnBox(eyes, box);
        const Vector3 delta = closest - eyes;
        if (horizontalLength(delta) >= kMinStableAimHorizontal)
            return closest;

        // When the player's X/Z overlaps the target AABB, a pure closest-point
        // clamp can collapse to nearly the eye position. That makes atan2(0, 0)
        // or tiny X/Z deltas produce unstable yaw near the target. Pin to the
        // nearest horizontal face instead, while keeping the reach-optimal Y.
        const Vector3 center = boxCenter(box);
        const float dx = eyes.x - center.x;
        const float dz = eyes.z - center.z;

        closest.y = clampf(eyes.y,
            static_cast<float>(box.minY),
            static_cast<float>(box.maxY));

        if (std::fabs(dx) >= std::fabs(dz))
        {
            closest.x = dx >= 0.0f ? static_cast<float>(box.maxX) : static_cast<float>(box.minX);
            closest.z = clampf(eyes.z, static_cast<float>(box.minZ), static_cast<float>(box.maxZ));
        }
        else
        {
            closest.x = clampf(eyes.x, static_cast<float>(box.minX), static_cast<float>(box.maxX));
            closest.z = dz >= 0.0f ? static_cast<float>(box.maxZ) : static_cast<float>(box.minZ);
        }

        return closest;
    }

    inline BoundingBox interpolatedBox(const CommonData::PlayerSnapshot& pd)
    {
        const float partial = CommonData::renderPartialTicks;
        const Vector3 interp = pd.lastPos + (pd.pos - pd.lastPos) * partial;
        const BoundingBox& cached = pd.boundingBox;

        const float halfW = std::max(
            static_cast<float>((cached.maxX - cached.minX) * 0.5), 0.3f);

        return BoundingBox{
            interp.x - halfW, interp.y, interp.z - halfW,
            interp.x + halfW, interp.y + pd.height, interp.z + halfW
        };
    }

    inline BoundingBox offsetBox(const BoundingBox& box, const Vector3& delta)
    {
        return BoundingBox{
            box.minX + delta.x, box.minY + delta.y, box.minZ + delta.z,
            box.maxX + delta.x, box.maxY + delta.y, box.maxZ + delta.z
        };
    }

    inline std::string targetNameKey(const CommonData::PlayerSnapshot& pd)
    {
        return pd.name.empty() ? pd.displayName : pd.name;
    }
}

struct AimAssist::TargetInfo
{
    CommonData::PlayerSnapshot player;
    Vector3 aimPoint;
    Vector3 velocity;
    float distance = 0.0f;
    float yawDelta = 0.0f;
    float pitchDelta = 0.0f;
    float totalAngle = 0.0f;
    bool occluded = false;
    std::string name;
};

AimAssist::AimAssist()
    : Module("Aim Assist", "Aura combat helper for close-range target tracking and reach sync.", Category::Combat)
{
    m_mode = &add<EnumSetting>("Mode", std::vector<const char*>{ "Simple", "Adaptive" }, 0);

    m_requireMouseDown = &add<BoolSetting>("Require mouse down", true);
    m_targetLock = &add<BoolSetting>("Target Lock", false);
    m_strafeIncrease = &add<NumberSetting>("Strafe Increase", 35.0f, 0.0f, 150.0f, 5.0f);
    m_strafeIncrease->suffix = "%";

    m_checkBlockBreak = &add<BoolSetting>("Check Block Break", true);
    m_breakBlocksWhitelist = &add<StringSetting>("Break Blocks Whitelist", "pickaxe,shovel,axe,shears");
    m_breakBlocksWhitelist->itemList = true;
    m_breakBlocksWhitelist->visible = [this]{ return m_checkBlockBreak->value; };

    m_aimVertically = &add<BoolSetting>("Aim Vertically", false);
    m_verticalSpeed = &add<NumberSetting>("Vertical Speed", 3.0f, 0.1f, 20.0f, 0.1f);
    m_verticalSpeed->suffix = "%";
    m_verticalSpeed->visible = [this]{ return m_aimVertically->value; };

    m_horizontalSpeed = &add<NumberSetting>("Horizontal Speed", 5.0f, 0.1f, 20.0f, 0.1f);
    m_horizontalSpeed->suffix = "%";

    m_maxAngle = &add<NumberSetting>("Max Angle", 60.0f, 1.0f, 180.0f, 1.0f);
    m_maxAngle->suffix = " deg";

    m_distance = &add<NumberSetting>("Distance", 4.5f, 1.0f, 8.0f, 0.1f);
    m_distance->suffix = " blocks";

    m_limitItemsEnabled = &add<BoolSetting>("Limit to Items", false);
    m_allowedItems = &add<StringSetting>("Allowed Items", "sword,axe");
    m_allowedItems->itemList = true;
    m_allowedItems->visible = [this]{ return m_limitItemsEnabled->value; };

    m_targetArea = &add<EnumSetting>("Target Area", std::vector<const char*>{ "Center", "Closest" }, 1);
    m_targetMode = &add<EnumSetting>("Target Mode", std::vector<const char*>{ "Distance", "Yaw", "Health", "Gapple" }, 1);

    m_wallCheck = &add<BoolSetting>("Wall Check", true);
    m_predictionTicks = &add<NumberSetting>("Prediction Ticks", 2.0f, 0.0f, 10.0f, 0.5f);
    m_predictionTicks->visible = [this]{ return m_mode->index == 1; };
}

std::string AimAssist::arrayListSuffix(SuffixDetail detail) const
{
    if (detail == SuffixDetail::None)
        return "";

    std::string s = m_mode->current();
    s += " ";
    s += m_targetMode->current();

    if (m_targetLock->value && !m_lockedTargetName.empty()) {
        s += " [LOCK:";
        const size_t maxName = std::min(m_lockedTargetName.size(), size_t(8));
        s.append(m_lockedTargetName, 0, maxName);
        s += "]";
    }

    if (detail == SuffixDetail::Basic)
        return s;

    char buf[192];
    std::snprintf(buf, sizeof(buf), "%s %.1f° %.1fb%s",
        s.c_str(), m_maxAngle->value, m_distance->value,
        m_requireMouseDown->value ? " [Hold]" : "");
    return buf;
}

bool AimAssist::isLeftMouseHeld() const
{
    return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
}

bool AimAssist::passesItemFilter() const
{
    if (!m_limitItemsEnabled->value)
        return true;

    return ItemWhitelist::IsAllowed(ItemWhitelist::GetHeldDisplayName(), m_allowedItems->value);
}

bool AimAssist::isBreakingAllowedBlock() const
{
    if (!m_checkBlockBreak->value || !isLeftMouseHeld() || !SDK::Minecraft)
        return false;

    if (CombatBridge::CrosshairOnEntity())
        return false;

    CMovingObjectPosition mop = SDK::Minecraft->GetMouseOver();
    if (!mop.GetInstance() || !mop.IsTypeOfBlock())
        return false;

    return ItemWhitelist::IsAllowed(ItemWhitelist::GetHeldDisplayName(), m_breakBlocksWhitelist->value);
}

bool AimAssist::isTargetVisible(const Vector3& eyes, const Vector3& aimPoint) const
{
    if (!m_wallCheck->value)
        return true;
    if (!SDK::Minecraft || !SDK::Minecraft->theWorld)
        return true;

    RayResult r = SDK::Minecraft->theWorld->rayTraceBlocks(
        eyes, aimPoint,
        /*stopOnLiquid=*/false,
        /*ignoreBlockWithoutBoundingBox=*/true,
        /*returnLastUncollidableBlock=*/false);

    return !r.hit;
}

float AimAssist::scoreTarget(const TargetInfo& target) const
{
    switch (m_targetMode->index)
    {
        case 0: return target.distance;
        case 1: return std::fabs(target.yawDelta) + (m_aimVertically->value ? std::fabs(target.pitchDelta) * 0.35f : 0.0f);
        case 2: return target.player.health + target.player.absorptionAmount;
        case 3: // Gapple — prefer the player whose regen is closest to expiring
                // (or has none at all).  No regen = score 0 = best target.
        {
            const long long now = nowMs();
            const long long remaining = target.player.regenEndMs - now;
            return remaining > 0 ? (float)(remaining / 1000.0) : 0.0f;
        }
        default: return target.totalAngle;
    }
}

bool AimAssist::buildTarget(const CommonData::PlayerSnapshot& pd, TargetInfo& outTarget) const
{
    if (pd.isLocalPlayer || pd.health <= 0.0f)
        return false;

    const std::string key = targetNameKey(pd);
    if (key.empty() || Friends::IsFriend(key))
        return false;

    CEntityPlayerSP* local = SDK::Minecraft ? SDK::Minecraft->thePlayer : nullptr;
    if (!local)
        return false;

    const Vector3 eyes = local->GetEyePos();
    const Vector2 current = local->GetAngles();
    BoundingBox box = interpolatedBox(pd);

    // Adaptive mode leads the target: shift the whole AABB along the target's
    // velocity, then pick the aim point inside the *predicted* box. Previously
    // the predicted point was clamped back into the un-moved box, which
    // cancelled almost all of the lead.
    if (m_mode->index == 1)
    {
        const Vector3 velocity = pd.pos - pd.lastPos;
        box = offsetBox(box, velocity * m_predictionTicks->value);
    }

    Vector3 aimPoint = boxCenter(box);
    if (m_targetArea->index == 1)
        aimPoint = stableClosestPointOnBox(eyes, box);

    if (horizontalLength(aimPoint - eyes) < kMinStableAimHorizontal)
        aimPoint = stableClosestPointOnBox(eyes, box);

    const float distance = eyes.Distance(aimPoint);
    if (distance > m_distance->value)
        return false;

    const Vector2 desired = Math::getAngles(eyes, aimPoint);
    const float yawDelta = Math::wrapAngleTo180(desired.x - current.x);
    const float pitchDelta = Math::wrapAngleTo180(desired.y - current.y);
    const float totalAngle = std::sqrt(yawDelta * yawDelta + pitchDelta * pitchDelta);

    const float gateAngle = m_aimVertically->value ? totalAngle : std::fabs(yawDelta);
    if (gateAngle > m_maxAngle->value)
        return false;

    outTarget.player = pd;
    outTarget.aimPoint = aimPoint;
    outTarget.velocity = pd.pos - pd.lastPos;
    outTarget.distance = distance;
    outTarget.yawDelta = yawDelta;
    outTarget.pitchDelta = pitchDelta;
    outTarget.totalAngle = totalAngle;
    outTarget.name = key;
    return true;
}

bool AimAssist::findLockedTarget(TargetInfo& outTarget) const
{
    if (m_lockedTargetName.empty())
        return false;

    const std::vector<CommonData::PlayerSnapshot> players = CommonData::SnapshotPlayers();

    for (const auto& pd : players)
    {
        if (targetNameKey(pd) != m_lockedTargetName)
            continue;

        if (!buildTarget(pd, outTarget))
            return false;

        CEntityPlayerSP* local = SDK::Minecraft ? SDK::Minecraft->thePlayer : nullptr;
        if (local)
        {
            const Vector3 eyes = local->GetEyePos();
            if (!isTargetVisible(eyes, outTarget.aimPoint))
                outTarget.occluded = true;
        }
        return true;
    }

    return false;
}

bool AimAssist::findTarget(TargetInfo& outTarget)
{
    const long long now = nowMs();

    // ---- 1) Locked target dene ----
    if (!m_lockedTargetName.empty())
    {
        TargetInfo temp{};
        if (findLockedTarget(temp))
        {
            if (temp.occluded)
            {
                // Locked target wall arkasında — grace içinde pause
                if (now - m_lockValidMs < kLockGraceMs)
                    return false;

                // Grace bitti — lock düşür, scan'e devam
                clearLock();
            }
            else
            {
                m_lockValidMs = now;
                outTarget = temp;
                return true;
            }
        }
        else
        {
            // Locked target oyuncu listesinde yok (öldü/despawn)
            if (m_targetLock->value && m_requireMouseDown->value && isLeftMouseHeld()
                && (now - m_lockValidMs) < kLockGraceMs)
                return false;

            clearLock();
        }
    }

    // ---- 2) Tarama ----
    CEntityPlayerSP* local = SDK::Minecraft ? SDK::Minecraft->thePlayer : nullptr;
    if (!local)
        return false;
    const Vector3 eyes = local->GetEyePos();

    const std::vector<CommonData::PlayerSnapshot> players = CommonData::SnapshotPlayers();

    bool found = false;
    float bestScore = std::numeric_limits<float>::max();
    TargetInfo best{};

    for (const auto& pd : players)
    {
        TargetInfo candidate{};
        if (!buildTarget(pd, candidate))
            continue;

        if (m_wallCheck->value && !isTargetVisible(eyes, candidate.aimPoint))
            continue;

        const float score = scoreTarget(candidate);
        if (!found || score < bestScore)
        {
            found = true;
            bestScore = score;
            best = candidate;
        }
    }

    if (!found)
        return false;

    outTarget = best;
    if ((m_requireMouseDown->value || m_targetLock->value) && m_lockedTargetName.empty())
    {
        m_lockedTargetName = best.name;
        m_lockValidMs = now;
    }
    return true;
}

bool AimAssist::isStrafing(const TargetInfo& target) const
{
    if (horizontalLength(target.velocity) > 0.025f)
        return true;

    if (!SDK::Minecraft || !SDK::Minecraft->thePlayer)
        return false;

    const Vector3 localPos = SDK::Minecraft->thePlayer->GetPos();
    if (!m_haveLastLocalPos)
        return false;

    // Threshold is a per-50ms displacement; scale by the real sample window
    // so per-frame sampling matches the old per-tick sensitivity.
    const long long dtMs = std::max(1LL, nowMs() - m_lastLocalPosMs);
    const float threshold = 0.025f * (static_cast<float>(dtMs) / 50.0f);
    return horizontalLength(localPos - m_lastLocalPos) > threshold;
}

void AimAssist::applyAim(const TargetInfo& target)
{
    CEntityPlayerSP* local = SDK::Minecraft ? SDK::Minecraft->thePlayer : nullptr;
    if (!local)
        return;

    const Vector2 current = local->GetAngles();
    float hSpeed = m_horizontalSpeed->value / 100.0f;
    float vSpeed = m_verticalSpeed->value / 100.0f;

    if (isStrafing(target))
    {
        const float boost = 1.0f + (m_strafeIncrease->value / 100.0f);
        hSpeed *= boost;
    }

    if (m_mode->index == 1)
    {
        const float yawFactor = clampf(std::fabs(target.yawDelta) / std::max(m_maxAngle->value, 1.0f), 0.25f, 1.0f);
        hSpeed *= 0.55f + yawFactor * 0.65f;
        vSpeed *= 0.55f + yawFactor * 0.55f;
    }

    hSpeed = clampf(hSpeed, 0.001f, 0.85f);
    vSpeed = clampf(vSpeed, 0.001f, 0.85f);

    // Speeds are tuned as a per-20Hz-tick fraction of the remaining angle.
    // Aim now applies every frame, so rescale by elapsed time (exponential
    // step) to keep the same convergence rate at any FPS.
    const long long now = nowMs();
    const long long dtMs = m_lastApplyMs > 0 ? now - m_lastApplyMs : 50;
    m_lastApplyMs = now;
    // Capping the exponent at one tick meant a 100ms frame only ever applied
    // 50ms worth of rotation, halving the effective speed at low FPS. Allow up
    // to 4 ticks of catch-up; beyond that a hitch would snap the view.
    const float tickFraction = clampf(static_cast<float>(dtMs) / 50.0f, 0.0f, 4.0f);
    hSpeed = 1.0f - std::pow(1.0f - hSpeed, tickFraction);
    vSpeed = 1.0f - std::pow(1.0f - vSpeed, tickFraction);

    const float yawStep = std::fabs(target.yawDelta) < kAimDeadzoneDeg ? 0.0f : target.yawDelta * hSpeed;
    const float pitchStep = std::fabs(target.pitchDelta) < kAimDeadzoneDeg ? 0.0f : target.pitchDelta * vSpeed;

    const float newYaw = Math::wrapAngleTo180(current.x + yawStep);
    float newPitch = current.y;
    if (m_aimVertically->value)
        newPitch = clampf(current.y + pitchStep, kMinPitch, kMaxPitch);

    local->SetAngles(Vector2{ newYaw, newPitch });
}

void AimAssist::clearLock()
{
    m_lockedTargetName.clear();
    m_lockValidMs = 0;
}

void AimAssist::onEnable()
{
    clearLock();
    m_hasCachedTarget = false;
    m_cachedTargetName.clear();
}

// Aim runs per frame (renderWorldPass hook on the client thread) instead of
// per 20Hz tick — rotation steps are dt-normalized inside applyAim so the
// convergence rate stays identical at any FPS.
void AimAssist::onRender3D(float /*partialTicks*/)
{
    tickAim();
}

void AimAssist::onDisable()
{
    clearLock();
    m_wasMouseHeld = false;
    m_haveLastLocalPos = false;
    m_lastLocalPosMs = 0;
    m_lastAimMs = 0;
    m_lastApplyMs = 0;
    m_hasCachedTarget = false;
    m_cachedTargetName.clear();
}

void AimAssist::tickAim()
{
    const long long now = nowMs();
    const bool mouseHeld = isLeftMouseHeld();
    if (!mouseHeld && m_wasMouseHeld)
    {
        if (m_requireMouseDown->value || !m_targetLock->value)
            clearLock();
    }
    m_wasMouseHeld = mouseHeld;

    if (!CombatBridge::InGame() || !SDK::Minecraft || !SDK::Minecraft->thePlayer)
    {
        clearLock();
        m_haveLastLocalPos = false;
        m_hasCachedTarget = false;
        m_cachedTargetName.clear();
        return;
    }

    if (m_requireMouseDown->value && !mouseHeld)
    {
        m_hasCachedTarget = false;
        m_cachedTargetName.clear();
        return;
    }

    if (!passesItemFilter())
    {
        m_hasCachedTarget = false;
        m_cachedTargetName.clear();
        return;
    }

    if (isBreakingAllowedBlock())
    {
        m_hasCachedTarget = false;
        m_cachedTargetName.clear();
        return;
    }

    // === Target selection (expensive: iterate players, raycast visibility) ===
    // Runs at 20Hz — the "best target" doesn't change faster than that.
    if (now - m_lastTargetScanMs >= 50)
    {
        m_lastTargetScanMs = now;
        TargetInfo target{};
        if (findTarget(target))
        {
            m_cachedTargetName = target.name;
            m_hasCachedTarget = true;
        }
        else
        {
            m_hasCachedTarget = false;
            m_cachedTargetName.clear();
            if (!m_targetLock->value || !m_requireMouseDown->value || !mouseHeld)
                clearLock();
        }
    }

    // === Aim application (every tick, fresh deltas) ===
    // Recompute yaw/pitch from the CURRENT eye position to the target's
    // CURRENT position. This gives smooth convergence without overshoot —
    // the old "cache deltas" approach applied the same delta repeatedly
    // which caused visible jitter as the angle overshot the target.
    if (m_hasCachedTarget && !m_cachedTargetName.empty())
    {
        const std::vector<CommonData::PlayerSnapshot> players = CommonData::SnapshotPlayers();

        bool found = false;
        for (const auto& pd : players)
        {
            if (targetNameKey(pd) == m_cachedTargetName)
            {
                found = true;
                TargetInfo target{};
                if (buildTarget(pd, target))
                {
                    applyAim(target);
                }
                break;
            }
        }
        if (!found)
        {
            m_hasCachedTarget = false;
            m_cachedTargetName.clear();
            clearLock();
        }
    }

    m_lastAimMs = now;
    m_lastLocalPos = SDK::Minecraft->thePlayer->GetPos();
    m_lastLocalPosMs = now;
    m_haveLastLocalPos = true;
}
