#pragma once

#include "../../module.h"
#include "../../../sdk/sdk.h"

#include <string>
#include <unordered_map>
#include <vector>

// XrayBypass — highlights ores from the block data the server legitimately
// sends (no extra packets). Anti-xray plugins only fake blocks the player
// cannot see, so SmartScan marks only ores with an exposed (air) neighbor,
// which are never obfuscated.
class XrayBypass : public Module
{
public:
    XrayBypass();

    void onEnable() override;
    void onDisable() override;
    void onTick() override;
    void onRender2D() override;

    // marks stay on screen after the one-shot scan auto-disables the module
    bool renderWhenDisabled() const override { return m_passDone; }

    std::string arrayListSuffix(SuffixDetail detail) const override;

private:
    struct BlockMark
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        unsigned int color = 0;
        bool verified = false;   // exposed at scan, or confirmed by proximity re-check
    };

    void resetScan();
    void scanStep();
    void verifyStep();

    // packet dig-probe: sends C07 start/abort pairs at candidate positions so
    // the server re-sends the real block (fake vs real becomes observable)
    bool packetInit();
    void probeStep(long long now);
    bool readNameAt(int x, int y, int z, bool& stillOre);
    bool sendDig(int x, int y, int z, bool start);
    bool classInit();

    bool resolveName(JNIEnv* env, jobject blockObj, std::string& outName);
    bool matchBlock(jobject blockObj, std::string& outName);
    bool isAir(jobject blockObj);

    NumberSetting* m_rangeXZ = nullptr;
    NumberSetting* m_expandUp = nullptr;
    NumberSetting* m_expandDown = nullptr;
    NumberSetting* m_delay = nullptr;        // ticks per scan step
    BoolSetting*   m_smartScan = nullptr;    // exposed ores only
    NumberSetting* m_scanDelay = nullptr;    // ms between full rescans
    BoolSetting*   m_allBlocks = nullptr;
    BoolSetting*   m_verifyOn = nullptr;     // proximity re-check removes fake blocks
    BoolSetting*   m_onlyReal = nullptr;     // render only confirmed-real marks
    BoolSetting*   m_packetScan = nullptr;   // dig-probe packets (server re-sends real blocks)
    NumberSetting* m_probeDelay = nullptr;   // ms between probes
    EnumSetting*   m_mode = nullptr;         // Outline / Fill
    StringSetting* m_whitelist = nullptr;    // comma list, substring match
    ColorSetting*  m_color = nullptr;

    // scan state
    int m_cursorX = 0, m_cursorY = 0, m_cursorZ = 0;
    int m_minX = 0, m_maxX = 0, m_minY = 0, m_maxY = 0, m_minZ = 0, m_maxZ = 0;
    long long m_lastScanMs = 0;
    long long m_lastStepMs = 0;
    long long m_lastVerifyMs = 0;
    bool m_passDone = false;

    // packet dig-probe state
    jclass    m_c07Class = nullptr;
    jmethodID m_c07Ctor = nullptr;
    jfieldID  m_sendQueueField = nullptr;
    jmethodID m_addToSendQueue = nullptr;
    jobject   m_actionStart = nullptr;   // global refs
    jobject   m_actionAbort = nullptr;
    jobject   m_facingUp = nullptr;
    bool      m_packetInitFailed = false;
    bool      m_probeAwait = false;
    long long m_probeSentMs = 0;
    long long m_probeNextMs = 0;
    int       m_probeX = 0, m_probeY = 0, m_probeZ = 0;
    std::set<std::array<int, 3>> m_probed;
    Vector3 m_scanOrigin{};

    std::vector<BlockMark> m_marks;          // render thread reads
    std::vector<BlockMark> m_nextMarks;      // accumulating sweep
    std::mutex m_marksMutex;

    // Block objects are JVM singletons — cache name per instance.
    // Local and global refs to one object are different jobject handles, so
    // identity is checked with IsSameObject, never pointer equality.
    std::vector<jobject> m_cacheRefs;
    std::vector<std::string> m_cacheNames;

    // resolved JNI
    jclass m_blockPosClass = nullptr;
    jmethodID m_blockPosCtor = nullptr;
    jmethodID m_getBlockState = nullptr;
    jmethodID m_getBlock = nullptr;
    jmethodID m_getUnlocalizedName = nullptr;
};
