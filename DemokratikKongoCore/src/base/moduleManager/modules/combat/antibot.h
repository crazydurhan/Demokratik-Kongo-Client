#pragma once

#include "../../module.h"
#include "../../../java/java.h"
#include <unordered_set>
#include <string>

class AntiBot : public Module
{
public:
    AntiBot();

    std::string arrayListSuffix(SuffixDetail detail) const override;
    void onTick() override;

    static bool IsBot(const std::string& name);
    static bool IsBotEntity(jobject entityObj);

private:
    BoolSetting* m_checkHeight = nullptr;
    BoolSetting* m_checkSleeping = nullptr;
    BoolSetting* m_checkHealth = nullptr;
    BoolSetting* m_checkInvisible = nullptr;

    static inline std::unordered_set<std::string> s_botList;
    static inline std::unordered_set<std::string> s_heightBots;
    static inline std::unordered_set<std::string> s_sleepingBots;
    static inline std::unordered_set<std::string> s_healthBots;
    static inline std::unordered_set<std::string> s_invisibleBots;
};
