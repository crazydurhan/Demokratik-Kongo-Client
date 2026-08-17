#include "autoTool.h"

#include "../combat/combatBridge.h"
#include "../../../sdk/sdk.h"
#include "../../../sdk/jniResolve.h"
#include "../../../sdk/strayCache.h"
#include "../../../sdk/net/minecraft/client/Minecraft.h"
#include "../../../java/java.h"
#include "../../../util/logger.h"

#include <Windows.h>
#include <chrono>
#include <string>

namespace
{
    inline long long nowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    struct ItemClassCache
    {
        jclass itemSwordClass   = nullptr;
        jclass itemToolClass    = nullptr;
        jclass itemPickaxeClass = nullptr;
        jclass itemAxeClass     = nullptr;
        jclass itemSpadeClass   = nullptr;
        jclass itemShearsClass = nullptr;
        jclass itemHoeClass     = nullptr;
        jclass itemBlockClass   = nullptr;
        jclass blockClass       = nullptr;
        jmethodID getStrVsBlock = nullptr;
        jmethodID getBlock      = nullptr;
        jmethodID getIdFromBlock = nullptr;
    };

    ItemClassCache& itemCache()
    {
        static ItemClassCache cache;
        return cache;
    }

    void initItemCache(JNIEnv* env)
    {
        ItemClassCache& c = itemCache();
        if (c.blockClass) return;
        if (!env) return;

        Java::AssignClass("net.minecraft.item.ItemSword", c.itemSwordClass);
        Java::AssignClass("net.minecraft.item.ItemTool", c.itemToolClass);
        Java::AssignClass("net.minecraft.item.ItemPickaxe", c.itemPickaxeClass);
        Java::AssignClass("net.minecraft.item.ItemAxe", c.itemAxeClass);
        Java::AssignClass("net.minecraft.item.ItemSpade", c.itemSpadeClass);
        Java::AssignClass("net.minecraft.item.ItemShears", c.itemShearsClass);
        Java::AssignClass("net.minecraft.item.ItemHoe", c.itemHoeClass);
        Java::AssignClass("net.minecraft.item.ItemBlock", c.itemBlockClass);
        Java::AssignClass("net.minecraft.block.Block", c.blockClass);

        if (StrayCache::itemStack_class && c.blockClass)
        {
            c.getStrVsBlock = JniResolve::Method(env, StrayCache::itemStack_class,
                "(Lnet/minecraft/block/Block;)F", "getStrVsBlock");
            c.getIdFromBlock = JniResolve::StaticMethod(env, c.blockClass,
                "(Lnet/minecraft/block/Block;)I", "getIdFromBlock");
        }
    }

    // Returns Block.getIdFromBlock(block), or -1 when unresolvable.
    int blockIdOf(JNIEnv* env, jobject blockObj)
    {
        if (!env || !blockObj) return -1;
        initItemCache(env);
        ItemClassCache& c = itemCache();
        if (!c.getIdFromBlock || !c.blockClass) return -1;
        const int id = env->CallStaticIntMethod(c.blockClass, c.getIdFromBlock, blockObj);
        JniResolve::ClearException(env);
        return id;
    }

    bool blockAllowedByFilter(JNIEnv* env, jobject blockObj, int filterMode)
    {
        const int id = blockIdOf(env, blockObj);
        if (id < 0)
        {
            // Unresolvable block id (SRG/mapping miss): fail closed would make
            // the module dead; treat as allowed so only explicit filters apply.
            return true;
        }

        auto isOre = [id]
        {
            switch (id)
            {
                case 14: case 15: case 16: case 21:            // gold, iron, coal, lapis
                case 56: case 73: case 74: case 129: case 153: // diamond, redstone(lit), emerald, quartz
                    return true;
                default:
                    return false;
            }
        };
        auto isWood = [id]
        {
            return id == 5 || id == 17 || id == 162 || id == 126 || id == 53
                || id == 134 || id == 135 || id == 136 || id == 163 || id == 164; // planks, logs, slabs/stairs
        };
        auto isStone = [id]
        {
            return id == 1 || id == 4 || id == 48 || id == 67 || id == 109 || id == 98; // stone, cobble, mossy, stairs, stonebrick
        };

        switch (filterMode)
        {
            case 0: return isOre();                                   // Ores Only
            case 1: return isOre() || isStone();                      // Ores & Stone
            case 2: return isWood();                                  // Wood Only
            case 3: return isOre() || id == 49;                       // Valuables: ores + obsidian
            default: return true;
        }
    }

    enum class HeldCategory
    {
        Sword,
        Tool,
        Fists,
        Other
    };

