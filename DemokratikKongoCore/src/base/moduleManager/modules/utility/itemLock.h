#pragma once

#include "../../module.h"

class ItemLock : public Module
{
public:
    ItemLock();

    void onTick() override;
    void onDisable() override;

    std::string arrayListSuffix(SuffixDetail detail) const override;

private:
    BoolSetting*   m_useWhitelist = nullptr;
    StringSetting* m_whitelist = nullptr;
    StringSetting* m_slotWhitelist = nullptr;
    int m_lastLoggedMatch = -1;
};
