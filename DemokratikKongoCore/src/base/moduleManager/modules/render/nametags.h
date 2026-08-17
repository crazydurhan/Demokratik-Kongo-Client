#pragma once

#include "../../module.h"

/*
    NameTags - Lunar Client-style player nametags rendered in Java.

    Settings are synced to ClassPatcher via ThreadContext (Patcher::put).
    RendererLivingEntity#renderLivingLabel is hooked to draw 3D billboards
    with Minecraft FontRenderer and RenderItem for real equipment icons.
    Combat modules use separate patches (EntityRenderer / EntityPlayerSP).
*/
class NameTags : public Module
{
public:
    NameTags();

    void onEnable()  override;
    void onDisable() override;
    void onTick()    override;
    void onRender2D() override;

private:
    void pushAll();
    void logSettings(const char* reason);

    BoolSetting*   m_ignoreInvisibles   = nullptr;
    NumberSetting* m_scale              = nullptr;
    BoolSetting*   m_autoScale          = nullptr;
    NumberSetting* m_minScale           = nullptr;
    BoolSetting*   m_hideBots            = nullptr;
    BoolSetting*   m_showOwnNametag      = nullptr;
    BoolSetting*   m_background         = nullptr;
    NumberSetting* m_bgOpacity          = nullptr;
    BoolSetting*   m_textShadow         = nullptr;
    BoolSetting*   m_renderPlayers      = nullptr;
    BoolSetting*   m_playersHealth      = nullptr;
    BoolSetting*   m_playersDistance    = nullptr;
    BoolSetting*   m_playersRegen       = nullptr;
    BoolSetting*   m_playersEquipment   = nullptr;
    BoolSetting*   m_playersDurability  = nullptr;
    BoolSetting*   m_playersEnchants    = nullptr;
    NumberSetting* m_iconSpacing        = nullptr;
};
