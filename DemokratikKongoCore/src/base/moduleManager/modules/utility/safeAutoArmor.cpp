#include "safeAutoArmor.h"

#include "../combat/combatBridge.h"
#include "../../../java/java.h"
#include "../../../sdk/jniResolve.h"
#include "../../../sdk/sdk.h"
#include "../../../sdk/strayCache.h"
#include "../../../patcher/patcher.h"
#include "../../../util/format.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>

namespace
{
    inline long long nowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    thread_local std::mt19937 g_rng{ std::random_device{}() };

    inline jobject asJObject(void* ptr)
    {
        return reinterpret_cast<jobject>(ptr);
    }
}

SafeAutoArmor::SafeAutoArmor()
    : Module("SafeAutoArmor", "Equips hotbar armor through vanilla item-use flow without inventory clicks.", Category::Utility)
{
    // Two checkboxes — both can be on simultaneously.
    m_hotbarMode = &add<BoolSetting>("Hotbar Mode", true);
    m_hotbarMode->description = "Right-click equip armor from hotbar (GUI closed).";

    m_inventoryMode = &add<BoolSetting>("Inventory Mode", false);
    m_inventoryMode->description = "Shift-click swap low-durability armor while inventory open.";

    // --- Hotbar mode settings ---
    m_onlyOnBreak = &add<BoolSetting>("onlyOnBreak", false);

    m_minDelay = &add<NumberSetting>("minDelay", 80.0f, 0.0f, 500.0f, 5.0f);
    m_minDelay->suffix = " ms";

    m_maxDelay = &add<NumberSetting>("maxDelay", 150.0f, 0.0f, 750.0f, 5.0f);
    m_maxDelay->suffix = " ms";

    m_dynamicCombat = &add<BoolSetting>("dynamicCombat", true);

    // --- Inventory mode settings ---
    m_threshold = &add<NumberSetting>("Durability Threshold", 15.0f, 1.0f, 100.0f, 1.0f);
    m_threshold->suffix = "%";
    m_threshold->description = "Swap equipped armor when remaining durability drops below this %.";

    m_swapDelay = &add<IntRangeSetting>("Swap Delay", 50, 150, 0, 1000, 1);
    m_swapDelay->suffix = "ms";

    m_startDelay = &add<IntRangeSetting>("Start Delay", 100, 200, 0, 1000, 1);
    m_startDelay->suffix = "ms";

    m_dropOld = &add<BoolSetting>("Drop Old Armor", true);
    m_dropOld->description = "Drop replaced armor on the ground instead of keeping it in hotbar.";

    // Show/hide settings based on which mode checkbox is on
    auto hotbarOn = [this] { return m_hotbarMode && m_hotbarMode->value; };
    auto invOn    = [this] { return m_inventoryMode && m_inventoryMode->value; };
    m_onlyOnBreak->visible   = hotbarOn;
    m_minDelay->visible      = hotbarOn;
    m_maxDelay->visible       = hotbarOn;
    m_dynamicCombat->visible = hotbarOn;
    m_threshold->visible     = invOn;
    m_swapDelay->visible      = invOn;
    m_startDelay->visible    = invOn;
    m_dropOld->visible       = invOn;
}

std::string SafeAutoArmor::arrayListSuffix(SuffixDetail detail) const
{
    if (detail == SuffixDetail::None)
        return "";

    if (detail == SuffixDetail::Basic)
    {
        std::string out;
        if (m_hotbarMode && m_hotbarMode->value) out += "H";
        if (m_inventoryMode && m_inventoryMode->value) out += (out.empty() ? "I" : "+I");
        return out.empty() ? "Off" : out;
    }

    char buf[96];
    if (m_hotbarMode && m_hotbarMode->value && m_inventoryMode && m_inventoryMode->value)
    {
        std::snprintf(buf, sizeof(buf), "H %.0f-%.0fms | I %.0f%%",
            m_minDelay->value, m_maxDelay->value, m_threshold->value);
    }
    else if (m_inventoryMode && m_inventoryMode->value)
    {
        std::snprintf(buf, sizeof(buf), "Inv %.0f%% %d-%dms",
            m_threshold->value,
            m_swapDelay ? m_swapDelay->getLow() : 0,
            m_swapDelay ? m_swapDelay->getHigh() : 0);
    }
    else
    {
        std::snprintf(buf, sizeof(buf), "%s %.0f-%.0fms%s",
            stateName(), m_minDelay->value, m_maxDelay->value,
            m_dynamicCombat->value ? " [Dynamic]" : "");
    }
    return buf;
}

