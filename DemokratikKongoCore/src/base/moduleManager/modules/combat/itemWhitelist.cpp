#include "itemWhitelist.h"

#include "../../commonData.h"
#include "../../../sdk/sdk.h"
#include "../../../sdk/strayCache.h"
#include "../../../java/java.h"
#include "../../../sdk/jniResolve.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace ItemWhitelist {

namespace {

struct MatchContext
{
    std::string displayName;
    std::string nameLower;
    int itemId = -1;
    int itemMeta = 0;
    bool isBlock = false;
    int hotbarSlot = 0;
    bool emptyHand = false;
};

struct HeldSnapshot
{
    std::string name;
    int itemId = -1;
    int itemMeta = 0;
    bool isBlock = false;
    int hotbarSlot = 0;
    bool valid = false;
    bool emptyHand = false;
};

std::string stripFormattingCodes(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0xC2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xA7) {
            i += 2;
            if (i < s.size()) ++i;
            continue;
        }
        if (c == 0xA7 || c == '&') {
            if (i + 1 < s.size()) ++i;
            continue;
        }
        out.push_back(static_cast<char>(c));
    }
    return out;
}

std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string normalizeTerm(std::string term)
{
    std::replace(term.begin(), term.end(), '_', ' ');
    return term;
}

bool isSwordId(int itemId)
{
    return itemId == 267 || itemId == 268 || itemId == 272 || itemId == 276 || itemId == 283;
}

bool isAxeId(int itemId)
{
    return itemId == 271 || itemId == 275 || itemId == 258 || itemId == 286 || itemId == 279;
}

bool isPickaxeId(int itemId)
{
    return itemId == 270 || itemId == 274 || itemId == 257 || itemId == 285 || itemId == 278;
}

bool isShovelId(int itemId)
{
    return itemId == 269 || itemId == 273 || itemId == 256 || itemId == 284 || itemId == 277;
}

bool isPickaxeName(const std::string& nameLower)
{
    return nameLower.find("pickaxe") != std::string::npos
        || nameLower.find("kazma") != std::string::npos;
}

bool isAxeName(const std::string& nameLower)
{
    if (isPickaxeName(nameLower))
        return false;
    return nameLower.find("axe") != std::string::npos
        || nameLower.find("balta") != std::string::npos;
}

bool isShovelName(const std::string& nameLower)
{
    return nameLower.find("shovel") != std::string::npos
        || nameLower.find("spade") != std::string::npos
        || nameLower.find("kurek") != std::string::npos
        || nameLower.find("k\u00fcrek") != std::string::npos;
}

bool isSwordName(const std::string& nameLower)
{
    return nameLower.find("sword") != std::string::npos
        || nameLower.find("kilic") != std::string::npos
        || nameLower.find("k\u0131l\u0131\u00e7") != std::string::npos;
}

MatchContext contextFromSnapshot(const HeldSnapshot& snap)
{
    MatchContext ctx{};
    ctx.displayName = snap.name;
    ctx.nameLower = toLower(stripFormattingCodes(snap.name));
    ctx.itemId = snap.itemId;
    ctx.itemMeta = snap.itemMeta;
    ctx.isBlock = snap.isBlock;
    ctx.hotbarSlot = snap.hotbarSlot;
    ctx.emptyHand = snap.emptyHand;
    return ctx;
}

MatchContext contextFromDisplayName(const std::string& displayName)
{
    MatchContext ctx{};
    ctx.displayName = displayName;
    ctx.nameLower = toLower(stripFormattingCodes(displayName));
    ctx.emptyHand = ctx.nameLower.empty();
    return ctx;
}

