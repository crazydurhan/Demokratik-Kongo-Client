#pragma once

#include "../../module.h"

#include <string>
#include <vector>

class ItemLogger : public Module
{
public:
    ItemLogger();

    void onDisable() override;
    void onTick()    override;

    std::string arrayListSuffix(SuffixDetail detail) const override;

    // Called from JNI (RuntimeBridge.pushItemLog) on the client thread.
    static void PushLog(const std::string& message);
    static std::vector<std::string> SnapshotLogs();
    static void ClearLogs();

private:
    NumberSetting* m_minSharpness = nullptr;
    BoolSetting*   m_useWhitelist = nullptr;
    StringSetting* m_whitelist    = nullptr;
    BoolSetting*   m_chatLog      = nullptr;
    ActionSetting* m_openManager  = nullptr;
};
