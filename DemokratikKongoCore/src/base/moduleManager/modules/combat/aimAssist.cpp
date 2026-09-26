#include "aimAssist.h"

#include "antibot.h"
#include "../../../util/math/geometry.h"
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
#include <random>
#include <vector>

namespace
{
    // Vanilla EntityLivingBase#getCollisionBorderSize for players.
    constexpr double kBorderSize = 0.1;
    constexpr double kRadToDeg = 57.295780181884766;
    constexpr float  kFarThreshold = 180.0f;

    inline long long nowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    inline double clampd(double v, double lo, double hi)
    {
        return std::max(lo, std::min(v, hi));
    }

    inline float wrapTo180(float a)
    {
        a = std::fmod(a + 180.0f, 360.0f);
        if (a < 0.0f)
            a += 360.0f;
        return a - 180.0f;
    }

    BoundingBox expandBox(const BoundingBox& b, double e)
    {
        return BoundingBox{ b.minX - e, b.minY - e, b.minZ - e,
                            b.maxX + e, b.maxY + e, b.maxZ + e };
    }

    Vector3 closestPointOnBox(const BoundingBox& b, const Vector3& p)
    {
        return Vector3{
            static_cast<float>(clampd(p.x, b.minX, b.maxX)),
            static_cast<float>(clampd(p.y, b.minY, b.maxY)),
            static_cast<float>(clampd(p.z, b.minZ, b.maxZ)) };
    }

    bool boxContains(const BoundingBox& b, const Vector3& p)
    {
        return p.x >= b.minX && p.x <= b.maxX &&
               p.y >= b.minY && p.y <= b.maxY &&
               p.z >= b.minZ && p.z <= b.maxZ;
    }

    // AxisAlignedBB#calculateIntercept — ray vs AABB, slab method.
    bool rayIntersectsBox(const Vector3& from, const Vector3& to, const BoundingBox& b)
    {
        const double dx = to.x - from.x;
        const double dy = to.y - from.y;
        const double dz = to.z - from.z;

        double tmin = 0.0, tmax = 1.0;
        const double p[3] = { from.x, from.y, from.z };
        const double d[3] = { dx, dy, dz };
        const double bmin[3] = { b.minX, b.minY, b.minZ };
        const double bmax[3] = { b.maxX, b.maxY, b.maxZ };

        for (int i = 0; i < 3; ++i)
        {
            if (std::fabs(d[i]) < 1e-12)
            {
                if (p[i] < bmin[i] || p[i] > bmax[i])
                    return false;
            }
            else
            {
                double t1 = (bmin[i] - p[i]) / d[i];
                double t2 = (bmax[i] - p[i]) / d[i];
                if (t1 > t2) std::swap(t1, t2);
                tmin = std::max(tmin, t1);
                tmax = std::min(tmax, t2);
                if (tmin > tmax)
                    return false;
            }
        }
        return true;
    }

    // RotationUtils#getRotationsToPoint — base-aware, degenerate-yaw safe.
    void rotationsToPoint(const Vector3& eye, const Vector3& point,
                          float baseYaw, float basePitch,
                          float& outYaw, float& outPitch)
    {
        const double deltaX = point.x - eye.x;
        const double deltaZ = point.z - eye.z;
        const double deltaY = point.y - eye.y;
        const double horizDistSq = deltaX * deltaX + deltaZ * deltaZ;

        if (horizDistSq < 1.0e-12)
        {
            outYaw = baseYaw;
            outPitch = basePitch + wrapTo180(
                static_cast<float>(-(std::atan2(deltaY, 0.0) * kRadToDeg)) - basePitch);
        }
        else
        {
            const float targetYaw = static_cast<float>(std::atan2(deltaZ, deltaX) * kRadToDeg) - 90.0f;
            const float targetPitch = static_cast<float>(
                -(std::atan2(deltaY, std::sqrt(horizDistSq)) * kRadToDeg));
            outYaw = baseYaw + wrapTo180(targetYaw - baseYaw);
            outPitch = basePitch + wrapTo180(targetPitch - basePitch);
        }
        outPitch = clampd(outPitch, -90.0f, 90.0f);
    }

