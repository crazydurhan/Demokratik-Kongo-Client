#pragma once

#include "../../module.h"
#include "../../../java/java.h"

class InvMove : public Module
{
public:
    InvMove();

    void onEnable() override;
    void onDisable() override;
    void onTick() override;

private:
    void pushAll();
    bool isChestScreen(JNIEnv* env, jobject screen) const;

    EnumSetting* m_inventoryMode = nullptr;
    EnumSetting* m_chestMode = nullptr;
};
