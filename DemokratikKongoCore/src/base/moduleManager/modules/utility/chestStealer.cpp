#include "chestStealer.h"
#include "../../../sdk/sdk.h"
#include "../../../sdk/strayCache.h"
#include "../../../java/java.h"
#include "../../../util/logger.h"
#include "../../../sdk/jniResolve.h"
#include <random>

namespace {
    // Everything below used to be re-resolved on every client tick while a
    // container was open (and getStack once per slot). Classes are pinned as
    // globals through the game ClassLoader; the field/method IDs are stable for
    // the lifetime of the process once resolved.
    jclass g_guiChestClass = nullptr;
    jclass g_guiContainerClass = nullptr;

    jfieldID  g_openContainerFid = nullptr;
    jfieldID  g_windowIdFid = nullptr;
    jfieldID  g_inventorySlotsFid = nullptr;
    jmethodID g_getStackMid = nullptr;

    jfieldID resolveField(JNIEnv* env, jclass cls, const char* sig,
                          const char* mcpName, const char* srgName)
    {
        jfieldID id = env->GetFieldID(cls, mcpName, sig);
        JniResolve::ClearException(env);
        if (!id)
        {
            id = env->GetFieldID(cls, srgName, sig);
            JniResolve::ClearException(env);
        }
        return id;
    }
}

ChestStealer::ChestStealer()
    : Module("ChestStealer", "Automatically loots items from opened container chests.", Category::Utility)
{
    m_minDelay = &add<NumberSetting>("Min Delay", 100.0f, 0.0f, 500.0f, 10.0f);
    m_minDelay->description = "Minimum delay in milliseconds between looting slots.";

    m_maxDelay = &add<NumberSetting>("Max Delay", 200.0f, 0.0f, 500.0f, 10.0f);
    m_maxDelay->description = "Maximum delay in milliseconds between looting slots.";

    m_autoClose = &add<BoolSetting>("Auto Close", true);
    m_autoClose->description = "Automatically close container when empty or looted.";

    m_trashFilter = &add<BoolSetting>("Filter Trash", true);
    m_trashFilter->description = "Only loot valuable items (weapons, armor, food, golden apples, materials).";

    setEnabled(false);
}

void ChestStealer::onEnable()
{
    Logger::Info("ChestStealer", "Enabled");
    m_lastStealTime = std::chrono::steady_clock::now();
}

void ChestStealer::onDisable()
{
    Logger::Info("ChestStealer", "Disabled");
}

bool ChestStealer::isItemValuable(JNIEnv* env, jobject itemStack)
{
    if (!itemStack) return false;
    if (!m_trashFilter->value) return true;

    jclass stackCls = env->GetObjectClass(itemStack);
    if (!stackCls) return true;

    jmethodID getItemMid = env->GetMethodID(stackCls, "getItem", "()Lnet/minecraft/item/Item;");
    if (!getItemMid) {
        env->ExceptionClear(); // NoSuchMethodError would poison the next lookup
        getItemMid = env->GetMethodID(stackCls, "func_77973_b", "()Lnet/minecraft/item/Item;");
        if (!getItemMid) env->ExceptionClear();
    }

    jobject itemObj = nullptr;
    if (getItemMid) itemObj = env->CallObjectMethod(itemStack, getItemMid);
    if (env->ExceptionCheck()) { env->ExceptionClear(); itemObj = nullptr; }
    env->DeleteLocalRef(stackCls);

    if (!itemObj) return true;

    // Classes cached as global refs — FindClass per stack per tick was hot.
    static jclass swordCls = nullptr, armorCls = nullptr, toolCls = nullptr,
        foodCls = nullptr, potionCls = nullptr, blockCls = nullptr;
    static bool s_classesResolved = false;
    if (!s_classesResolved)
    {
        s_classesResolved = true;
        auto cacheGlobal = [env](const char* name) -> jclass {
            jclass local = env->FindClass(name);
            if (!local) { env->ExceptionClear(); return nullptr; }
            jclass global = (jclass)env->NewGlobalRef(local);
            env->DeleteLocalRef(local);
            return global;
        };
        swordCls  = cacheGlobal("net/minecraft/item/ItemSword");
        armorCls  = cacheGlobal("net/minecraft/item/ItemArmor");
        toolCls   = cacheGlobal("net/minecraft/item/ItemTool");
        foodCls   = cacheGlobal("net/minecraft/item/ItemFood");
        potionCls = cacheGlobal("net/minecraft/item/ItemPotion");
        blockCls  = cacheGlobal("net/minecraft/item/ItemBlock");
    }

    // Default false: anything that is NOT a known valuable class is trash.
    // (Previously initialized to true, making Filter Trash a complete no-op.)
    bool valuable = false;
    if (swordCls && env->IsInstanceOf(itemObj, swordCls)) valuable = true;
    else if (armorCls && env->IsInstanceOf(itemObj, armorCls)) valuable = true;
    else if (toolCls && env->IsInstanceOf(itemObj, toolCls)) valuable = true;
    else if (foodCls && env->IsInstanceOf(itemObj, foodCls)) valuable = true;
    else if (potionCls && env->IsInstanceOf(itemObj, potionCls)) valuable = true;
    else if (blockCls && env->IsInstanceOf(itemObj, blockCls)) valuable = true;

    env->DeleteLocalRef(itemObj);

    return valuable;
}