    // RotationUtils#smoothRotation — linear step model along the combined
    // (yaw, pitch) direction, with randomization and proximity slowdown.
    void smoothRotation(float baseYaw, float basePitch,
                        float targetYaw, float targetPitch,
                        int speed, float randomizationPercent,
                        float& outYaw, float& outPitch)
    {
        if (speed <= 0) { outYaw = baseYaw; outPitch = clampd(basePitch, -90.0f, 90.0f); return; }
        if (speed >= 30) { outYaw = targetYaw; outPitch = clampd(targetPitch, -90.0f, 90.0f); return; }

        const float deltaYaw = wrapTo180(targetYaw - baseYaw);
        const float deltaPitch = targetPitch - basePitch;
        const float magnitude = std::sqrt(deltaYaw * deltaYaw + deltaPitch * deltaPitch);
        if (magnitude < 0.001f)
        {
            outYaw = targetYaw;
            outPitch = clampd(targetPitch, -90.0f, 90.0f);
            return;
        }

        thread_local std::mt19937 rng{ std::random_device{}() };
        thread_local std::uniform_real_distribution<float> uniform(0.0f, 1.0f);

        const float t = speed / 30.0f;
        float stepSize = t * t * 180.0f;   // degrees per game tick, like raven

        const float range = 0.6f * (randomizationPercent / 100.0f);
        const float multiplier = (range <= 0.001f) ? 1.0f
            : (1.0f - range * 0.5f + uniform(rng) * range);
        stepSize *= multiplier;

        float proximityFactor = std::min(1.0f, magnitude / kFarThreshold);
        proximityFactor = std::pow(proximityFactor, 0.7f);
        const float maxSlowdown = randomizationPercent / 100.0f;
        // Cap proximity slowdown at 20% so high randomization doesn't kill aim.
        const float proximityMult = std::max(0.8f, 1.0f - maxSlowdown * (1.0f - proximityFactor));
        stepSize *= proximityMult;

        const float stepLength = std::min(stepSize, magnitude);
        const float scale = stepLength / magnitude;

        outYaw = baseYaw + deltaYaw * scale;
        outPitch = clampd(basePitch + deltaPitch * scale, -90.0f, 90.0f);
    }
}

AimAssist::AimAssist()
    : Module("Aim Assist", "raven-bS style aim assist: multipoint aim point, backup face points, smoothed rotation.", Category::Combat)
{
    m_speed = &add<NumberSetting>("Speed", 10.0f, 1.0f, 30.0f, 1.0f);

    m_multipointH = &add<NumberSetting>("Multipoint Horizontal", 0.0f, 0.0f, 100.0f, 1.0f);
    m_multipointH->suffix = "%";
    m_multipointV = &add<NumberSetting>("Multipoint Vertical", 0.0f, 0.0f, 100.0f, 1.0f);
    m_multipointV->suffix = "%";
    m_randomization = &add<NumberSetting>("Randomization", 50.0f, 0.0f, 100.0f, 1.0f);
    m_randomization->suffix = "%";

    m_fov = &add<NumberSetting>("FOV", 90.0f, 15.0f, 360.0f, 1.0f);
    m_fov->suffix = " deg";
    m_range = &add<NumberSetting>("Range", 4.5f, 0.0f, 5.0f, 0.1f);
    m_range->suffix = " blocks";

    m_sortMode = &add<EnumSetting>("Sort", std::vector<const char*>{ "Health", "Angle", "Distance" }, 1);

    m_ignoreBehindWalls = &add<BoolSetting>("Ignore behind walls", false);
    m_ignoreBehindEntities = &add<BoolSetting>("Ignore behind entities", false);
    m_aimInvis = &add<BoolSetting>("Aim invis", false);
    m_clickAim = &add<BoolSetting>("Require mouse", true);
    m_stopWhenBreaking = &add<BoolSetting>("Stop when breaking", false);
    m_hoverDelay = &add<NumberSetting>("Hover delay", 100.0f, 0.0f, 500.0f, 10.0f);
    m_hoverDelay->suffix = " ms";
    m_hoverDelay->visible = [this]{ return m_stopWhenBreaking->value; };
    m_weaponOnly = &add<BoolSetting>("Weapon only", false);
}