    HeldCategory categorizeStack(JNIEnv* env, jobject itemStack)
    {
        if (!itemStack) return HeldCategory::Fists;

        initItemCache(env);
        ItemClassCache& c = itemCache();

        if (StrayCache::itemStack_getItem)
        {
            jobject item = env->CallObjectMethod(itemStack, StrayCache::itemStack_getItem);
            JniResolve::ClearException(env);
            if (item)
            {
                bool isSword = c.itemSwordClass && env->IsInstanceOf(item, c.itemSwordClass);
                bool isTool  = (c.itemToolClass    && env->IsInstanceOf(item, c.itemToolClass))
                            || (c.itemPickaxeClass && env->IsInstanceOf(item, c.itemPickaxeClass))
                            || (c.itemAxeClass     && env->IsInstanceOf(item, c.itemAxeClass))
                            || (c.itemSpadeClass   && env->IsInstanceOf(item, c.itemSpadeClass))
                            || (c.itemShearsClass  && env->IsInstanceOf(item, c.itemShearsClass))
                            || (c.itemHoeClass     && env->IsInstanceOf(item, c.itemHoeClass));

                env->DeleteLocalRef(item);

                if (isSword) return HeldCategory::Sword;
                if (isTool)  return HeldCategory::Tool;
            }
        }

        return HeldCategory::Other;
    }
}

AutoTool::AutoTool()
    : Module("Auto Tool", "Automatically selects the correct tool when mining a block.", Category::Utility)
{
    m_activationDelay = &add<NumberSetting>("Activation Time", 0.0f, 0.0f, 1000.0f, 10.0f);
    m_activationDelay->suffix = " ms";
    m_activationDelay->description = "Minimum duration holding down mouse button before Auto Tool activates (0 = instant).";

    m_switchBack = &add<BoolSetting>("Switch Back When Done", true);
    m_switchBack->description = "Switches back to your previous hotbar slot when done mining.";

    m_allowSword = &add<BoolSetting>("Allow Sword", true);
    m_allowSword->description = "Allow Auto Tool to activate while holding a sword.";

    m_allowTool = &add<BoolSetting>("Allow Tool", true);
    m_allowTool->description = "Allow Auto Tool to activate while holding another tool.";

    m_allowFists = &add<BoolSetting>("Allow Fists", true);
    m_allowFists->description = "Allow Auto Tool to activate while holding nothing (fists).";

    m_allowOther = &add<BoolSetting>("Allow Other", true);
    m_allowOther->description = "Allow Auto Tool to activate while holding any other item.";

    m_sneakOnly = &add<BoolSetting>("Sneak Only", false);
    m_sneakOnly->description = "Only activate Auto Tool while sneaking (prevents accidental switches during combat).";

    m_restrictBlocks = &add<BoolSetting>("Restrict Allowed Blocks", false);
    m_restrictBlocks->description = "Only activate Auto Tool for specific block types.";

    m_blockFilterMode = &add<EnumSetting>("Allowed Blocks Mode",
        std::vector<const char*>{ "Ores Only", "Ores & Stone", "Wood Only", "Valuables Only" }, 0);
    m_blockFilterMode->visible = [this] { return m_restrictBlocks && m_restrictBlocks->value; };

    setEnabled(false);
}

void AutoTool::onEnable()
{
    resetState();
}

void AutoTool::onDisable()
{
    JNIEnv* env = Java::GetEnv();
    resetState(env);
}

void AutoTool::resetState(JNIEnv* env)
{
    if (m_switchBack && m_switchBack->value && m_originalSlot >= 0 && m_originalSlot <= 8)
    {
        if (!env) env = Java::GetEnv();
        if (env && CombatBridge::InGame() && SDK::Minecraft && SDK::Minecraft->thePlayer)
        {
            jobject player = SDK::Minecraft->GetThePlayerObject();
            if (player && StrayCache::entityPlayer_inventory && StrayCache::inventoryPlayer_currentItem)
            {
                jobject inv = env->GetObjectField(player, StrayCache::entityPlayer_inventory);
                JniResolve::ClearException(env);
                if (inv)
                {
        env->SetIntField(inv, StrayCache::inventoryPlayer_currentItem, m_originalSlot);
                    JniResolve::ClearException(env);
                    CombatBridge::RequestHotbarSlot(m_originalSlot);
                    env->DeleteLocalRef(inv);
                }
                env->DeleteLocalRef(player);
            }
        }
    }

    m_originalSlot = -1;
    m_isMining = false;
    m_miningStartMs = 0;
}

