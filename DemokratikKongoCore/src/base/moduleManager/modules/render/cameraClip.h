#pragma once

#include "../../module.h"

class CameraClip : public Module
{
public:
    CameraClip();

    void onEnable() override;
    void onDisable() override;
    void onTick() override;
    void pushAll();

    std::string arrayListSuffix(SuffixDetail detail) const override;

private:
    NumberSetting* m_distance = nullptr;
};