HeldSnapshot snapshotFromJni()
{
    HeldSnapshot snap{};
    if (!SDK::Minecraft || !SDK::Minecraft->thePlayer)
        return snap;

    JNIEnv* env = Java::GetEnv();
    if (!env)
        return snap;

    jobject playerObj = SDK::Minecraft->thePlayer->GetInstance();
    if (!playerObj)
        return snap;

    if (StrayCache::entityPlayer_inventory && StrayCache::inventoryPlayer_currentItem) {
        jobject invObj = env->GetObjectField(playerObj, StrayCache::entityPlayer_inventory);
        JniResolve::ClearException(env);
        if (invObj) {
            snap.hotbarSlot = env->GetIntField(invObj, StrayCache::inventoryPlayer_currentItem);
            JniResolve::ClearException(env);
            env->DeleteLocalRef(invObj);
        }
    }

    if (!StrayCache::entityLivingBase_getHeldItem || !StrayCache::itemStack_getDisplayName) {
        snap.valid = true;
        return snap;
    }

    jobject heldStack = env->CallObjectMethod(playerObj, StrayCache::entityLivingBase_getHeldItem);
    JniResolve::ClearException(env);
    if (!heldStack) {
        snap.valid = true;
        snap.emptyHand = true;
        return snap;
    }

    jstring dName = (jstring)env->CallObjectMethod(heldStack, StrayCache::itemStack_getDisplayName);
    JniResolve::ClearException(env);
    if (dName) {
        const char* chars = env->GetStringUTFChars(dName, nullptr);
        if (chars) {
            snap.name = chars;
            env->ReleaseStringUTFChars(dName, chars);
        }
        env->DeleteLocalRef(dName);
    }

    if (StrayCache::itemStack_getItemDamage) {
        snap.itemMeta = env->CallIntMethod(heldStack, StrayCache::itemStack_getItemDamage);
        JniResolve::ClearException(env);
    }

    if (StrayCache::itemStack_getItem) {
        jobject itemObj = env->CallObjectMethod(heldStack, StrayCache::itemStack_getItem);
        JniResolve::ClearException(env);
        if (itemObj) {
            if (StrayCache::item_getIdFromItem && StrayCache::item_class) {
                snap.itemId = env->CallStaticIntMethod(StrayCache::item_class, StrayCache::item_getIdFromItem, itemObj);
                JniResolve::ClearException(env);
            }
            if (StrayCache::itemBlock_class) {
                snap.isBlock = env->IsInstanceOf(itemObj, StrayCache::itemBlock_class) == JNI_TRUE;
            }
            env->DeleteLocalRef(itemObj);
        }
    }

    env->DeleteLocalRef(heldStack);
    snap.valid = true;
    snap.emptyHand = snap.name.empty() && snap.itemId <= 0;
    return snap;
}

HeldSnapshot snapshotLocalHeld()
{
    HeldSnapshot snap = snapshotFromJni();

    if (!CommonData::SanityCheck())
        return snap;

    std::lock_guard<std::mutex> lock(CommonData::playerListMutex);
    snap.hotbarSlot = CommonData::localHotbarSlot;

    for (const auto& pd : CommonData::nativePlayerList) {
        if (!pd.isLocalPlayer)
            continue;

        if (!pd.equipmentItems.empty()) {
            const auto& held = pd.equipmentItems[0];
            if (!held.displayName.empty())
                snap.name = held.displayName;
            if (held.itemId > 0)
                snap.itemId = held.itemId;
            snap.itemMeta = held.itemMeta;
            snap.isBlock = held.isBlockItem;
        } else {
            for (const auto& eq : pd.equipmentNames) {
                if (eq.rfind("Held: ", 0) == 0) {
                    snap.name = eq.substr(6);
                    break;
                }
            }
        }

        snap.valid = true;
        snap.emptyHand = snap.name.empty() && snap.itemId <= 0;
        break;
    }

    return snap;
}