const char* SafeAutoArmor::stateName() const
{
    switch (m_state)
    {
        case State::Idle: return "IDLE";
        case State::SwapToArmor: return "SWAP";
        case State::RightClick: return "USE";
        case State::SwapBack: return "BACK";
        case State::Cooldown: return "COOLDOWN";
        default: return "?";
    }
}

bool SafeAutoArmor::canRun() const
{
    return CombatBridge::InGame()
        && SDK::Minecraft
        && SDK::Minecraft->thePlayer
        && !SDK::Minecraft->IsInGuiState()
        && StrayCache::entityPlayer_inventory
        && StrayCache::inventoryPlayer_currentItem
        && StrayCache::inventoryPlayer_mainInventory
        && StrayCache::itemStack_getItem
        && StrayCache::item_getIdFromItem
        && StrayCache::entityPlayer_getCurrentArmor;
}

void SafeAutoArmor::scheduleNext(long long now, float extraMultiplier)
{
    float minDelay = std::max(0.0f, m_minDelay->value);
    float maxDelay = std::max(minDelay, m_maxDelay->value);

    std::uniform_real_distribution<float> dist(minDelay, maxDelay);
    float delay = dist(g_rng) * extraMultiplier;

    if (m_dynamicCombat->value && recentlyHurt(now))
        delay *= 0.85f;

    m_nextActionMs = now + static_cast<long long>(std::lround(std::max(0.0f, delay)));
}

bool SafeAutoArmor::recentlyHurt(long long now) const
{
    return m_lastHurtMs > 0 && now - m_lastHurtMs <= 1000;
}

void SafeAutoArmor::updateCombatTimer(long long now)
{
    if (!SDK::Minecraft || !SDK::Minecraft->thePlayer || !StrayCache::entityLivingBase_hurtTime)
        return;

    JNIEnv* env = Java::GetEnv();
    jobject player = SDK::Minecraft->GetThePlayerObject();
    if (!env || !player)
        return;

    const int hurtTime = env->GetIntField(player, StrayCache::entityLivingBase_hurtTime);
    JniResolve::ClearException(env);
    if (hurtTime > 0)
        m_lastHurtMs = now;

    env->DeleteLocalRef(player);
}

int SafeAutoArmor::armorSlotFromItemId(int itemId) const
{
    switch (itemId)
    {
        // Boots
        case 301: case 305: case 309: case 313: case 317: return 0;
        // Leggings
        case 300: case 304: case 308: case 312: case 316: return 1;
        // Chestplate
        case 299: case 303: case 307: case 311: case 315: return 2;
        // Helmet
        case 298: case 302: case 306: case 310: case 314: return 3;
        default: return -1;
    }
}

int SafeAutoArmor::baseArmorScoreFromItemId(int itemId) const
{
    switch (itemId)
    {
        // Leather
        case 298: case 299: case 300: case 301: return 10;
        // Chain
        case 302: case 303: case 304: case 305: return 30;
        // Iron
        case 306: case 307: case 308: case 309: return 40;
        // Diamond
        case 310: case 311: case 312: case 313: return 50;
        // Gold
        case 314: case 315: case 316: case 317: return 20;
        default: return 0;
    }
}

int SafeAutoArmor::durabilityRemaining(void* stackPtr) const
{
    jobject stack = asJObject(stackPtr);
    JNIEnv* env = Java::GetEnv();
    if (!env || !stack || !StrayCache::itemStack_getMaxDamage || !StrayCache::itemStack_getItemDamage)
        return 0;

    const int maxDamage = env->CallIntMethod(stack, StrayCache::itemStack_getMaxDamage);
    JniResolve::ClearException(env);
    if (maxDamage <= 0)
        return 0;

    const int damage = env->CallIntMethod(stack, StrayCache::itemStack_getItemDamage);
    JniResolve::ClearException(env);
    return std::max(0, maxDamage - damage);
}

