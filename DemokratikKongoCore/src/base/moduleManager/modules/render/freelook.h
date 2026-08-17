#pragma once

#include "../../module.h"

class Freelook : public Module
{
public:
    Freelook();

    void onEnable() override;
    void onDisable() override;
    void onTick() override;
    void pushAll();

private:
    void enterPerspective();
    void resetPerspective();
    bool isKeyDown() const;

    KeybindSetting* m_key = nullptr;
    BoolSetting*    m_hold = nullptr;
    BoolSetting*    m_invertPitch = nullptr;
    BoolSetting*    m_lockPitch = nullptr;
    BoolSetting*    m_customFov = nullptr;
    NumberSetting*  m_fov = nullptr;

    bool  m_active = false;
    bool  m_prevKey = false;
    int   m_savedPerspective = 0;
    float m_savedFov = 70.0f;
};
