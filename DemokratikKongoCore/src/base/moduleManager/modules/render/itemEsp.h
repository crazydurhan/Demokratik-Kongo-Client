#pragma once

#include "../../module.h"

struct BoolSetting;
struct NumberSetting;
struct StringSetting;

class ItemESP : public Module
{
public:
    ItemESP();

    void onEnable()  override;
    void onDisable() override;
    void onTick()    override;

private:
    void pushAll();

    BoolSetting*    m_distance       = nullptr;
    BoolSetting*    m_groupItems     = nullptr;
    NumberSetting*  m_scale          = nullptr;
    BoolSetting*    m_autoScale      = nullptr;
    BoolSetting*    m_whitelistOnly  = nullptr;
    StringSetting*  m_allowedItems   = nullptr;
};
