#pragma once

#include "../../module.h"
#include "../../../sdk/sdk.h"

#include <atomic>
#include <memory>
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
    bool renderWhenDisabled() const override { return m_passDone.load(); }

    std::string arrayListSuffix(SuffixDetail detail) const override;

private:
    struct BlockMark
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        unsigned int color = 0;
        // Proven real, not merely whitelisted: set at scan time only by the
        // SmartScan air-neighbour test, later by verifyStep()'s proximity
        // re-check or by a dig-probe read-back. A mark found with SmartScan off
        // starts unverified.
        bool verified = false;
    };

    void resetScan();
    void expandScanBox(const Vector3& pos);
    void scanStep();
    void verifyStep();

    // Republish the render-thread view of m_marks. Called after every change
    // to m_marks; the render thread only ever touches the snapshot, so it
    // never copies or locks while drawing.
    void publishSnapshot();
    void flushProbe();

    // packet dig-probe: sends C07 start/abort pairs at candidate positions so
    // the server re-sends the real block (fake vs real becomes observable)
    bool packetInit();
    void probeStep(long long now);
    bool readNameAt(int x, int y, int z, bool& stillOre);
    bool sendDig(int x, int y, int z, bool start);
    bool classInit();

    bool resolveName(JNIEnv* env, jobject blockObj, std::string& outName);
    bool matchBlock(jobject blockObj, std::string& outName);
    void rebuildTokens();
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
    NumberSetting* m_probeRange = nullptr;   // probe reach, blocks (not only close marks)
    EnumSetting*   m_mode = nullptr;         // Outline / Fill
    StringSetting* m_whitelist = nullptr;    // comma list, substring match
    ColorSetting*  m_color = nullptr;
    BoolSetting*   m_debug = nullptr;        // verbose scan/render diagnostics

    // scan state
    int m_cursorX = 0, m_cursorY = 0, m_cursorZ = 0;
    int m_minX = 0, m_maxX = 0, m_minY = 0, m_maxY = 0, m_minZ = 0, m_maxZ = 0;
    long long m_lastScanMs = 0;
    long long m_lastStepMs = 0;
    long long m_lastVerifyMs = 0;
    std::atomic<bool> m_passDone{ false };

    // packet dig-probe state
    jclass    m_c07Class = nullptr;
    jmethodID m_c07Ctor = nullptr;
    jfieldID  m_sendQueueField = nullptr;
    jmethodID m_addToSendQueue = nullptr;
    jobject   m_actionStart = nullptr;   // global refs
    jobject   m_actionAbort = nullptr;
    jobject   m_facingUp = nullptr;
    bool      m_packetInitFailed = false;
    bool      m_packetInitWarned = false;
    long long m_nextPacketInitMs = 0;
    bool      m_probeAwait = false;
    long long m_probeSentMs = 0;
    long long m_probeNextMs = 0;
    int       m_probeX = 0, m_probeY = 0, m_probeZ = 0;
    std::set<std::array<int, 3>> m_probed;
    int       m_probeSentCount = 0;
    int       m_probeRemovedCount = 0;
    bool      m_probeIdleLogged = false;
    bool      m_probeCapLogged = false;

    std::vector<BlockMark> m_marks;          // tick-thread owned
    std::vector<BlockMark> m_nextMarks;      // accumulating sweep (tick thread only)
    std::shared_ptr<const std::vector<BlockMark>> m_snapshot;   // render thread
    mutable std::mutex m_marksMutex;         // guards m_marks + m_snapshot

    // Block objects are JVM singletons — cache name per instance.
    // Local and global refs to one object are different jobject handles, so
    // identity is checked with IsSameObject, never pointer equality.
    // m_cacheFront is the most recently used entry: the sweep is spatially
    // coherent, so consecutive reads almost always hit it.
    std::vector<jobject> m_cacheRefs;
    std::vector<std::string> m_cacheNames;
    size_t m_cacheFront = static_cast<size_t>(-1);

    // Whitelist tokens, lowercased once per distinct whitelist value.
    std::string m_tokenSource;
    std::vector<std::string> m_tokens;

    // resolved JNI
    jclass m_blockPosClass = nullptr;
    jmethodID m_blockPosCtor = nullptr;
    jmethodID m_getBlockState = nullptr;
    jmethodID m_getBlock = nullptr;
    jmethodID m_getUnlocalizedName = nullptr;
};