int SafeAutoArmor::enchantLevel(int enchantmentId, void* stackPtr) const
{
    jobject stack = asJObject(stackPtr);
    JNIEnv* env = Java::GetEnv();
    if (!env || !stack || !StrayCache::enchantmentHelper_class || !StrayCache::enchantmentHelper_getEnchantmentLevel)
        return 0;

    const int level = env->CallStaticIntMethod(
        StrayCache::enchantmentHelper_class,
        StrayCache::enchantmentHelper_getEnchantmentLevel,
        enchantmentId,
        stack);
    JniResolve::ClearException(env);
    return std::max(0, level);
}

int SafeAutoArmor::scoreArmorStack(void* stackPtr, int itemId, int* outDurability) const
{
    const int durability = durabilityRemaining(stackPtr);
    if (outDurability)
        *outDurability = durability;

    int score = baseArmorScoreFromItemId(itemId);
    if (score <= 0)
        return 0;

    // Minecraft 1.8 enchantment IDs:
    // 0 Protection, 1 Fire Protection, 3 Blast Protection, 4 Projectile Protection.
    score += enchantLevel(0, stackPtr) * 5;
    score += enchantLevel(1, stackPtr) * 2;
    score += enchantLevel(3, stackPtr) * 2;
    score += enchantLevel(4, stackPtr) * 2;
    return score;
}

bool SafeAutoArmor::readArmorPresence(std::array<bool, 4>& outPresence) const
{
    outPresence = { false, false, false, false };

    JNIEnv* env = Java::GetEnv();
    jobject player = SDK::Minecraft ? SDK::Minecraft->GetThePlayerObject() : nullptr;
    if (!env || !player || !StrayCache::entityPlayer_getCurrentArmor)
        return false;

    for (int slot = 0; slot < 4; ++slot)
    {
        jobject stack = env->CallObjectMethod(player, StrayCache::entityPlayer_getCurrentArmor, slot);
        JniResolve::ClearException(env);
        outPresence[slot] = stack != nullptr;
        if (stack)
            env->DeleteLocalRef(stack);
    }

    env->DeleteLocalRef(player);
    return true;
}

bool SafeAutoArmor::shouldFillArmorSlot(int armorSlot, const std::array<bool, 4>& presence) const
{
    if (armorSlot < 0 || armorSlot >= 4)
        return false;
    if (presence[armorSlot])
        return false;

    if (!m_onlyOnBreak->value)
        return true;

    return m_presenceInitialized && m_hadArmor[armorSlot] && !presence[armorSlot];
}

bool SafeAutoArmor::readHotbarArmorCandidate(int hotbarSlot, ArmorCandidate& outCandidate) const
{
    if (hotbarSlot < 0 || hotbarSlot > 8)
        return false;

    JNIEnv* env = Java::GetEnv();
    jobject player = SDK::Minecraft ? SDK::Minecraft->GetThePlayerObject() : nullptr;
    if (!env || !player)
        return false;

    jobject inv = env->GetObjectField(player, StrayCache::entityPlayer_inventory);
    JniResolve::ClearException(env);
    if (!inv)
    {
        env->DeleteLocalRef(player);
        return false;
    }

    jobjectArray mainInv = (jobjectArray)env->GetObjectField(inv, StrayCache::inventoryPlayer_mainInventory);
    JniResolve::ClearException(env);
    if (!mainInv)
    {
        env->DeleteLocalRef(inv);
        env->DeleteLocalRef(player);
        return false;
    }

    jobject stack = env->GetObjectArrayElement(mainInv, hotbarSlot);
    JniResolve::ClearException(env);
    bool ok = false;

    if (stack)
    {
        jobject item = env->CallObjectMethod(stack, StrayCache::itemStack_getItem);
        JniResolve::ClearException(env);
        if (item)
        {
            const int itemId = env->CallStaticIntMethod(StrayCache::item_class, StrayCache::item_getIdFromItem, item);
            JniResolve::ClearException(env);

            const int armorSlot = armorSlotFromItemId(itemId);
            int durability = 0;
            const int score = scoreArmorStack(stack, itemId, &durability);
            if (armorSlot >= 0 && score > 0)
            {
                outCandidate.hotbarSlot = hotbarSlot;
                outCandidate.armorSlot = armorSlot;
                outCandidate.itemId = itemId;
                outCandidate.score = score;
                outCandidate.durability = durability;
                ok = true;
            }
            env->DeleteLocalRef(item);
        }
        env->DeleteLocalRef(stack);
    }

    env->DeleteLocalRef(mainInv);
    env->DeleteLocalRef(inv);
    env->DeleteLocalRef(player);
    return ok;
}