std::string AimAssist::arrayListSuffix(SuffixDetail detail) const
{
    if (detail == SuffixDetail::None)
        return "";

    if (detail == SuffixDetail::Basic)
        return m_sortMode->current();

    char buf[96];
    std::snprintf(buf, sizeof(buf), "%.0f%% %.0fdeg %.1fb",
        m_randomization->value, m_fov->value, m_range->value);
    return buf;
}

bool AimAssist::passesItemFilter() const
{
    if (!m_weaponOnly->value)
        return true;
    return ItemWhitelist::IsAllowed(ItemWhitelist::GetHeldDisplayName(), "sword,axe");
}

bool AimAssist::isBreakingAllowedBlock() const
{
    if (!m_stopWhenBreaking->value)
        return false;

    if (!SDK::Minecraft || !CombatBridge::CrosshairOnEntity())
    {
        // raven checks "is the player actually mining a block".
        if (SDK::Minecraft)
        {
            CMovingObjectPosition mop = SDK::Minecraft->GetMouseOver();
            if (mop.GetInstance() && mop.IsTypeOfBlock())
                return true;
        }
    }
    return false;
}

// raven AimAssist#conditionsMet
bool AimAssist::conditionsMet()
{
    if (!CombatBridge::InGame() || !SDK::Minecraft || !SDK::Minecraft->thePlayer)
        return false;
    if (!passesItemFilter())
        return false;
    if (m_clickAim->value && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0)
        return false;
    if (m_stopWhenBreaking->value && isBreakingAllowedBlock())
    {
        if (m_miningStartTime == -1)
            m_miningStartTime = nowMs();
        const long long elapsed = nowMs() - m_miningStartTime;
        if (elapsed >= m_hoverDelay->value)
            return false;
    }
    else
    {
        m_miningStartTime = -1;
    }
    return true;
}

// RotationUtils#getAimPoint — center pulled toward the eye-closest point by
// the multipoint factors.
Vector3 AimAssist::getAimPoint(const CommonData::PlayerSnapshot& pd, const BoundingBox& rawBox) const
{
    CEntityPlayerSP* local = SDK::Minecraft ? SDK::Minecraft->thePlayer : nullptr;
    const BoundingBox box = expandBox(rawBox, kBorderSize);
    const Vector3 eye = local->GetEyePos();

    const float centerX = static_cast<float>((box.minX + box.maxX) * 0.5);
    const float centerZ = static_cast<float>((box.minZ + box.maxZ) * 0.5);
    // Living entities aim at eye height (posY + eyeHeight); vanilla eyeHeight
    // for players is height * 0.9.
    const float centerY = pd.pos.y + pd.height * 0.9f;

    if (boxContains(box, eye))
        return Vector3{ centerX, eye.y, centerZ };

    const Vector3 cl = closestPointOnBox(box, eye);
    const float tH = static_cast<float>(clampd(m_multipointH->value / 100.0, 0.0, 1.0));
    const float tV = static_cast<float>(clampd(m_multipointV->value / 100.0, 0.0, 1.0));

    return Vector3{
        centerX + (cl.x - centerX) * tH,
        centerY + (cl.y - centerY) * tV,
        centerZ + (cl.z - centerZ) * tH };
}

// RotationUtils#mainRayHitsTargetAABB — extend the ray to `range` and require
// it to still cross the target's (expanded) box.
bool AimAssist::mainRayHitsTargetAabb(const Vector3& eye, const Vector3& point,
                                      const BoundingBox& targetBox, float range) const
{
    const double dx = point.x - eye.x;
    const double dy = point.y - eye.y;
    const double dz = point.z - eye.z;
    const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len < 1e-6)
        return false;

    const double scale = range / len;
    const Vector3 end{
        static_cast<float>(eye.x + dx * scale),
        static_cast<float>(eye.y + dy * scale),
        static_cast<float>(eye.z + dz * scale) };

    return rayIntersectsBox(eye, end, expandBox(targetBox, kBorderSize));
}