bool tokenMatches(const std::string& rawTerm, const MatchContext& ctx)
{
    std::string term = normalizeTerm(toLower(rawTerm));
    if (term.empty())
        return false;

    if (term == "hand")
        return ctx.emptyHand;

    if (term == "swords" || term == "sword") {
        if (ctx.itemId >= 0 && isSwordId(ctx.itemId))
            return true;
        return isSwordName(ctx.nameLower);
    }

    if (term == "pickaxes" || term == "pickaxe") {
        if (ctx.itemId >= 0 && isPickaxeId(ctx.itemId))
            return true;
        return isPickaxeName(ctx.nameLower);
    }

    if (term == "axes" || term == "axe") {
        if (ctx.itemId >= 0 && isAxeId(ctx.itemId))
            return true;
        return isAxeName(ctx.nameLower);
    }

    if (term == "shovels" || term == "shovel" || term == "spade") {
        if (ctx.itemId >= 0 && isShovelId(ctx.itemId))
            return true;
        return isShovelName(ctx.nameLower);
    }

    if (term == "potions" || term == "potion")
        return ctx.nameLower.find("potion") != std::string::npos
            || ctx.nameLower.find("iksir") != std::string::npos;

    if (term == "pearls" || term == "pearl")
        return ctx.nameLower.find("pearl") != std::string::npos
            || ctx.nameLower.find("ender") != std::string::npos;

    if (term == "apples" || term == "apple")
        return ctx.nameLower.find("apple") != std::string::npos
            || ctx.nameLower.find("gapple") != std::string::npos
            || ctx.nameLower.find("elma") != std::string::npos;

    if (term == "bows" || term == "bow")
        return ctx.nameLower.find("bow") != std::string::npos
            || ctx.nameLower.find("yay") != std::string::npos;

    if (term == "arrows" || term == "arrow")
        return ctx.nameLower.find("arrow") != std::string::npos;

    if (term == "food") {
        return ctx.nameLower.find("apple") != std::string::npos
            || ctx.nameLower.find("beef") != std::string::npos
            || ctx.nameLower.find("steak") != std::string::npos
            || ctx.nameLower.find("porkchop") != std::string::npos
            || ctx.nameLower.find("chicken") != std::string::npos
            || ctx.nameLower.find("mutton") != std::string::npos
            || ctx.nameLower.find("rabbit") != std::string::npos
            || ctx.nameLower.find("bread") != std::string::npos
            || ctx.nameLower.find("cookie") != std::string::npos
            || ctx.nameLower.find("melon") != std::string::npos
            || ctx.nameLower.find("potato") != std::string::npos
            || ctx.nameLower.find("carrot") != std::string::npos
            || ctx.nameLower.find("soup") != std::string::npos
            || ctx.nameLower.find("stew") != std::string::npos
            || ctx.nameLower.find("fish") != std::string::npos
            || ctx.nameLower.find("salmon") != std::string::npos
            || ctx.nameLower.find("pie") != std::string::npos
            || ctx.nameLower.find("elma") != std::string::npos
            || ctx.nameLower.find("ekmek") != std::string::npos;
    }

    if (term == "blocks" || term == "block") {
        if (ctx.isBlock)
            return true;
        return ctx.nameLower.find("block") != std::string::npos
            || ctx.nameLower.find("stone") != std::string::npos
            || ctx.nameLower.find("dirt") != std::string::npos
            || ctx.nameLower.find("planks") != std::string::npos
            || ctx.nameLower.find("obsidian") != std::string::npos;
    }

    if (term.rfind("slot ", 0) == 0) {
        try {
            const int targetSlot = std::stoi(term.substr(5));
            return ctx.hotbarSlot == (targetSlot - 1);
        } catch (...) {}
        return false;
    }

    bool isNumeric = !term.empty();
    bool hasColon = false;
    for (char c : term) {
        if (c == ':')
            hasColon = true;
        else if (!std::isdigit(static_cast<unsigned char>(c)))
            isNumeric = false;
    }

    if (isNumeric || (hasColon && term.find_first_not_of("0123456789:") == std::string::npos)) {
        if (ctx.itemId < 0)
            return false;

        if (hasColon) {
            const size_t colonPos = term.find(':');
            try {
                const int targetId = std::stoi(term.substr(0, colonPos));
                const int targetMeta = std::stoi(term.substr(colonPos + 1));
                return ctx.itemId == targetId && ctx.itemMeta == targetMeta;
            } catch (...) {}
            return false;
        }

        try {
            const int targetId = std::stoi(term);
            return ctx.itemId == targetId;
        } catch (...) {}
        return false;
    }

    if (ctx.nameLower.empty())
        return false;

    if (ctx.nameLower.find(term) != std::string::npos)
        return true;

    if (term.size() > 3 && term.back() == 's') {
        const std::string singular = term.substr(0, term.size() - 1);
        if (ctx.nameLower.find(singular) != std::string::npos)
            return true;
    }

    return false;
}

bool matchConfigList(const MatchContext& ctx, const std::string& configList, bool emptyMeansAllowAll)
{
    if (configList.empty())
        return emptyMeansAllowAll;

    std::stringstream ss(configList);
    std::string token;

    while (std::getline(ss, token, ',')) {
        size_t start = token.find_first_not_of(" \t\r\n");
        size_t end = token.find_last_not_of(" \t\r\n");
        if (start == std::string::npos)
            continue;

        const std::string term = token.substr(start, end - start + 1);
        if (tokenMatches(term, ctx))
            return true;
    }

    return false;
}

} // namespace

std::string GetHeldDisplayName()
{
    const HeldSnapshot snap = snapshotLocalHeld();
    return snap.valid ? snap.name : std::string{};
}

int GetHeldHotbarSlot()
{
    const HeldSnapshot snap = snapshotLocalHeld();
    return snap.valid ? snap.hotbarSlot : 0;
}

bool IsAllowed(const std::string& heldItemName, const std::string& configList)
{
    if (configList.empty())
        return false;

    HeldSnapshot snap = snapshotLocalHeld();
    if (!heldItemName.empty()) {
        if (snap.name.empty())
            snap.name = heldItemName;
        else if (snap.itemId < 0)
            snap.name = heldItemName;
    }

    const MatchContext ctx = contextFromSnapshot(snap);
    return matchConfigList(ctx, configList, false);
}

bool MatchesDisplayName(const std::string& displayName, const std::string& configList)
{
    if (configList.empty())
        return false;

    const MatchContext ctx = contextFromDisplayName(displayName);
    return matchConfigList(ctx, configList, false);
}

bool MatchesItem(const std::string& displayName, int itemId, int itemMeta,
                 bool isBlock, const std::string& configList)
{
    if (configList.empty())
        return false;

    MatchContext ctx = contextFromDisplayName(displayName);
    ctx.itemId = itemId;
    ctx.itemMeta = itemMeta;
    ctx.isBlock = isBlock;
    ctx.emptyHand = false;
    return matchConfigList(ctx, configList, false);
}

} // namespace ItemWhitelist
