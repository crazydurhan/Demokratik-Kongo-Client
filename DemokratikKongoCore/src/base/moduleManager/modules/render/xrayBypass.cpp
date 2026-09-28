#include "xrayBypass.h"

#include "../combat/combatBridge.h"
#include "../../../java/java.h"
#include "imgui.h"

#include "../../commonData.h"
#include "../../../sdk/jniResolve.h"
#include "../../../util/math/worldToScreen.h"
#include "../../../util/format.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cctype>
#include <cmath>
#include <array>
#include <mutex>
#include <set>

#include "../../../util/logger.h"

namespace
{
    constexpr int kBlocksPerStep = 3000;   // JNI block reads per scan step

    unsigned int oreColor(const std::string& lower)
    {
        if (lower.find("diamond") != std::string::npos) return IM_COL32( 90, 230, 255, 255);
        if (lower.find("emerald") != std::string::npos) return IM_COL32(  60, 240, 120, 255);
        if (lower.find("gold")    != std::string::npos) return IM_COL32( 250, 210,  60, 255);
        if (lower.find("iron")    != std::string::npos) return IM_COL32( 215, 175, 150, 255);
        if (lower.find("redstone")!= std::string::npos) return IM_COL32( 255,  70,  70, 255);
        if (lower.find("lapis")   != std::string::npos) return IM_COL32(  80, 110, 255, 255);
        if (lower.find("coal")    != std::string::npos) return IM_COL32(  60,  60,  60, 255);
        if (lower.find("quartz")  != std::string::npos) return IM_COL32( 235, 225, 220, 255);
        if (lower.find("debris")  != std::string::npos) return IM_COL32( 140, 100,  90, 255);
        return IM_COL32(180, 180, 180, 255);
    }

    struct ScanDiag
    {
        int bpFail = 0, stateFail = 0, blockFail = 0, noMatch = 0, matched = 0, processed = 0;
        bool bpExLogged = false, stateExLogged = false, blockExLogged = false;

        void logException(JNIEnv* env, const char* stage, bool& logged)
        {
            if (logged || !env || !env->ExceptionCheck()) return;
            logged = true;
            jthrowable ex = env->ExceptionOccurred();
            if (!ex) return;
            jclass exCls = env->GetObjectClass(ex);
            jmethodID toStr = env->GetMethodID(exCls, "toString", "()Ljava/lang/String;");
            if (jstring msg = (jstring)env->CallObjectMethod(ex, toStr))
            {
                const char* u = env->GetStringUTFChars(msg, nullptr);
                Logger::Error("XrayBypass", std::string(stage) + " failed: " + (u ? u : "?"));
                env->ReleaseStringUTFChars(msg, u);
                env->DeleteLocalRef(msg);
            }
            env->DeleteLocalRef(exCls);
            env->DeleteLocalRef(ex);
        }
        void summarize()
        {
            Logger::Info("XrayBypass",
                "diag: processed=" + std::to_string(processed)
                + " bpFail=" + std::to_string(bpFail)
                + " stateFail=" + std::to_string(stateFail)
                + " blockFail=" + std::to_string(blockFail)
                + " noMatch=" + std::to_string(noMatch)
                + " matched=" + std::to_string(matched));
        }
    };
    ScanDiag g_diag;