// RotationUtils#buildBackupPoints — grid of points on the faces of the target
// box facing the eye (BACKUP_TARGET_TOTAL = 30, inset 0.05).
std::vector<Vector3> AimAssist::buildBackupPoints(const BoundingBox& rawBox, const Vector3& eye) const
{
    constexpr double kInset = 0.05;
    constexpr int kTargetTotal = 30;

    const BoundingBox box = expandBox(rawBox, kBorderSize);
    const double sizeX = box.maxX - box.minX;
    const double sizeY = box.maxY - box.minY;
    const double sizeZ = box.maxZ - box.minZ;

    const bool xPos = eye.x > box.maxX, xNeg = eye.x < box.minX;
    const bool yPos = eye.y > box.maxY, yNeg = eye.y < box.minY;
    const bool zPos = eye.z > box.maxZ, zNeg = eye.z < box.minZ;

    const int visibleFaceCount =
        (xPos || xNeg ? 1 : 0) + (yPos || yNeg ? 1 : 0) + (zPos || zNeg ? 1 : 0);
    if (visibleFaceCount == 0)
        return {};

    const int pointsPerFace = kTargetTotal / visibleFaceCount;
    std::vector<Vector3> points;
    points.reserve(kTargetTotal + 6);

    auto addFaceGrid = [&](int fixedAxis, double fixedVal,
                           double uMin, double uMax, double vMin, double vMax,
                           int targetPoints, double dimU, double dimV)
    {
        if (dimU < 1e-4 || dimV < 1e-4)
        {
            const double uMid = (uMin + uMax) * 0.5;
            const double vMid = (vMin + vMax) * 0.5;
            if (fixedAxis == 0)      points.emplace_back(Vector3{ static_cast<float>(fixedVal), static_cast<float>(uMid), static_cast<float>(vMid) });
            else if (fixedAxis == 1) points.emplace_back(Vector3{ static_cast<float>(uMid), static_cast<float>(fixedVal), static_cast<float>(vMid) });
            else                     points.emplace_back(Vector3{ static_cast<float>(uMid), static_cast<float>(vMid), static_cast<float>(fixedVal) });
            return;
        }

        const double ratio = dimU / dimV;
        const int gridU = std::max(2, static_cast<int>(std::lround(std::sqrt(targetPoints * ratio))));
        const int gridV = std::max(2, static_cast<int>(std::lround(std::sqrt(targetPoints / ratio))));

        for (int i = 0; i < gridU; ++i)
        {
            const double u = uMin + (uMax - uMin) * i / (gridU - 1);
            for (int j = 0; j < gridV; ++j)
            {
                const double v = vMin + (vMax - vMin) * j / (gridV - 1);
                if (fixedAxis == 0)      points.emplace_back(Vector3{ static_cast<float>(fixedVal), static_cast<float>(u), static_cast<float>(v) });
                else if (fixedAxis == 1) points.emplace_back(Vector3{ static_cast<float>(u), static_cast<float>(fixedVal), static_cast<float>(v) });
                else                     points.emplace_back(Vector3{ static_cast<float>(u), static_cast<float>(v), static_cast<float>(fixedVal) });
            }
        }
    };

    if (xPos || xNeg)
        addFaceGrid(0, xPos ? box.maxX - kInset : box.minX + kInset,
                    box.minY + kInset, box.maxY - kInset,
                    box.minZ + kInset, box.maxZ - kInset,
                    pointsPerFace, sizeY, sizeZ);
    if (yPos || yNeg)
        addFaceGrid(1, yPos ? box.maxY - kInset : box.minY + kInset,
                    box.minX + kInset, box.maxX - kInset,
                    box.minZ + kInset, box.maxZ - kInset,
                    pointsPerFace, sizeX, sizeZ);
    if (zPos || zNeg)
        addFaceGrid(2, zPos ? box.maxZ - kInset : box.minZ + kInset,
                    box.minX + kInset, box.maxX - kInset,
                    box.minY + kInset, box.maxY - kInset,
                    pointsPerFace, sizeX, sizeY);

    return points;
}