bool SafeAutoArmor::scanBestCandidate(ArmorCandidate& outCandidate)
{
    std::array<bool, 4> presence{};
    if (!readArmorPresence(presence))
        return false;

    if (!m_presenceInitialized)
    {
        m_hadArmor = presence;
        m_presenceInitialized = true;
        if (m_onlyOnBreak->value)
            return false;
    }

    bool found = false;
    ArmorCandidate best{};

    for (int slot = 0; slot < 9; ++slot)
    {
        ArmorCandidate candidate{};
        if (!readHotbarArmorCandidate(slot, candidate))
            continue;
        if (!shouldFillArmorSlot(candidate.armorSlot, presence))
            continue;

        if (!found
            || candidate.score > best.score
            || (candidate.score == best.score && candidate.durability > best.durability))
        {
            found = true;
            best = candidate;
        }
    }

    m_hadArmor = presence;

    if (!found)
        return false;

    outCandidate = best;
    return true;
}

int SafeAutoArmor::getCurrentHotbarSlot() const
{
    JNIEnv* env = Java::GetEnv();
    jobject player = SDK::Minecraft ? SDK::Minecraft->GetThePlayerObject() : nullptr;
    if (!env || !player || !StrayCache::entityPlayer_inventory || !StrayCache::inventoryPlayer_currentItem)
        return -1;

    jobject inv = env->GetObjectField(player, StrayCache::entityPlayer_inventory);
    JniResolve::ClearException(env);
    if (!inv)
    {
        env->DeleteLocalRef(player);
        return -1;
    }

    int slot = env->GetIntField(inv, StrayCache::inventoryPlayer_currentItem);
    JniResolve::ClearException(env);

    env->DeleteLocalRef(inv);
    env->DeleteLocalRef(player);

    if (slot < 0 || slot > 8)
        return -1;
    return slot;
}

bool SafeAutoArmor::setHotbarSlot(int slot) const
{
    if (slot < 0 || slot > 8)
        return false;

    JNIEnv* env = Java::GetEnv();
    jobject player = SDK::Minecraft ? SDK::Minecraft->GetThePlayerObject() : nullptr;
    if (!env || !player || !StrayCache::entityPlayer_inventory || !StrayCache::inventoryPlayer_currentItem)
        return false;

    jobject inv = env->GetObjectField(player, StrayCache::entityPlayer_inventory);
    JniResolve::ClearException(env);
    if (!inv)
    {
        env->DeleteLocalRef(player);
        return false;
    }

    env->DeleteLocalRef(inv);
    env->DeleteLocalRef(player);

    // Do not call InventoryPlayer.currentItem setters or
    // PlayerControllerMP.syncCurrentPlayItem() here. Those create a direct
    // held-item-change path from the cheat loop, which can be observed as a
    // post/out-of-order packet. Let Minecraft consume a normal hotbar key
    // input in its own input/tick flow instead.
    CombatBridge::SelectHotbarSlot(slot);
    return true;
}

bool SafeAutoArmor::isStillValidCandidate(const ArmorCandidate& candidate) const
{
    ArmorCandidate current{};
    if (!readHotbarArmorCandidate(candidate.hotbarSlot, current))
        return false;

    std::array<bool, 4> presence{};
    if (!readArmorPresence(presence))
        return false;

    return current.itemId == candidate.itemId
        && current.armorSlot == candidate.armorSlot
        && !presence[candidate.armorSlot];
}