    inline long long nowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    std::string lowerCopy(const std::string& s)
    {
        std::string out = s;
        std::transform(out.begin(), out.end(), out.begin(),
            [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        return out;
    }
}

XrayBypass::XrayBypass()
    : Module("XrayBypass",
             "Marks ores the server actually sent. SmartScan only shows exposed ores, which anti-xray never fakes.",
             Category::Render)
{
    m_rangeXZ = &add<NumberSetting>("RangeXZ", 20.0f, 4.0f, 48.0f, 1.0f);
    m_rangeXZ->suffix = " blocks";
    m_expandUp = &add<NumberSetting>("ExpandUP", 20.0f, 0.0f, 48.0f, 1.0f);
    m_expandUp->suffix = " blocks";
    m_expandDown = &add<NumberSetting>("ExpandDown", 20.0f, 0.0f, 48.0f, 1.0f);
    m_expandDown->suffix = " blocks";
    m_delay = &add<NumberSetting>("Delay", 3.0f, 1.0f, 20.0f, 1.0f);
    m_delay->suffix = " ticks";

    m_smartScan = &add<BoolSetting>("SmartScan", true);
    m_scanDelay = &add<NumberSetting>("ScanDelay", 1000.0f, 200.0f, 5000.0f, 100.0f);
    m_scanDelay->suffix = " ms";
    m_scanDelay->visible = [this]{ return m_smartScan->value; };

    m_allBlocks = &add<BoolSetting>("AllBlocks", false);
    m_verifyOn = &add<BoolSetting>("Verify", true);
    m_onlyReal = &add<BoolSetting>("OnlyReal", false);
    m_packetScan = &add<BoolSetting>("PacketScan", false);
    m_probeDelay = &add<NumberSetting>("ProbeDelay", 150.0f, 100.0f, 2000.0f, 50.0f);
    m_probeDelay->suffix = " ms";
    m_probeDelay->visible = [this]{ return m_packetScan->value; };
    m_probeRange = &add<NumberSetting>("ProbeRange", 32.0f, 8.0f, 128.0f, 4.0f);
    m_probeRange->suffix = " blk";
    m_probeRange->visible = [this]{ return m_packetScan->value; };
    m_mode = &add<EnumSetting>("Mode", std::vector<const char*>{ "Outline", "Fill" }, 0);
    m_whitelist = &add<StringSetting>("Whitelist", "diamond,emerald,iron,gold,redstone,lapis,coal,quartz");
    m_whitelist->itemList = true;
    m_whitelist->visible = [this]{ return !m_allBlocks->value; };
    m_color = &add<ColorSetting>("Block Color", Color{ 0.35f, 0.9f, 0.95f, 0.9f });

    setEnabled(false);
}

std::string XrayBypass::arrayListSuffix(SuffixDetail detail) const
{
    if (detail == SuffixDetail::None)
        return "";
    if (detail == SuffixDetail::Basic)
        return m_allBlocks->value ? "All" : "Ores";
    return m_allBlocks->value ? "All" : "Smart";
}

void XrayBypass::onEnable()
{
    classInit();
    {
        std::lock_guard<std::mutex> lock(m_marksMutex);
        m_marks.clear();
        m_nextMarks.clear();
    }
    m_passDone = false;
    m_lastScanMs = 0;   // first tick starts a fresh pass from the current position
    m_lastStepMs = 0;
    m_lastVerifyMs = 0;
    m_probeAwait = false;
    m_probeNextMs = 0;
    m_probed.clear();
    m_probeSentCount = 0;
    m_probeRemovedCount = 0;
    m_probeIdleLogged = false;
    m_probeCapLogged = false;
    Logger::Info("XrayBypass", "enabled — one-shot scan from current position");
}

void XrayBypass::onDisable()
{
    // marks are intentionally kept for viewing after the one-shot scan ends
    Logger::Info("XrayBypass", std::string("disabled (scan ") + (m_passDone ? "finished)" : "incomplete)"));
}

bool XrayBypass::classInit()
{
    JNIEnv* env = Java::GetEnv();
    if (!env)
        return false;

    if (!m_getBlockState)
    {
        CWorld* world = SDK::Minecraft ? SDK::Minecraft->theWorld : nullptr;
        if (!world || !world->GetInstance())
        {
            static long long s_lastW = 0;
            if (nowMs() - s_lastW > 5000)
            {
                s_lastW = nowMs();
                Logger::Warn("XrayBypass", "classInit: theWorld or world instance is null");
            }
            return false;
        }

        jclass worldCls = env->GetObjectClass(world->GetInstance());
        if (!worldCls)
            return false;
        m_getBlockState = JniResolve::Method(env, worldCls,
            "(Lnet/minecraft/util/BlockPos;)Lnet/minecraft/block/state/IBlockState;", "getBlockState");
        env->DeleteLocalRef(worldCls);
    }

    if (!m_blockPosClass && Java::AssignClass("net/minecraft/util/BlockPos", m_blockPosClass))
    {
        // AssignClass hands out a global ref; keep it.
        m_blockPosCtor = env->GetMethodID(m_blockPosClass, "<init>", "(III)V");
    }

    if (!m_getBlock)
    {
        jclass ibs = nullptr;
        if (Java::AssignClass("net/minecraft/block/state/IBlockState", ibs))
            m_getBlock = JniResolve::Method(env, ibs,
                "()Lnet/minecraft/block/Block;", "getBlock");
        if (ibs) env->DeleteLocalRef(ibs);
    }

    const bool ok = m_getBlockState && m_blockPosCtor && m_getBlock;
    {
        static long long s_lastWarn = 0;
        if (!ok && nowMs() - s_lastWarn > 5000)
        {
            s_lastWarn = nowMs();
            Logger::Warn("XrayBypass", "JNI resolution incomplete: getBlockState="
                + std::to_string(m_getBlockState != nullptr)
                + " blockPosCtor=" + std::to_string(m_blockPosCtor != nullptr)
                + " getBlock=" + std::to_string(m_getBlock != nullptr)
                + " unlocalizedName=" + std::to_string(m_getUnlocalizedName != nullptr));
        }
        else if (ok && s_lastWarn != 0)
        {
            Logger::Info("XrayBypass", "JNI methods resolved.");
            s_lastWarn = 0;
        }
    }
    return ok;
}

void XrayBypass::resetScan()
{
    if (!SDK::Minecraft || !SDK::Minecraft->thePlayer)
        return;

    const Vector3 pos = SDK::Minecraft->thePlayer->GetPos();
    const int r = static_cast<int>(m_rangeXZ->value);
    m_scanOrigin = pos;
    m_minX = static_cast<int>(pos.x) - r; m_maxX = static_cast<int>(pos.x) + r;
    m_minZ = static_cast<int>(pos.z) - r; m_maxZ = static_cast<int>(pos.z) + r;
    m_minY = static_cast<int>(pos.y) - static_cast<int>(m_expandDown->value);
    m_maxY = static_cast<int>(pos.y) + static_cast<int>(m_expandUp->value);
    m_cursorX = m_minX; m_cursorY = m_minY; m_cursorZ = m_minZ;

    m_nextMarks.clear();
}

// Block -> unlocalized name cache. Blocks are JVM singletons; local and
// global jobject handles to the same object differ, so identity is compared
// with IsSameObject instead of pointer equality.
bool XrayBypass::resolveName(JNIEnv* env, jobject blockObj, std::string& outName)
{
    if (!env || !blockObj)
        return false;

    for (size_t i = 0; i < m_cacheRefs.size(); ++i)
    {
        if (m_cacheRefs[i] == blockObj || env->IsSameObject(m_cacheRefs[i], blockObj))
        {
            outName = m_cacheNames[i];
            return true;
        }
    }

    if (!m_getUnlocalizedName)
    {
        jclass blockCls = env->GetObjectClass(blockObj);
        if (!blockCls)
        {
            JniResolve::ClearException(env);
            return false;
        }
        m_getUnlocalizedName = JniResolve::Method(env, blockCls,
            "()Ljava/lang/String;", "getUnlocalizedName");
        env->DeleteLocalRef(blockCls);
        if (!m_getUnlocalizedName)
            return false;
    }

    jstring jname = (jstring)env->CallObjectMethod(blockObj, m_getUnlocalizedName);
    if (JniResolve::ClearException(env), !jname)
        return false;
    const char* utf = env->GetStringUTFChars(jname, nullptr);
    outName = utf ? utf : "";
    if (utf)
        env->ReleaseStringUTFChars(jname, utf);
    env->DeleteLocalRef(jname);

    jobject gref = env->NewGlobalRef(blockObj);
    if (gref)
    {
        m_cacheRefs.push_back(gref);
        m_cacheNames.push_back(outName);
    }
    return true;
}

bool XrayBypass::isAir(jobject blockObj)
{
    if (!blockObj)
        return true;
    JNIEnv* env = Java::GetEnv();
    std::string name;
    if (!resolveName(env, blockObj, name))
        return false;
    return name == "tile.air";
}

bool XrayBypass::matchBlock(jobject blockObj, std::string& outName)
{
    if (!blockObj)
        return false;

    std::string name;
    if (!resolveName(Java::GetEnv(), blockObj, name))
        return false;

    if (name == "tile.air")
        return false;

    // diagnostics: first distinct names seen (name-mapping sanity)
    {
        static std::set<std::string> s_seen;
        if (s_seen.size() < 20 && s_seen.insert(name).second)
            Logger::Info("XrayBypass", "block name: " + name);
    }

    outName = name;

    if (m_allBlocks->value)
        return true;

    const std::string lower = lowerCopy(name);
    // whitelist tokens are substrings, e.g. "diamond" matches tile.oreDiamond
    for (const char* tok = m_whitelist->value.c_str(); *tok; )
    {
        const char* comma = std::strchr(tok, ',');
        const size_t len = comma ? static_cast<size_t>(comma - tok) : std::strlen(tok);
        std::string token(tok, len);
        tok = comma ? comma + 1 : tok + len;

        // trim
        while (!token.empty() && (token.front() == ' ')) token.erase(token.begin());
        while (!token.empty() && (token.back() == ' ')) token.pop_back();
        if (token.empty())
            continue;
        if (lower.find(lowerCopy(token)) != std::string::npos)
        {
            static std::set<std::string> s_seenMatch;
            if (s_seenMatch.size() < 20 && s_seenMatch.insert(name).second)
                Logger::Info("XrayBypass", "matched: " + name);
            return true;
        }
    }
    return false;
}

void XrayBypass::scanStep()
{
    JNIEnv* env = Java::GetEnv();
    CWorld* world = SDK::Minecraft ? SDK::Minecraft->theWorld : nullptr;
    jobject worldObj = world ? world->GetInstance() : nullptr;
    if (!env || !worldObj || !m_getBlockState || !m_blockPosCtor)
    {
        static long long s_lastW = 0;
        if (nowMs() - s_lastW > 5000)
        {
            s_lastW = nowMs();
            Logger::Warn("XrayBypass", "scanStep abort: env=" + std::to_string(env != nullptr)
                + " worldObj=" + std::to_string(worldObj != nullptr)
                + " getBlockState=" + std::to_string(m_getBlockState != nullptr)
                + " blockPosCtor=" + std::to_string(m_blockPosCtor != nullptr));
        }
        return;
    }

    {
        static long long s_lastEntry = 0;
        if (nowMs() - s_lastEntry > 5000)
        {
            s_lastEntry = nowMs();
            Logger::Info("XrayBypass", "scanStep entered, budget per step=" + std::to_string(kBlocksPerStep));
        }
    }

    JniResolve::LocalFrame frame(env, 64);
    if (!frame.env)
        return;

    const bool smart = m_smartScan->value;
    g_diag = ScanDiag{};

    while (g_diag.processed < kBlocksPerStep)
    {
        if (m_cursorY > m_maxY)  // sweep finished
        {
            std::lock_guard<std::mutex> lock(m_marksMutex);
            m_marks.swap(m_nextMarks);
            m_nextMarks.clear();
            g_diag.summarize();
            m_passDone = true;
            if (m_verifyOn->value)
            {
                Logger::Info("XrayBypass", "Pass complete — " + std::to_string(m_marks.size())
                    + " blocks marked. Verify mode: proximity re-check active, fakes will drop as you dig.");
            }
            else
            {
                Logger::Info("XrayBypass", "Pass complete — " + std::to_string(m_marks.size())
                    + " blocks marked. Auto-disabling (one-shot scan).");
                setEnabled(false);   // re-enable to scan again from the new position
            }
            return;
        }

        const int x = m_cursorX, y = m_cursorY, z = m_cursorZ;

        // advance cursor
        if (++m_cursorX > m_maxX)
        {
            m_cursorX = m_minX;
            if (++m_cursorZ > m_maxZ)
            {
                m_cursorZ = m_minZ;
                ++m_cursorY;
            }
        }

        jobject bp = env->NewObject(m_blockPosClass, m_blockPosCtor,
            (jint)x, (jint)y, (jint)z);
        if (!bp) { g_diag.bpFail++; g_diag.logException(env, "BlockPos ctor", g_diag.bpExLogged); JniResolve::ClearException(env); continue; }
        ++g_diag.processed;

        jobject state = env->CallObjectMethod(worldObj, m_getBlockState, bp);
        if (env->ExceptionCheck())
        {
            g_diag.stateFail++;
            g_diag.logException(env, "getBlockState", g_diag.stateExLogged);
            JniResolve::ClearException(env);
            env->DeleteLocalRef(bp);
            continue;
        }
        env->DeleteLocalRef(bp);
        if (!state) { g_diag.stateFail++; continue; }

        jobject blockObj = env->CallObjectMethod(state, m_getBlock);
        if (env->ExceptionCheck())
        {
            g_diag.blockFail++;
            g_diag.logException(env, "getBlock", g_diag.blockExLogged);
            JniResolve::ClearException(env);
        }
        env->DeleteLocalRef(state);
        if (!blockObj) continue;

        std::string matchedName;
        if (matchBlock(blockObj, matchedName))
        {
            ++g_diag.matched;
            // SmartScan: only ores with at least one air neighbor — those are
            // genuinely visible and never obfuscated by anti-xray.
            bool exposed = true;
            if (smart)
            {
                exposed = false;
                static const int offs[6][3] = {
                    {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };
                for (const auto& off : offs)
                {
                    jobject nbp = env->NewObject(m_blockPosClass, m_blockPosCtor,
                        (jint)(x + off[0]), (jint)(y + off[1]), (jint)(z + off[2]));
                    if (!nbp) { JniResolve::ClearException(env); continue; }
                    jobject ns = env->CallObjectMethod(worldObj, m_getBlockState, nbp);
                    JniResolve::ClearException(env);
                    env->DeleteLocalRef(nbp);
                    if (!ns) continue;
                    jobject nb = env->CallObjectMethod(ns, m_getBlock);
                    JniResolve::ClearException(env);
                    env->DeleteLocalRef(ns);
                    if (nb && isAir(nb))
                    {
                        env->DeleteLocalRef(nb);
                        exposed = true;
                        break;
                    }
                    if (nb) env->DeleteLocalRef(nb);
                }
            }

            if (exposed)
            {
            unsigned int col = oreColor(lowerCopy(matchedName));
                std::lock_guard<std::mutex> lock(m_marksMutex);
                m_nextMarks.push_back(BlockMark{
                    static_cast<float>(x), static_cast<float>(y),
                    static_cast<float>(z), col, exposed });
                if (m_nextMarks.size() > 8000)
                    m_nextMarks.erase(m_nextMarks.begin());
            }
        }
        env->DeleteLocalRef(blockObj);
        ++g_diag.processed;
    }

    {
        static long long s_lastBudget = 0;
        if (nowMs() - s_lastBudget > 5000)
        {
            s_lastBudget = nowMs();
            Logger::Info("XrayBypass", "step budget exhausted, cursor now Y=" + std::to_string(m_cursorY)
                + " X=" + std::to_string(m_cursorX) + " Z=" + std::to_string(m_cursorZ));
        }
    }
}

void XrayBypass::onTick()
{
    // periodic state heartbeat so a stuck pipeline is visible in the log
    static long long s_lastBeat = 0;
    const long long nowBeat = nowMs();
    if (nowBeat - s_lastBeat >= 5000)
    {
        s_lastBeat = nowBeat;
        std::lock_guard<std::mutex> lock(m_marksMutex);
        const bool inGame = CombatBridge::InGame();
        const bool gui = SDK::Minecraft && SDK::Minecraft->IsInGuiState();
        const bool sane = CommonData::SanityCheck();
        const bool combat = CombatBridge::CanCombat();
        Logger::Info("XrayBypass", "state: inGame=" + std::to_string(inGame ? 1 : 0)
            + " guiOpen=" + std::to_string(gui ? 1 : 0)
            + " sane=" + std::to_string(sane ? 1 : 0)
            + " combat=" + std::to_string(combat ? 1 : 0)
            + " marks=" + std::to_string(m_marks.size())
            + " passDone=" + std::to_string(m_passDone ? 1 : 0)
            + " box=Y[" + std::to_string(m_minY) + ".." + std::to_string(m_maxY) + "]"
            + " X[" + std::to_string(m_minX) + ".." + std::to_string(m_maxX) + "]"
            + " cur=[" + std::to_string(m_cursorX) + "," + std::to_string(m_cursorY) + "," + std::to_string(m_cursorZ) + "]");
    }

    if (!CombatBridge::CanCombat() || !CommonData::SanityCheck() ||
        !SDK::Minecraft || !SDK::Minecraft->thePlayer)
    {
        std::lock_guard<std::mutex> lock(m_marksMutex);
        m_marks.clear();
        m_lastScanMs = 0;
        return;
    }

    if (!classInit())
        return;

    const long long now = nowMs();

    if (m_passDone)
    {
        // one-shot scan finished — only continuous checks run now
        if (m_verifyOn->value && now - m_lastVerifyMs >= 2000)
        {
            m_lastVerifyMs = now;
            verifyStep();
        }
        if (m_packetScan->value)
            probeStep(now);
        return;
    }

    // full rescan cadence + player moved far from scan origin
    // one-time settings dump per world session — catches config garbage
    static long long s_lastDump = -60000;
    if (nowMs() - s_lastDump >= 60000)
    {
        s_lastDump = nowMs();
        Logger::Info("XrayBypass", "settings: RangeXZ=" + std::to_string(m_rangeXZ->value)
            + " UP=" + std::to_string(m_expandUp->value)
            + " DOWN=" + std::to_string(m_expandDown->value)
            + " Delay=" + std::to_string(m_delay->value)
            + " ScanDelay=" + std::to_string(m_scanDelay->value)
            + " allBlocks=" + std::to_string(m_allBlocks->value ? 1 : 0)
            + " smart=" + std::to_string(m_smartScan->value ? 1 : 0)
            + " verify=" + std::to_string(m_verifyOn->value ? 1 : 0)
            + " onlyReal=" + std::to_string(m_onlyReal->value ? 1 : 0)
            + " packetScan=" + std::to_string(m_packetScan->value ? 1 : 0)
            + " probeRange=" + std::to_string((int)m_probeRange->value)
            + " wl=" + m_whitelist->value);
    }

    const Vector3 ppos = SDK::Minecraft->thePlayer->GetPos();
    const bool outsideBox =
        ppos.x < static_cast<float>(m_minX - 8) || ppos.x > static_cast<float>(m_maxX + 8) ||
        ppos.z < static_cast<float>(m_minZ - 8) || ppos.z > static_cast<float>(m_maxZ + 8);
    // One-shot flow: a pass is atomic (cursor runs to completion, no periodic
    // resets) and starts on the first tick after enable or when the player
    // left the previous box. When it finishes, the module auto-disables.
    const bool firstEver = (m_lastScanMs == 0);
    if (outsideBox || firstEver)
    {
        m_lastScanMs = now;
        m_passDone = false;
        resetScan();
    }

    // Delay setting spaces out scan steps across game ticks
    const long long stepGateMs = std::max<long long>(50, static_cast<long long>(m_delay->value * 50.0f));
    if (now - m_lastStepMs < stepGateMs)
        return;
    m_lastStepMs = now;

    scanStep();
}

void XrayBypass::verifyStep()
{
    JNIEnv* env = Java::GetEnv();
    CWorld* world = SDK::Minecraft ? SDK::Minecraft->theWorld : nullptr;
    jobject worldObj = world ? world->GetInstance() : nullptr;
    if (!env || !worldObj || !m_getBlockState || !m_blockPosCtor || !SDK::Minecraft || !SDK::Minecraft->thePlayer)
        return;

    const Vector3 ppos = SDK::Minecraft->thePlayer->GetPos();
    const float R2  = 16.0f * 16.0f;
    const float R62 = 6.0f * 6.0f;

    // collect nearby marks under the lock, then do the JNI reads outside of it
    std::vector<std::array<int, 3>> candidates;
    {
        std::lock_guard<std::mutex> lock(m_marksMutex);
        candidates.reserve(256);
        for (const BlockMark& m : m_marks)
        {
            const float dx = m.x + 0.5f - ppos.x;
            const float dy = m.y + 0.5f - ppos.y;
            const float dz = m.z + 0.5f - ppos.z;
            if (dx * dx + dy * dy + dz * dz <= R2)
                candidates.push_back({ (int)m.x, (int)m.y, (int)m.z });
            if (candidates.size() >= 400)
                break;
        }
    }
    if (candidates.empty())
        return;

    JniResolve::LocalFrame frame(env, 64);
    if (!frame.env)
        return;

    std::vector<std::array<int, 3>> fakes, confirmed;
    int checked = 0;
    for (const auto& c : candidates)
    {
        jobject bp = env->NewObject(m_blockPosClass, m_blockPosCtor, (jint)c[0], (jint)c[1], (jint)c[2]);
        if (!bp) { JniResolve::ClearException(env); continue; }
        jobject state = env->CallObjectMethod(worldObj, m_getBlockState, bp);
        JniResolve::ClearException(env);
        env->DeleteLocalRef(bp);
        if (!state) continue;
        jobject blockObj = env->CallObjectMethod(state, m_getBlock);
        JniResolve::ClearException(env);
        env->DeleteLocalRef(state);
        if (!blockObj) continue;

        // The server rewrites hidden fake blocks to their real value once the
        // player's area gets updated (digging next to them). Re-reading tells
        // fake from real: no longer a whitelisted ore == it was fake.
        std::string name;
        const bool stillOre = matchBlock(blockObj, name);
        env->DeleteLocalRef(blockObj);

        const float dx = (float)c[0] + 0.5f - ppos.x;
        const float dy = (float)c[1] + 0.5f - ppos.y;
        const float dz = (float)c[2] + 0.5f - ppos.z;
        const bool close = (dx * dx + dy * dy + dz * dz) <= R62;

        if (!stillOre)
            fakes.push_back(c);
        else if (close)
            confirmed.push_back(c);
        ++checked;
    }
    if (checked == 0)
        return;

    auto inList = [](const std::vector<std::array<int, 3>>& v, const BlockMark& m)
    {
        for (const auto& c : v)
            if (c[0] == (int)m.x && c[1] == (int)m.y && c[2] == (int)m.z)
                return true;
        return false;
    };

    int removed = 0, verified = 0;
    size_t left = 0;
    {
        std::lock_guard<std::mutex> lock(m_marksMutex);
        for (auto it = m_marks.begin(); it != m_marks.end(); )
        {
            if (!fakes.empty() && inList(fakes, *it))
            {
                it = m_marks.erase(it);
                ++removed;
                continue;
            }
            if (!it->verified && !confirmed.empty() && inList(confirmed, *it))
            {
                it->verified = true;
                ++verified;
            }
            ++it;
        }
        left = m_marks.size();
    }

    if (removed || verified)
        Logger::Info("XrayBypass", "verify: removed " + std::to_string(removed)
            + " fake, confirmed " + std::to_string(verified)
            + " real (" + std::to_string(checked) + " checked, "
            + std::to_string(left) + " marks left)");
}

void XrayBypass::onRender2D()
{
    if (m_marks.empty())
        return;

    const CommonData::RenderState rs = CommonData::GetRenderState();
    const ImGuiIO& io = ImGui::GetIO();
    const int sw = static_cast<int>(io.DisplaySize.x);
    const int sh = static_cast<int>(io.DisplaySize.y);
    if (sw <= 0 || sh <= 0)
        return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const bool fill = m_mode->index == 1;
    const CommonData::PlayerSnapshot* self = nullptr;

    std::vector<BlockMark> marks;
    {
        std::lock_guard<std::mutex> lock(m_marksMutex);
        marks = m_marks;
    }

    // diagnostics: matrix + projection sanity, once per 5s (no JNI here —
    // this runs on the render thread, data comes from CommonData cache)
    static long long s_lastDrawDiag = 0;
    static bool s_dumpedFull = false;
    if (nowMs() - s_lastDrawDiag > 5000)
    {
        s_lastDrawDiag = nowMs();
        if (!s_dumpedFull && !marks.empty())
        {
            s_dumpedFull = true;
            const Matrix& mv = rs.modelView;
            const Matrix& pj = rs.projection;
            Logger::Info("XrayBypass", "MV: " + std::to_string(mv.m00) + " " + std::to_string(mv.m01) + " " + std::to_string(mv.m02) + " " + std::to_string(mv.m03)
                + " | " + std::to_string(mv.m10) + " " + std::to_string(mv.m11) + " " + std::to_string(mv.m12) + " " + std::to_string(mv.m13)
                + " | " + std::to_string(mv.m20) + " " + std::to_string(mv.m21) + " " + std::to_string(mv.m22) + " " + std::to_string(mv.m23)
                + " | " + std::to_string(mv.m30) + " " + std::to_string(mv.m31) + " " + std::to_string(mv.m32) + " " + std::to_string(mv.m33));
            Logger::Info("XrayBypass", "PJ: " + std::to_string(pj.m00) + " " + std::to_string(pj.m01) + " " + std::to_string(pj.m02) + " " + std::to_string(pj.m03)
                + " | " + std::to_string(pj.m10) + " " + std::to_string(pj.m11) + " " + std::to_string(pj.m12) + " " + std::to_string(pj.m13)
                + " | " + std::to_string(pj.m20) + " " + std::to_string(pj.m21) + " " + std::to_string(pj.m22) + " " + std::to_string(pj.m23)
                + " | " + std::to_string(pj.m30) + " " + std::to_string(pj.m31) + " " + std::to_string(pj.m32) + " " + std::to_string(pj.m33));
            Logger::Info("XrayBypass", "mark0=" + std::to_string(marks.front().x) + "," + std::to_string(marks.front().y) + "," + std::to_string(marks.front().z)
                + " camPos=" + std::to_string(rs.renderPos.x) + "," + std::to_string(rs.renderPos.y) + "," + std::to_string(rs.renderPos.z));
        }
        const Vector3 rel{ marks.empty() ? 0.0f : (marks.front().x - rs.camPos.x),
                           marks.empty() ? 0.0f : (marks.front().y - rs.camPos.y),
                           marks.empty() ? 0.0f : (marks.front().z - rs.camPos.z) };
        const Vector4 view = CWorldToScreen::Multiply(Vector4{ rel.x, rel.y, rel.z, 1.0f }, rs.modelView);
        Vector2 probe;
        const bool probed = CWorldToScreen::WorldToScreenVisible(
            rel, rs.modelView, rs.projection, sw, sh, probe);
        Logger::Info("XrayBypass", "render diag: mv.m00=" + std::to_string(rs.modelView.m00)
            + " proj.m00=" + std::to_string(rs.projection.m00)
            + " camPos=" + std::to_string(rs.camPos.x) + "," + std::to_string(rs.camPos.y)
            + "," + std::to_string(rs.camPos.z)
            + " relView=" + std::to_string(view.x) + "," + std::to_string(view.y) + "," + std::to_string(view.z)
            + " center=" + std::to_string(sw / 2) + "," + std::to_string(sh / 2)
            + " marks=" + std::to_string(marks.size())
            + " probeW2S=" + std::to_string(probed ? 1 : 0)
            + (probed ? (" -> " + std::to_string(probe.x) + "," + std::to_string(probe.y)) : std::string()));
    }


    const Vector3 cam = rs.camPos;
    for (const BlockMark& m : marks)
    {
        if (m_onlyReal->value && !m.verified)
            continue;

        // camera-relative corners: modelView is rotation-only
        Vector3 corners[8] = {
            { m.x     - cam.x, m.y     - cam.y, m.z     - cam.z },
            { m.x + 1 - cam.x, m.y     - cam.y, m.z     - cam.z },
            { m.x + 1 - cam.x, m.y     - cam.y, m.z + 1 - cam.z },
            { m.x     - cam.x, m.y     - cam.y, m.z + 1 - cam.z },
            { m.x     - cam.x, m.y + 1 - cam.y, m.z     - cam.z },
            { m.x + 1 - cam.x, m.y + 1 - cam.y, m.z     - cam.z },
            { m.x + 1 - cam.x, m.y + 1 - cam.y, m.z + 1 - cam.z },
            { m.x     - cam.x, m.y + 1 - cam.y, m.z + 1 - cam.z },
        };

        ImVec2 pts[8];
        bool allBehind = true;
        bool ok = true;
        for (int i = 0; i < 8; ++i)
        {
            Vector2 sp;
            if (!CWorldToScreen::WorldToScreenVisible(corners[i], rs.modelView, rs.projection, sw, sh, sp))
            {
                ok = false;
                break;
            }
            pts[i] = ImVec2(sp.x, sp.y);
            allBehind = false;
        }
        if (!ok || allBehind)
            continue;

        static const int edges[12][2] = {
            {0,1},{1,2},{2,3},{3,0}, // bottom
            {4,5},{5,6},{6,7},{7,4}, // top
            {0,4},{1,5},{2,6},{3,7}  // verticals
        };

        if (fill)
        {
            const ImU32 fillColor = (m.color & 0x00FFFFFF) | IM_COL32(0, 0, 0, 90);
            dl->AddQuadFilled(pts[0], pts[1], pts[2], pts[3], fillColor);
            dl->AddQuadFilled(pts[4], pts[5], pts[6], pts[7], fillColor);
            for (int i = 0; i < 4; ++i)
                dl->AddQuadFilled(pts[i], pts[i + 4], pts[(i + 1) % 4 + 4], pts[(i + 1) % 4], fillColor);
        }

        // unverified (possibly fake) marks draw dimmed so real ones stand out
        const ImU32 lineCol = m.verified
            ? m.color
            : ((m.color & 0x00FFFFFFu) | (110u << 24));
        for (const auto& e : edges)
            dl->AddLine(pts[e[0]], pts[e[1]], lineCol, 1.4f);
    }
}


// ---------------------------------------------------------------------------
// Packet dig-probe (experimental): build C07PacketPlayerDigging(Action,
// BlockPos, EnumFacing) and queue it, so anti-xray servers re-evaluate the
// block and (on many configs) re-send its REAL value. A probe that changes
// from ore to stone/air proves the mark was a fake; an unchanged mark is
// left alone. START is always followed by ABORT so nothing is ever broken.
// ---------------------------------------------------------------------------
namespace
{
    // find a method of cls whose name matches one of the variants and which
    // takes `argc` params and returns void; builds the runtime descriptor by
    // reflection so obfuscated names never matter
    jmethodID findMethodByNames(JNIEnv* env, jclass cls,
        const char* const* names, int nameCount, int argc)
    {
        if (!env || !cls)
            return nullptr;
        jclass classClass = env->FindClass("java/lang/Class");
        if (!classClass) { JniResolve::ClearException(env); return nullptr; }
        jmethodID getDeclaredMethods = env->GetMethodID(classClass, "getDeclaredMethods", "()[Ljava/lang/reflect/Method;");
        JniResolve::ClearException(env);
        jclass methodClass = env->FindClass("java/lang/reflect/Method");
        jmethodID getName = methodClass ? env->GetMethodID(methodClass, "getName", "()Ljava/lang/String;") : nullptr;
        jmethodID getParameterTypes = methodClass ? env->GetMethodID(methodClass, "getParameterTypes", "()[Ljava/lang/Class;") : nullptr;
        if (!getDeclaredMethods || !getName || !getParameterTypes)
        {
            JniResolve::ClearException(env);
            return nullptr;
        }

        jmethodID result = nullptr;
        jobjectArray methods = (jobjectArray)env->CallObjectMethod(cls, getDeclaredMethods);
        JniResolve::ClearException(env);
        if (methods)
        {
            const jsize n = env->GetArrayLength(methods);
            for (jsize i = 0; i < n && !result; ++i)
            {
                jobject m = env->GetObjectArrayElement(methods, i);
                if (!m)
                    continue;
                jstring jn = (jstring)env->CallObjectMethod(m, getName);
                JniResolve::ClearException(env);
                const char* nm = jn ? env->GetStringUTFChars(jn, nullptr) : nullptr;
                bool nameHit = false;
                for (int k = 0; nm && k < nameCount; ++k)
                    if (std::strcmp(nm, names[k]) == 0) { nameHit = true; break; }
                if (nm) env->ReleaseStringUTFChars(jn, nm);
                if (jn) env->DeleteLocalRef(jn);

                if (nameHit)
                {
                    jobjectArray params = (jobjectArray)env->CallObjectMethod(m, getParameterTypes);
                    JniResolve::ClearException(env);
                    if (params && env->GetArrayLength(params) == argc)
                    {
                        std::string sig = "(";
                        for (jsize k = 0; k < argc; ++k)
                        {
                            jclass pc = (jclass)env->GetObjectArrayElement(params, k);
                            sig += JniResolve::ClassToDescriptor(env, pc);
                            env->DeleteLocalRef(pc);
                        }
                        sig += ")V";
                        // re-fetch the name for the GetMethodID call
                        jstring jn2 = (jstring)env->CallObjectMethod(m, getName);
                        const char* nm2 = jn2 ? env->GetStringUTFChars(jn2, nullptr) : nullptr;
                        if (nm2)
                        {
                            result = env->GetMethodID(cls, nm2, sig.c_str());
                            JniResolve::ClearException(env);
                            env->ReleaseStringUTFChars(jn2, nm2);
                        }
                        if (jn2) env->DeleteLocalRef(jn2);
                    }
                    if (params) env->DeleteLocalRef(params);
                }
                env->DeleteLocalRef(m);
            }
            env->DeleteLocalRef(methods);
        }
        env->DeleteLocalRef(classClass);
        if (methodClass) env->DeleteLocalRef(methodClass);
        return result;
    }
}

bool XrayBypass::packetInit()
{
    if (m_c07Ctor && m_addToSendQueue && m_sendQueueField && m_actionStart && m_actionAbort && m_facingUp)
        return true;
    if (m_packetInitFailed)
        return false;

    JNIEnv* env = Java::GetEnv();
    if (!env)
        return false;

    JniResolve::LocalFrame frame(env, 96);
    if (!frame.env)
        return false;

    m_packetInitFailed = true;   // set false again on success

    // --- C07 class via the alias table ---
    if (!Java::AssignClass("net/minecraft/network/play/client/C07PacketPlayerDigging", m_c07Class))
        return false;

    // --- constructor (Action, BlockPos, EnumFacing) by reflection ---
    jclass classClass = env->FindClass("java/lang/Class");
    jclass ctorClass = env->FindClass("java/lang/reflect/Constructor");
    if (!classClass || !ctorClass)
    {
        JniResolve::ClearException(env);
        return false;
    }
    jmethodID getDeclaredConstructors = env->GetMethodID(classClass, "getDeclaredConstructors", "()[Ljava/lang/reflect/Constructor;");
    jmethodID getParameterTypes = env->GetMethodID(ctorClass, "getParameterTypes", "()[Ljava/lang/Class;");
    jmethodID isEnumMethod = env->GetMethodID(classClass, "isEnum", "()Z");
    jmethodID getEnumConstants = env->GetMethodID(classClass, "getEnumConstants", "()[Ljava/lang/Object;");
    if (!getDeclaredConstructors || !getParameterTypes || !isEnumMethod || !getEnumConstants)
    {
        JniResolve::ClearException(env);
        return false;
    }

    jclass actionCls = nullptr;
    jclass facingCls = nullptr;
    jobjectArray ctors = (jobjectArray)env->CallObjectMethod(m_c07Class, getDeclaredConstructors);
    JniResolve::ClearException(env);
    if (ctors)
    {
        const jsize n = env->GetArrayLength(ctors);
        for (jsize i = 0; i < n && !m_c07Ctor; ++i)
        {
            jobject ctor = env->GetObjectArrayElement(ctors, i);
            if (!ctor)
                continue;
            jobjectArray params = (jobjectArray)env->CallObjectMethod(ctor, getParameterTypes);
            JniResolve::ClearException(env);
            if (params && env->GetArrayLength(params) == 3)
            {
                jclass p0 = (jclass)env->GetObjectArrayElement(params, 0);
                jclass p1 = (jclass)env->GetObjectArrayElement(params, 1);
                jclass p2 = (jclass)env->GetObjectArrayElement(params, 2);
                const jboolean p0enum = p0 ? env->CallBooleanMethod(p0, isEnumMethod) : JNI_FALSE;
                JniResolve::ClearException(env);
                if (p0enum && p1 && p2)
                {
                    // BlockPos is the param that matches our scan BlockPos class
                    if (env->IsSameObject(p1, m_blockPosClass))
                    {
                        actionCls = (jclass)env->NewGlobalRef(p0);
                        facingCls = (jclass)env->NewGlobalRef(p2);
                    }
                    else if (env->IsSameObject(p2, m_blockPosClass))
                    {
                        actionCls = (jclass)env->NewGlobalRef(p0);
                        facingCls = (jclass)env->NewGlobalRef(p1);
                    }

                    std::string sig = "(";
                    sig += JniResolve::ClassToDescriptor(env, p0);
                    sig += JniResolve::ClassToDescriptor(env, p1);
                    sig += JniResolve::ClassToDescriptor(env, p2);
                    sig += ")V";
                    m_c07Ctor = env->GetMethodID(m_c07Class, "<init>", sig.c_str());
                    JniResolve::ClearException(env);
                }
                if (p0) env->DeleteLocalRef(p0);
                if (p1) env->DeleteLocalRef(p1);
                if (p2) env->DeleteLocalRef(p2);
            }
            if (params) env->DeleteLocalRef(params);
            env->DeleteLocalRef(ctor);
        }
        env->DeleteLocalRef(ctors);
    }
    if (!m_c07Ctor || !actionCls || !facingCls)
        return false;

    // --- enum constants: Action{START=0, ABORT=1}, Facing{..., UP=1} (1.8.9) ---
    {
        jobjectArray acts = (jobjectArray)env->CallObjectMethod(actionCls, getEnumConstants);
        JniResolve::ClearException(env);
        if (!acts || env->GetArrayLength(acts) < 6)
        {
            if (acts) env->DeleteLocalRef(acts);
            return false;
        }
        jobject a0 = env->GetObjectArrayElement(acts, 0);
        jobject a1 = env->GetObjectArrayElement(acts, 1);
        m_actionStart = env->NewGlobalRef(a0);
        m_actionAbort = env->NewGlobalRef(a1);
        env->DeleteLocalRef(a0);
        env->DeleteLocalRef(a1);
        env->DeleteLocalRef(acts);

        jobjectArray facs = (jobjectArray)env->CallObjectMethod(facingCls, getEnumConstants);
        JniResolve::ClearException(env);
        if (!facs || env->GetArrayLength(facs) < 6)
        {
            if (facs) env->DeleteLocalRef(facs);
            return false;
        }
        jobject f1 = env->GetObjectArrayElement(facs, 1);
        m_facingUp = env->NewGlobalRef(f1);
        env->DeleteLocalRef(f1);
        env->DeleteLocalRef(facs);
    }
    env->DeleteLocalRef(actionCls);
    env->DeleteLocalRef(facingCls);

    // --- sendQueue field on EntityPlayerSP + addToSendQueue ---
    jclass playerCls = nullptr;
    if (!Java::AssignClass("net/minecraft/client/entity/EntityPlayerSP", playerCls))
        return false;

    classClass = env->FindClass("java/lang/Class");
    jclass fieldClass = env->FindClass("java/lang/reflect/Field");
    if (!classClass || !fieldClass)
    {
        JniResolve::ClearException(env);
        return false;
    }
    jmethodID getDeclaredFields = env->GetMethodID(classClass, "getDeclaredFields", "()[Ljava/lang/reflect/Field;");
    jmethodID fieldGetName = env->GetMethodID(fieldClass, "getName", "()Ljava/lang/String;");
    jmethodID fieldGetType = env->GetMethodID(fieldClass, "getType", "()Ljava/lang/Class;");
    JniResolve::ClearException(env);

    static const char* kSendNames[] = { "sendQueue", "field_71174_a", "a" };
    static const char* kSendMethods[] = { "addToSendQueue", "func_147297_a", "a" };

    if (getDeclaredFields && fieldGetName && fieldGetType)
    {
        jobjectArray fields = (jobjectArray)env->CallObjectMethod(playerCls, getDeclaredFields);
        JniResolve::ClearException(env);
        if (fields)
        {
            const jsize n = env->GetArrayLength(fields);
            for (jsize i = 0; i < n && !m_sendQueueField; ++i)
            {
                jobject f = env->GetObjectArrayElement(fields, i);
                if (!f)
                    continue;
                jstring jn = (jstring)env->CallObjectMethod(f, fieldGetName);
                const char* nm = jn ? env->GetStringUTFChars(jn, nullptr) : nullptr;
                bool hit = false;
                for (const char* cand : kSendNames)
                    if (nm && std::strcmp(nm, cand) == 0) { hit = true; break; }
                if (nm) env->ReleaseStringUTFChars(jn, nm);
                if (jn) env->DeleteLocalRef(jn);

                if (hit)
                {
                    jclass typeCls = (jclass)env->CallObjectMethod(f, fieldGetType);
                    JniResolve::ClearException(env);
                    if (typeCls)
                    {
                        // the field type must carry the send method — that rules
                        // out the many same-letter fields on EntityPlayerSP
                        jmethodID sender = findMethodByNames(env, typeCls,
                            kSendMethods, 3, 1);
                        if (sender)
                        {
                            std::string fdesc = JniResolve::ClassToDescriptor(env, typeCls);
                            m_sendQueueField = env->GetFieldID(playerCls, nm, fdesc.c_str());
                            JniResolve::ClearException(env);
                            m_addToSendQueue = sender;
                        }
                        env->DeleteLocalRef(typeCls);
                    }
                }
                env->DeleteLocalRef(f);
            }
            env->DeleteLocalRef(fields);
        }
    }

    m_packetInitFailed = !(m_sendQueueField && m_addToSendQueue &&
                           m_c07Ctor && m_actionStart && m_actionAbort && m_facingUp);
    if (!m_packetInitFailed)
        Logger::Info("XrayBypass", "PacketScan ready: C07 dig-probe initialized.");
    else
        Logger::Warn("XrayBypass", "PacketScan init failed (ctor/field/method resolution).");
    return !m_packetInitFailed;
}

bool XrayBypass::sendDig(int x, int y, int z, bool start)
{
    if (!packetInit())
        return false;
    JNIEnv* env = Java::GetEnv();
    if (!env || !SDK::Minecraft || !SDK::Minecraft->thePlayer)
        return false;

    JniResolve::LocalFrame frame(env, 16);
    if (!frame.env)
        return false;

    jobject bp = env->NewObject(m_blockPosClass, m_blockPosCtor, (jint)x, (jint)y, (jint)z);
    if (!bp)
    {
        JniResolve::ClearException(env);
        return false;
    }
    jobject pkt = env->NewObject(m_c07Class, m_c07Ctor,
        start ? m_actionStart : m_actionAbort, bp, m_facingUp);
    JniResolve::ClearException(env);
    env->DeleteLocalRef(bp);
    if (!pkt)
        return false;

    jobject player = SDK::Minecraft->thePlayer->GetInstance();
    jobject nh = player ? env->GetObjectField(player, m_sendQueueField) : nullptr;
    JniResolve::ClearException(env);
    bool ok = false;
    if (nh)
    {
        env->CallVoidMethod(nh, m_addToSendQueue, pkt);
        ok = !env->ExceptionCheck();
        JniResolve::ClearException(env);
        env->DeleteLocalRef(nh);
    }
    env->DeleteLocalRef(pkt);
    return ok;
}

// true if the position reads as a listed ore right now; false = no longer one
bool XrayBypass::readNameAt(int x, int y, int z, bool& stillOre)
{
    stillOre = true;   // conservative default: keep the mark on any failure
    JNIEnv* env = Java::GetEnv();
    CWorld* world = SDK::Minecraft ? SDK::Minecraft->theWorld : nullptr;
    jobject worldObj = world ? world->GetInstance() : nullptr;
    if (!env || !worldObj || !m_getBlockState || !m_blockPosCtor)
        return false;

    JniResolve::LocalFrame frame(env, 16);
    if (!frame.env)
        return false;

    jobject bp = env->NewObject(m_blockPosClass, m_blockPosCtor, (jint)x, (jint)y, (jint)z);
    if (!bp) { JniResolve::ClearException(env); return false; }
    jobject state = env->CallObjectMethod(worldObj, m_getBlockState, bp);
    JniResolve::ClearException(env);
    env->DeleteLocalRef(bp);
    if (!state)
        return false;
    jobject blockObj = env->CallObjectMethod(state, m_getBlock);
    JniResolve::ClearException(env);
    env->DeleteLocalRef(state);
    if (!blockObj)
        return false;

    std::string name;
    stillOre = matchBlock(blockObj, name);
    env->DeleteLocalRef(blockObj);
    return true;
}

void XrayBypass::probeStep(long long now)
{
    if (!CombatBridge::CanCombat() || !SDK::Minecraft || !SDK::Minecraft->thePlayer)
        return;
    if (!packetInit())
        return;
    if (m_probed.size() >= 4000)
    {
        if (!m_probeCapLogged)
        {
            m_probeCapLogged = true;
            Logger::Info("XrayBypass", "probe: session cap reached (4000). Re-enable the module to reset.");
        }
        return;
    }

    // a probe is in flight: after the wait, read the position back and abort
    if (m_probeAwait)
    {
        if (now - m_probeSentMs < 150)
            return;
        m_probeAwait = false;

        bool stillOre = true;
        if (readNameAt(m_probeX, m_probeY, m_probeZ, stillOre) && !stillOre)
        {
            int removed = 0;
            {
                std::lock_guard<std::mutex> lock(m_marksMutex);
                for (auto it = m_marks.begin(); it != m_marks.end(); )
                {
                    if ((int)it->x == m_probeX && (int)it->y == m_probeY && (int)it->z == m_probeZ)
                    {
                        it = m_marks.erase(it);
                        ++removed;
                    }
                    else
                        ++it;
                }
            }
            if (removed)
            {
                m_probeRemovedCount += removed;
                Logger::Info("XrayBypass", "probe: fake confirmed at "
                    + std::to_string(m_probeX) + "," + std::to_string(m_probeY) + "," + std::to_string(m_probeZ)
                    + " (removed " + std::to_string(removed) + ")");
            }
        }

        sendDig(m_probeX, m_probeY, m_probeZ, false);   // ABORT — never break anything
        return;
    }

    if (now - m_probeNextMs < (long long)std::max(100.0f, m_probeDelay->value))
        return;

    // pick the nearest unprobed mark within ProbeRange (not only close marks)
    const Vector3 ppos = SDK::Minecraft->thePlayer->GetPos();
    int bx = 0, by = 0, bz = 0;
    const float probeR = std::max(8.0f, m_probeRange->value);
    float best = probeR * probeR;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(m_marksMutex);
        for (const BlockMark& m : m_marks)
        {
            const float dx = m.x + 0.5f - ppos.x;
            const float dy = m.y + 0.5f - ppos.y;
            const float dz = m.z + 0.5f - ppos.z;
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 > best)
                continue;
            const std::array<int, 3> key{ (int)m.x, (int)m.y, (int)m.z };
            if (m_probed.count(key))
                continue;
            best = d2;
            bx = (int)m.x; by = (int)m.y; bz = (int)m.z;
            found = true;
        }
    }
    if (!found)
    {
        if (!m_probeIdleLogged)
        {
            m_probeIdleLogged = true;
            Logger::Info("XrayBypass", "probe: idle — every mark within " + std::to_string((int)probeR)
                + " blocks has been probed (sent=" + std::to_string(m_probeSentCount)
                + " removed=" + std::to_string(m_probeRemovedCount) + ")");
        }
        return;
    }

    m_probed.insert({ bx, by, bz });
    if (sendDig(bx, by, bz, true))
    {
        m_probeX = bx; m_probeY = by; m_probeZ = bz;
        m_probeSentMs = now;
        m_probeAwait = true;
        m_probeNextMs = now;
        m_probeIdleLogged = false;
        ++m_probeSentCount;
        if (m_probeSentCount % 20 == 0)
            Logger::Info("XrayBypass", "probe progress: sent=" + std::to_string(m_probeSentCount)
                + " removed=" + std::to_string(m_probeRemovedCount));
    }
}