// AimAssist#getEnemy — candidate filter + sort + (optionally) ray fallback.
bool AimAssist::getEnemy(AimTarget& out)
{
    CEntityPlayerSP* local = SDK::Minecraft ? SDK::Minecraft->thePlayer : nullptr;
    if (!local)
        return false;

    const Vector3 eye = local->GetEyePos();
    const Vector2 view = local->GetAngles();
    const float range = m_range->value;
    const float rangeSq = range * range;
    const int fovVal = static_cast<int>(m_fov->value);

    struct Candidate
    {
        CommonData::PlayerSnapshot player;
        BoundingBox box{};
        float distanceSq = 0.0f;
        float score = 0.0f;
    };

    std::vector<Candidate> candidates;

    for (const auto& pd : CommonData::SnapshotPlayers())
    {
        if (pd.isLocalPlayer || pd.health <= 0.0f)
            continue;
        const std::string& key = pd.name.empty() ? pd.displayName : pd.name;
        if (key.empty() || Friends::IsFriend(key))
            continue;
        if (!m_aimInvis->value && pd.isInvisible)
            continue;
        if (AntiBot::IsBot(key))
            continue;

        const BoundingBox rawBox = pd.boundingBox;
        const BoundingBox box = expandBox(rawBox, kBorderSize);
        const Vector3 cl = closestPointOnBox(box, eye);
        const Vector3 ecl = eye - cl;
        const float dsq = ecl.Length() * ecl.Length();
        if (dsq > rangeSq)
            continue;

        Candidate c;
        c.player = pd;
        c.box = rawBox;
        c.distanceSq = dsq;

        // FOV gate (raven: Utils.inFov with the view yaw).
        if (fovVal != 360)
        {
            const float angleToEntity = static_cast<float>(
                -std::atan2(pd.pos.x - eye.x, pd.pos.z - eye.z) * kRadToDeg);
            float diff = wrapTo180(view.x - angleToEntity);
            if (diff > fovVal * 0.5f || diff < -fovVal * 0.5f)
                continue;
        }

        // Sort score (raven comparators; lower = better).
        switch (m_sortMode->index)
        {
            case 0: // Health (lower first)
                c.score = pd.health + pd.absorptionAmount;
                break;
            case 1: // Angle (smaller first)
            {
                Vector3 ap = getAimPoint(pd, rawBox);
                float ty, tp;
                rotationsToPoint(eye, ap, view.x, view.y, ty, tp);
                c.score = std::fabs(wrapTo180(view.x - ty)) + std::fabs(view.y - tp);
                break;
            }
            default: // Distance (closer first)
                c.score = dsq;
                break;
        }

        candidates.push_back(std::move(c));
    }

    if (candidates.empty())
        return false;

    std::sort(candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b)
        {
            if (a.score != b.score)
                return a.score < b.score;
            return a.distanceSq < b.distanceSq;
        });

    // raven: when either "ignore behind ..." is on, walk the sorted list and
    // pick the first candidate with a valid aim point (main ray + backups).
    if (m_ignoreBehindWalls->value || m_ignoreBehindEntities->value)
    {
        const bool allowThroughBlocks = !m_ignoreBehindWalls->value;
        const bool allowThroughEntities = !m_ignoreBehindEntities->value;
        (void)allowThroughBlocks; // raven's canAimAtPoint stubs block checks.

        const std::vector<CommonData::PlayerSnapshot> all = CommonData::SnapshotPlayers();

        for (const auto& c : candidates)
        {
            const Vector3 mainPoint = getAimPoint(c.player, c.box);
            if (!mainRayHitsTargetAabb(eye, mainPoint, c.box, range))
                continue;

            // Entity occlusion: any other tracked player whose expanded box
            // blocks the eye->point ray closer than the target.
            if (!allowThroughEntities)
            {
                bool blocked = false;
                for (const auto& other : all)
                {
                    if (other.isLocalPlayer)
                        continue;
                    const std::string& ok = other.name.empty() ? other.displayName : other.name;
                    const std::string& ck = c.player.name.empty() ? c.player.displayName : c.player.name;
                    if (ok == ck)
                        continue;
                    if (rayIntersectsBox(eye, mainPoint, expandBox(other.boundingBox, kBorderSize)))
                    {
                        blocked = true;
                        break;
                    }
                }
                if (blocked)
                    continue;
            }

            out.player = c.player;
            out.aimPoint = mainPoint;
            out.distanceSq = c.distanceSq;
            out.name = c.player.name.empty() ? c.player.displayName : c.player.name;
            return true;
        }
        return false;
    }

    const Candidate& best = candidates.front();
    out.player = best.player;
    out.aimPoint = getAimPoint(best.player, best.box);
    out.distanceSq = best.distanceSq;
    out.name = best.player.name.empty() ? best.player.displayName : best.player.name;
    return true;
}

