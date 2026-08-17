#pragma once

#include "../../module.h"
#include "../../../../../ext/jni/jni.h"
#include <chrono>

class ChestStealer : public Module
{
public:
    ChestStealer();

    void onEnable() override;
    void onDisable() override;
    void onTick() override { /* worker: no-op — runs on client thread */ }
    void clientTick();

private:
    NumberSetting* m_minDelay;
    NumberSetting* m_maxDelay;
    BoolSetting* m_autoClose;
    BoolSetting* m_trashFilter;

    std::chrono::steady_clock::time_point m_lastStealTime;
    int m_nextDelayMs = 150;

    bool isItemValuable(JNIEnv* env, jobject itemStack);
};