void SafeAutoArmor::resetState(bool restoreSlot)
{
    if (restoreSlot && m_originalHotbarSlot >= 0 && getCurrentHotbarSlot() != m_originalHotbarSlot)
        setHotbarSlot(m_originalHotbarSlot);

    m_state = State::Idle;
    m_candidate = ArmorCandidate{};
    m_originalHotbarSlot = -1;
    m_nextActionMs = 0;
}

void SafeAutoArmor::onEnable()
{
    // Patcher keys are pushed on the first onTick; this is just a hook
    // point if we later want to do one-shot setup.
}

void SafeAutoArmor::onTick()
{
    if (!isEnabled())
    {
        Patcher::put("autoarmor_mode", "0");
        return;
    }

    // --- Inventory mode (checkbox): push settings to Java ---
    // RuntimeBridge.tickAutoArmor runs the actual shift-click swap on the
    // client thread (onWalkingUpdatePre), right before tickRefill.
    bool invOn = m_inventoryMode && m_inventoryMode->value;
    if (invOn)
    {
        Patcher::put("autoarmor_mode",      "1");
        Patcher::put("autoarmor_threshold",  fstr(m_threshold->value));
        Patcher::put("autoarmor_swap_delay_min",  istr(m_swapDelay->getLow()));
        Patcher::put("autoarmor_swap_delay_max",  istr(m_swapDelay->getHigh()));
        Patcher::put("autoarmor_start_delay_min", istr(m_startDelay->getLow()));
        Patcher::put("autoarmor_start_delay_max", istr(m_startDelay->getHigh()));
        Patcher::put("autoarmor_drop_old",   bstr(m_dropOld->value));
    }
    else
    {
        Patcher::put("autoarmor_mode", "0");
    }

    // --- Hotbar mode (checkbox): run the C++ state machine ---
    // Both modes can be active at the same time. If inventory mode is on
    // but hotbar mode is off, skip the state machine entirely.
    if (!m_hotbarMode || !m_hotbarMode->value)
    {
        resetState(true);
        return;
    }

    const long long now = nowMs();

    if (!canRun())
    {
        resetState(true);
        return;
    }

    updateCombatTimer(now);

    if (now < m_nextActionMs)
        return;

    switch (m_state)
    {
        case State::Idle:
        {
            ArmorCandidate candidate{};
            if (!scanBestCandidate(candidate))
                return;

            m_originalHotbarSlot = getCurrentHotbarSlot();
            if (m_originalHotbarSlot < 0)
                return;

            m_candidate = candidate;
            m_state = State::SwapToArmor;
            scheduleNext(now);
            break;
        }

        case State::SwapToArmor:
        {
            if (!isStillValidCandidate(m_candidate))
            {
                resetState(true);
                return;
            }

            if (!setHotbarSlot(m_candidate.hotbarSlot))
            {
                resetState(true);
                return;
            }

            m_state = State::RightClick;
            scheduleNext(now);
            break;
        }

        case State::RightClick:
        {
            if (!isStillValidCandidate(m_candidate) || getCurrentHotbarSlot() != m_candidate.hotbarSlot)
            {
                resetState(true);
                return;
            }

            // Same principle as slot switching: do not call
            // Minecraft.rightClickMouse() from the cheat loop. Post a normal
            // RMB input and let Minecraft's own input loop produce any
            // resulting use-item action in order.
            CombatBridge::PostRightClick();
            m_state = State::SwapBack;
            scheduleNext(now);
            break;
        }

        case State::SwapBack:
        {
            if (m_originalHotbarSlot >= 0)
                setHotbarSlot(m_originalHotbarSlot);

            std::array<bool, 4> presence{};
            if (readArmorPresence(presence))
                m_hadArmor = presence;

            m_state = State::Cooldown;
            scheduleNext(now, 1.75f);
            break;
        }

        case State::Cooldown:
        {
            resetState(false);
            break;
        }
    }
}

void SafeAutoArmor::onDisable()
{
    resetState(true);
    m_presenceInitialized = false;
    m_hadArmor = { false, false, false, false };
    m_lastHurtMs = 0;
    Patcher::put("autoarmor_mode", "0");
}