// RotationHelper#getRotationsToTarget (Normal mode) — multipoint aim point,
// optional ray fallback, then smoothRotation with randomization.
bool AimAssist::findRotations(const CommonData::PlayerSnapshot& pd, float& outYaw, float& outPitch)
{
    CEntityPlayerSP* local = SDK::Minecraft ? SDK::Minecraft->thePlayer : nullptr;
    if (!local)
        return false;

    const Vector3 eye = local->GetEyePos();
    const Vector2 view = local->GetAngles();
    const BoundingBox rawBox = pd.boundingBox;

    Vector3 mainPoint = getAimPoint(pd, rawBox);
    const bool useBackup = m_ignoreBehindWalls->value || m_ignoreBehindEntities->value;

    if (useBackup && !mainRayHitsTargetAabb(eye, mainPoint, rawBox, m_range->value))
    {
        // getRotationsWithBackup: main ray can't reach the aim point inside
        // `range` — fall back to the closest backup face point.
        std::vector<Vector3> backups = buildBackupPoints(rawBox, eye);
        std::sort(backups.begin(), backups.end(),
            [&eye](const Vector3& a, const Vector3& b)
            {
                const Vector3 da = a - eye, db = b - eye;
                return da.Length() < db.Length();
            });

        bool found = false;
        for (const Vector3& p : backups)
        {
            // raven's canAimAtPoint only rejects degenerate rays.
            const Vector3 d = p - eye;
            if (d.Length() < 1e-3f)
                continue;
            mainPoint = p;
            found = true;
            break;
        }
        if (!found)
            return false;
    }

    const Vector3 toPoint = mainPoint - eye;
    if (toPoint.Length() < 1e-3f)
        return false;

    float targetYaw, targetPitch;
    rotationsToPoint(eye, mainPoint, view.x, view.y, targetYaw, targetPitch);
    smoothRotation(view.x, view.y, targetYaw, targetPitch,
        static_cast<int>(m_speed->value), m_randomization->value,
        outYaw, outPitch);
    return true;
}

void AimAssist::applyRotation(float yaw, float pitch)
{
    CEntityPlayerSP* local = SDK::Minecraft ? SDK::Minecraft->thePlayer : nullptr;
    if (!local)
        return;

    // RotationUtils#fixRotation: snap the rotation delta onto the vanilla
    // mouse GCD grid so anticheats comparing consecutive deltas ("GCD is
    // incorrect") can't flag the rotations as impossible.
    const Vector2 current = local->GetAngles();
    float sens = 0.5f;
    if (SDK::Minecraft->gameSettings)
        sens = SDK::Minecraft->gameSettings->GetMouseSensitivity();

    const float f = sens * 0.6f + 0.2f;
    const float gcd = f * f * f * 8.0f;
    if (gcd > 1e-6f)
    {
        const float yawDelta = wrapTo180(yaw - current.x);
        yaw = current.x + std::round(yawDelta / gcd) * gcd;
        pitch = current.y + std::round((pitch - current.y) / gcd) * gcd;
    }

    pitch = clampd(pitch, -90.0f, 90.0f);
    local->SetAngles(Vector2{ Math::wrapAngleTo180(yaw), pitch });
}

void AimAssist::onEnable()
{
    m_miningStartTime = -1;
}

void AimAssist::onDisable()
{
    m_miningStartTime = -1;
}

// raven applies aim in onUpdate — the game's 20Hz tick. Our onTick hook is
// the same client-thread tick, so smoothRotation's per-step model applies
// exactly as designed.
void AimAssist::onTick()
{
    if (!conditionsMet())
        return;

    AimTarget enemy;
    if (!getEnemy(enemy))
        return;

    float yaw = 0.0f, pitch = 0.0f;
    if (!findRotations(enemy.player, yaw, pitch))
        return;

    applyRotation(yaw, pitch);
}
