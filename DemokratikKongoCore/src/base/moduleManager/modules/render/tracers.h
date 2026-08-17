#pragma once

#include "../../module.h"

struct BoolSetting;
struct ColorSetting;

class Tracers : public Module
{
public:
    Tracers();

    void onEnable()  override;
    void onDisable() override;
    void onTick()    override;

private:
    void pushAll();

    BoolSetting*  m_invisibles         = nullptr;
    BoolSetting*  m_colorByDistance    = nullptr;
    BoolSetting*  m_highlightFocusing  = nullptr;
    BoolSetting*  m_hideBots           = nullptr;
    BoolSetting*  m_ignoreFriends      = nullptr;
    ColorSetting* m_color              = nullptr;
};
