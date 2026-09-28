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
#include <mutex>

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
            Logger::Info("XrayBypass", "Pass complete — " + std::to_string(m_marks.size())
                + " blocks marked. Auto-disabling (one-shot scan).");
            setEnabled(false);   // re-enable to scan again from the new position
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
                    static_cast<float>(z), col });
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

    for (const BlockMark& m : marks)
    {
        Vector3 corners[8] = {
            { m.x,     m.y,     m.z     },
            { m.x + 1, m.y,     m.z     },
            { m.x + 1, m.y,     m.z + 1 },
            { m.x,     m.y,     m.z + 1 },
            { m.x,     m.y + 1, m.z     },
            { m.x + 1, m.y + 1, m.z     },
            { m.x + 1, m.y + 1, m.z + 1 },
            { m.x,     m.y + 1, m.z + 1 },
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

        for (const auto& e : edges)
            dl->AddLine(pts[e[0]], pts[e[1]], m.color, 1.4f);
    }
}
