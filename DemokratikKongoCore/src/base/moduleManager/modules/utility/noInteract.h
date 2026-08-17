#pragma once

#include "../../module.h"

class NoInteract : public Module
{
public:
    NoInteract();

    void onTick() override;
    void onDisable() override;

    std::string arrayListSuffix(SuffixDetail detail) const override;

private:
    BoolSetting*   m_useWhitelist = nullptr;
    StringSetting* m_whitelist = nullptr;
    int m_lastLoggedMatch = -1;
};
