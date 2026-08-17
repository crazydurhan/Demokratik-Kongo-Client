#pragma once

#include "../../module.h"

struct BoolSetting;
struct ColorSetting;
struct EnumSetting;

/*
    3D ESP — world-space player boxes rendered in Java (EspBridge.renderWorld).
    C++ only syncs settings via Patcher::put; no 2D screen overlay.
*/
class Esp3D : public Module
{
public:
    Esp3D();

    void onEnable()  override;
    void onDisable() override;
    void onTick()    override;

private:
    void pushAll();

    ColorSetting* m_playerColor = nullptr;
    EnumSetting*  m_mode        = nullptr;
    BoolSetting*  m_invisibles  = nullptr;
    BoolSetting*  m_hideBots    = nullptr;
};
