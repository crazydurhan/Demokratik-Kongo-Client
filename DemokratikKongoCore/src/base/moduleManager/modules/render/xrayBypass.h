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

    std::string arrayListSuffix(SuffixDetail detail) const override;

private:
    struct BlockMark
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        unsigned int color = 0;
    };

    void resetScan();
    void scanStep();
    bool classInit();

    bool matchBlock(jobject blockObj, std::string& outName);
    bool isAir(jobject blockObj);

    NumberSetting* m_rangeXZ = nullptr;
    NumberSetting* m_expandUp = nullptr;
    NumberSetting* m_expandDown = nullptr;
    NumberSetting* m_delay = nullptr;        // ticks per scan step
    BoolSetting*   m_smartScan = nullptr;    // exposed ores only
    NumberSetting* m_scanDelay = nullptr;    // ms between full rescans
    BoolSetting*   m_allBlocks = nullptr;
    EnumSetting*   m_mode = nullptr;         // Outline / Fill
    StringSetting* m_whitelist = nullptr;    // comma list, substring match
    ColorSetting*  m_color = nullptr;

    // scan state
    int m_cursorX = 0, m_cursorY = 0, m_cursorZ = 0;
    int m_minX = 0, m_maxX = 0, m_minY = 0, m_maxY = 0, m_minZ = 0, m_maxZ = 0;
    long long m_lastScanMs = 0;
    long long m_lastStepMs = 0;
    Vector3 m_scanOrigin{};

    std::vector<BlockMark> m_marks;          // render thread reads
    std::vector<BlockMark> m_nextMarks;      // accumulating sweep
    std::mutex m_marksMutex;

    // Block objects are JVM singletons — cache name per instance.
    std::unordered_map<jobject, std::string> m_nameCache;   // global-ref keys
    std::vector<jobject> m_cacheRefs;                        // for cleanup

    // resolved JNI
    jclass m_blockPosClass = nullptr;
    jmethodID m_blockPosCtor = nullptr;
    jmethodID m_getBlockState = nullptr;
    jmethodID m_getBlock = nullptr;
    jmethodID m_getUnlocalizedName = nullptr;
};