void AutoTool::clientTick()
{
    if (!isEnabled() || !CombatBridge::InGame() || !SDK::Minecraft || !SDK::Minecraft->thePlayer)
    {
        resetState();
        return;
    }

    if (SDK::Minecraft->IsInGuiState())
    {
        resetState();
        return;
    }

    JNIEnv* env = Java::GetEnv();
    if (!env) return;

    // Check if left mouse button is held down
    const bool isLeftClicking = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

    jobject player = SDK::Minecraft->GetThePlayerObject();
    if (!player) return;

    // Verify objectMouseOver targets a block
    bool isTargetingBlock = false;
    jobject blockObj = nullptr;

    if (StrayCache::minecraft_objectMouseOver && StrayCache::movingObjectPosition_typeOfHit)
    {
        jobject mouseOver = env->GetObjectField(SDK::Minecraft->GetInstance(), StrayCache::minecraft_objectMouseOver);
        JniResolve::ClearException(env);
        if (mouseOver)
        {
            jobject typeOfHit = env->GetObjectField(mouseOver, StrayCache::movingObjectPosition_typeOfHit);
            JniResolve::ClearException(env);
            if (typeOfHit)
            {
                // Inspect typeOfHit ordinal. 1.8.9 MovingObjectType: MISS=0, BLOCK=1, ENTITY=2
                // Enum.ordinal() is inherited from java/lang/Enum, so the ID is
                // stable and only needs resolving once (this used to run every
                // mining tick).
                static jmethodID s_ordinalMid = nullptr;
                if (!s_ordinalMid)
                {
                    jclass enumClass = env->GetObjectClass(typeOfHit);
                    if (enumClass)
                    {
                        s_ordinalMid = env->GetMethodID(enumClass, "ordinal", "()I");
                        JniResolve::ClearException(env);
                        env->DeleteLocalRef(enumClass);
                    }
                }
                if (s_ordinalMid)
                {
                    const int ordinal = env->CallIntMethod(typeOfHit, s_ordinalMid);
                    JniResolve::ClearException(env);
                    if (ordinal == 1) // BLOCK
                    {
                        isTargetingBlock = true;
                    }
                }
                env->DeleteLocalRef(typeOfHit);
            }

            if (isTargetingBlock && StrayCache::movingObjectPosition_blockPos)
            {
                jobject blockPos = env->GetObjectField(mouseOver, StrayCache::movingObjectPosition_blockPos);
                JniResolve::ClearException(env);
                if (blockPos)
                {
                    jobject world = SDK::Minecraft->GetTheWorldObject();
                    if (world)
                    {
                        jclass worldClass = env->GetObjectClass(world);
                        jmethodID getBlockStateMid = JniResolve::Method(env, worldClass,
                            "(Lnet/minecraft/util/BlockPos;)Lnet/minecraft/block/state/IBlockState;", "getBlockState");
                        if (getBlockStateMid)
                        {
                            jobject blockState = env->CallObjectMethod(world, getBlockStateMid, blockPos);
                            JniResolve::ClearException(env);
                            if (blockState)
                            {
                                jclass blockStateClass = env->GetObjectClass(blockState);
                                jmethodID getBlockMid = JniResolve::Method(env, blockStateClass,
                                    "()Lnet/minecraft/block/Block;", "getBlock");
                                if (getBlockMid)
                                {
                                    blockObj = env->CallObjectMethod(blockState, getBlockMid);
                                    JniResolve::ClearException(env);
                                }
                                env->DeleteLocalRef(blockStateClass);
                                env->DeleteLocalRef(blockState);
                            }
                        }
                        env->DeleteLocalRef(worldClass);
                        env->DeleteLocalRef(world);
                    }
                    env->DeleteLocalRef(blockPos);
                }
            }

            env->DeleteLocalRef(mouseOver);
        }
    }

    const bool activelyMining = isLeftClicking && isTargetingBlock;

    if (!activelyMining)
    {
        if (m_isMining)
        {
            resetState(env);
        }
        env->DeleteLocalRef(player);
        if (blockObj) env->DeleteLocalRef(blockObj);
        return;
    }

    // Check Sneak Only condition
    if (m_sneakOnly->value)
    {
        bool isSneaking = false;
        if (StrayCache::entity_isSneaking)
        {
            isSneaking = env->CallBooleanMethod(player, StrayCache::entity_isSneaking) == JNI_TRUE;
            JniResolve::ClearException(env);
        }
        if (!isSneaking)
        {
            env->DeleteLocalRef(player);
            if (blockObj) env->DeleteLocalRef(blockObj);
            return;
        }
    }

    // Check inventory and held item category
    if (!StrayCache::entityPlayer_inventory || !StrayCache::inventoryPlayer_currentItem || !StrayCache::inventoryPlayer_mainInventory)
    {
        env->DeleteLocalRef(player);
        if (blockObj) env->DeleteLocalRef(blockObj);
        return;
    }

    jobject inv = env->GetObjectField(player, StrayCache::entityPlayer_inventory);
    JniResolve::ClearException(env);
    if (!inv)
    {
        env->DeleteLocalRef(player);
        if (blockObj) env->DeleteLocalRef(blockObj);
        return;
    }

    const int currentSlot = env->GetIntField(inv, StrayCache::inventoryPlayer_currentItem);
    JniResolve::ClearException(env);

    jobjectArray mainInv = (jobjectArray)env->GetObjectField(inv, StrayCache::inventoryPlayer_mainInventory);
    JniResolve::ClearException(env);
    if (!mainInv)
    {
        env->DeleteLocalRef(inv);
        env->DeleteLocalRef(player);
        if (blockObj) env->DeleteLocalRef(blockObj);
        return;
    }

    // Inspect currently held item category
    jobject currentStack = (currentSlot >= 0 && currentSlot <= 8)
        ? env->GetObjectArrayElement(mainInv, currentSlot) : nullptr;
    JniResolve::ClearException(env);

    HeldCategory category = categorizeStack(env, currentStack);
    if (currentStack) env->DeleteLocalRef(currentStack);

    bool categoryAllowed = true;
    switch (category)
    {
    case HeldCategory::Sword: categoryAllowed = m_allowSword->value; break;
    case HeldCategory::Tool:  categoryAllowed = m_allowTool->value;  break;
    case HeldCategory::Fists: categoryAllowed = m_allowFists->value; break;
    case HeldCategory::Other: categoryAllowed = m_allowOther->value; break;
    }

    if (!categoryAllowed)
    {
        env->DeleteLocalRef(mainInv);
        env->DeleteLocalRef(inv);
        env->DeleteLocalRef(player);
        if (blockObj) env->DeleteLocalRef(blockObj);
        return;
    }

    // Restrict Allowed Blocks filter — previously a dead setting.
    if (m_restrictBlocks->value)
    {
        const int filterMode = m_blockFilterMode ? m_blockFilterMode->index : 0;
        if (!blockAllowedByFilter(env, blockObj, filterMode))
        {
            if (m_isMining)
                resetState(env);
            env->DeleteLocalRef(mainInv);
            env->DeleteLocalRef(inv);
            env->DeleteLocalRef(player);
            if (blockObj) env->DeleteLocalRef(blockObj);
            return;
        }
    }

    // Handle Activation Delay
    const long long now = nowMs();
    if (!m_isMining)
    {
        m_isMining = true;
        m_miningStartMs = now;
    }

    if (now - m_miningStartMs < static_cast<long long>(m_activationDelay->value))
    {
        env->DeleteLocalRef(mainInv);
        env->DeleteLocalRef(inv);
        env->DeleteLocalRef(player);
        if (blockObj) env->DeleteLocalRef(blockObj);
        return;
    }

    // Find best tool in hotbar (slots 0..8)
    initItemCache(env);
    ItemClassCache& c = itemCache();

    int bestSlot = -1;
    float bestSpeed = 1.0f;

    if (c.getStrVsBlock && blockObj)
    {
        for (int slot = 0; slot < 9; ++slot)
        {
            jobject stack = env->GetObjectArrayElement(mainInv, slot);
            JniResolve::ClearException(env);
            if (!stack) continue;

            const float speed = env->CallFloatMethod(stack, c.getStrVsBlock, blockObj);
            JniResolve::ClearException(env);

            if (speed > bestSpeed)
            {
                bestSpeed = speed;
                bestSlot = slot;
            }
            env->DeleteLocalRef(stack);
        }
    }
    else
    {
        // Fallback: search for explicit tool items (Pickaxe/Axe/Spade/Shears)
        for (int slot = 0; slot < 9; ++slot)
        {
            jobject stack = env->GetObjectArrayElement(mainInv, slot);
            JniResolve::ClearException(env);
            if (!stack) continue;

            const HeldCategory cat = categorizeStack(env, stack);
            if (cat == HeldCategory::Tool)
            {
                bestSlot = slot;
                env->DeleteLocalRef(stack);
                break;
            }
            env->DeleteLocalRef(stack);
        }
    }

    // Execute tool switch
    if (bestSlot >= 0 && bestSlot != currentSlot)
    {
        if (m_originalSlot == -1)
        {
            m_originalSlot = currentSlot;
        }

        CombatBridge::RequestHotbarSlot(bestSlot);
    }

    env->DeleteLocalRef(mainInv);
    env->DeleteLocalRef(inv);
    env->DeleteLocalRef(player);
    if (blockObj) env->DeleteLocalRef(blockObj);
}