void ChestStealer::clientTick()
{
    if (!isEnabled() || !SDK::Minecraft || !SDK::Minecraft->thePlayer) return;

    JNIEnv* env = Java::GetEnv();
    if (!env || !StrayCache::minecraft_currentScreen) return;

    jobject mcObj = SDK::Minecraft->GetInstance();
    if (!mcObj) return;

    jobject currentScreen = env->GetObjectField(mcObj, StrayCache::minecraft_currentScreen);
    if (env->ExceptionCheck()) { env->ExceptionClear(); currentScreen = nullptr; }

    if (!currentScreen) return;

    // Check if open screen is GuiChest or Container GUI
    Java::AssignClass("net.minecraft.client.gui.inventory.GuiChest", g_guiChestClass);
    Java::AssignClass("net.minecraft.client.gui.inventory.GuiContainer", g_guiContainerClass);

    bool isChestScreen = false;
    if (g_guiChestClass && env->IsInstanceOf(currentScreen, g_guiChestClass)) isChestScreen = true;
    else if (g_guiContainerClass && env->IsInstanceOf(currentScreen, g_guiContainerClass)) isChestScreen = true;
    JniResolve::ClearException(env);

    if (!isChestScreen)
    {
        env->DeleteLocalRef(currentScreen);
        return;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastStealTime).count();
    if (elapsed < m_nextDelayMs)
    {
        env->DeleteLocalRef(currentScreen);
        return;
    }

    // Get openContainer on player
    jobject playerObj = SDK::Minecraft->GetThePlayerObject();
    if (!playerObj) { env->DeleteLocalRef(currentScreen); return; }

    if (!g_openContainerFid)
    {
        jclass playerClass = env->GetObjectClass(playerObj);
        if (playerClass)
        {
            g_openContainerFid = resolveField(env, playerClass, "Lnet/minecraft/inventory/Container;",
                                              "openContainer", "field_71070_bA");
            env->DeleteLocalRef(playerClass);
        }
    }

    jobject openContainer = g_openContainerFid ? env->GetObjectField(playerObj, g_openContainerFid) : nullptr;
    JniResolve::ClearException(env);

    if (!openContainer)
    {
        env->DeleteLocalRef(playerObj);
        env->DeleteLocalRef(currentScreen);
        return;
    }

    if (!g_windowIdFid || !g_inventorySlotsFid)
    {
        jclass containerClass = env->GetObjectClass(openContainer);
        if (containerClass)
        {
            if (!g_windowIdFid)
                g_windowIdFid = resolveField(env, containerClass, "I", "windowId", "field_75152_c");
            if (!g_inventorySlotsFid)
                g_inventorySlotsFid = resolveField(env, containerClass, "Ljava/util/List;",
                                                   "inventorySlots", "field_75151_b");
            env->DeleteLocalRef(containerClass);
        }
    }

    int windowId = g_windowIdFid ? env->GetIntField(openContainer, g_windowIdFid) : 0;
    jobject inventorySlots = g_inventorySlotsFid ? env->GetObjectField(openContainer, g_inventorySlotsFid) : nullptr;
    JniResolve::ClearException(env);

    bool lootedAny = false;
    bool hasItemsLeft = false;

    if (inventorySlots && StrayCache::list_class && StrayCache::list_toArray)
    {
        jobjectArray slotsArray = (jobjectArray)env->CallObjectMethod(inventorySlots, StrayCache::list_toArray);
        if (env->ExceptionCheck()) { env->ExceptionClear(); slotsArray = nullptr; }

        if (slotsArray)
        {
            jsize slotCount = env->GetArrayLength(slotsArray);
            // Lower chest inventory slots are 0 to slotCount - 36
            int chestSlotCount = slotCount > 36 ? slotCount - 36 : slotCount;

            for (int i = 0; i < chestSlotCount; i++)
            {
                jobject slotObj = env->GetObjectArrayElement(slotsArray, i);
                if (!slotObj) continue;

                if (!g_getStackMid)
                {
                    jclass slotClass = env->GetObjectClass(slotObj);
                    if (slotClass)
                    {
                        g_getStackMid = env->GetMethodID(slotClass, "getStack", "()Lnet/minecraft/item/ItemStack;");
                        JniResolve::ClearException(env);
                        if (!g_getStackMid)
                        {
                            g_getStackMid = env->GetMethodID(slotClass, "func_75211_c", "()Lnet/minecraft/item/ItemStack;");
                            JniResolve::ClearException(env);
                        }
                        env->DeleteLocalRef(slotClass);
                    }
                }

                jobject stackObj = g_getStackMid ? env->CallObjectMethod(slotObj, g_getStackMid) : nullptr;
                JniResolve::ClearException(env);
                env->DeleteLocalRef(slotObj);

                if (stackObj)
                {
                    hasItemsLeft = true;
                    if (isItemValuable(env, stackObj))
                    {
                        // Quick move (shift click) slot i
                        jobject controllerObj = env->GetObjectField(mcObj, StrayCache::minecraft_playerController);
                        if (controllerObj && StrayCache::playerControllerMP_windowClick)
                        {
                            // windowClick returns the dragged ItemStack — must drop
                            // the local ref or the table leaks one slot per click.
                            jobject clickResult = env->CallObjectMethod(controllerObj, StrayCache::playerControllerMP_windowClick,
                                windowId, i, 0, 1, playerObj);
                            if (env->ExceptionCheck()) env->ExceptionClear();
                            if (clickResult) env->DeleteLocalRef(clickResult);
                            env->DeleteLocalRef(controllerObj);
                        }

                        env->DeleteLocalRef(stackObj);
                        lootedAny = true;
                        break;
                    }
                    env->DeleteLocalRef(stackObj);
                }
            }
            env->DeleteLocalRef(slotsArray);
        }
        env->DeleteLocalRef(inventorySlots);
    }

    m_lastStealTime = std::chrono::steady_clock::now();

    // Randomize next delay
    float minD = (std::min)(m_minDelay->value, m_maxDelay->value);
    float maxD = (std::max)(m_minDelay->value, m_maxDelay->value);
    if (maxD <= minD) maxD = minD + 1.0f;

    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist((int)minD, (int)maxD);
    m_nextDelayMs = dist(gen);

    // Auto close container if empty or completely looted
    if (m_autoClose->value && (!hasItemsLeft || !lootedAny))
    {
        static jmethodID s_closeScreenMid = nullptr;
        if (!s_closeScreenMid)
        {
            jclass playerSPClass = env->GetObjectClass(playerObj);
            if (playerSPClass)
            {
                s_closeScreenMid = env->GetMethodID(playerSPClass, "closeScreen", "()V");
                JniResolve::ClearException(env);
                if (!s_closeScreenMid)
                {
                    s_closeScreenMid = env->GetMethodID(playerSPClass, "func_71053_j", "()V");
                    JniResolve::ClearException(env);
                }
                env->DeleteLocalRef(playerSPClass);
            }
        }
        if (s_closeScreenMid)
        {
            env->CallVoidMethod(playerObj, s_closeScreenMid);
            JniResolve::ClearException(env);
        }
    }

    env->DeleteLocalRef(openContainer);
    env->DeleteLocalRef(playerObj);
    env->DeleteLocalRef(currentScreen);
}
