#include "nametags.h"

#include "../../../patcher/patcher.h"
#include "../../../util/format.h"
#include "../../../util/logger.h"
#include "../combat/friends.h"

#include <sstream>

NameTags::NameTags()
    : Module("NameTags",
             "Lunar-style nametags via Minecraft FontRenderer and RenderItem (real item icons).",
             Category::Render)
{
    m_ignoreInvisibles   = &add<BoolSetting>  ("Ignore Invisibles",   true);
    m_scale              = &add<NumberSetting>("Scale",               1.0f, 0.25f, 2.0f, 0.05f);
    m_autoScale          = &add<BoolSetting>  ("Auto Scale",          true);
    m_minScale           = &add<NumberSetting>("Min Scale",           0.45f, 0.10f, 1.0f, 0.05f);
    m_hideBots           = &add<BoolSetting>  ("Hide Bots",           true);
    m_showOwnNametag     = &add<BoolSetting>  ("Show Own Nametag",    true);
    m_background         = &add<BoolSetting>  ("Background",          true);
    m_bgOpacity          = &add<NumberSetting>("Background Opacity", 65.0f, 0.0f, 100.0f, 1.0f);
    m_textShadow         = &add<BoolSetting>  ("Text Shadow",         true);
    m_renderPlayers      = &add<BoolSetting>  ("Render Players",      true);
    m_playersHealth      = &add<BoolSetting>  ("Show Health",         false);
    m_playersDistance    = &add<BoolSetting>  ("Show Distance",       false);
    m_playersRegen       = &add<BoolSetting>  ("Show Regen Timer",    true);
    m_playersEquipment   = &add<BoolSetting>  ("Show Equipment",      true);
    m_playersDurability  = &add<BoolSetting>  ("Show Durability",     true);
    m_playersEnchants    = &add<BoolSetting>  ("Show Enchants",       true);
    m_iconSpacing        = &add<NumberSetting>("Icon Spacing",        2.0f, 0.0f, 8.0f, 0.5f);

    m_bgOpacity->suffix   = "%";
    m_bgOpacity->visible  = [this] { return m_background->value; };
    m_playersDurability->visible = [this] { return m_playersEquipment->value; };
    m_playersEnchants->visible   = [this] { return m_playersEquipment->value; };

    m_bgOpacity->description       = "Dark background translucency behind the name text only.";
    m_showOwnNametag->description   = "Show your nametag in third-person (F5).";
    m_playersDurability->description = "Durability bars (Java path uses vanilla item rendering).";
    m_playersRegen->description     = "Shows remaining Regeneration seconds next to the name (god apple tracking).";
    m_autoScale->description        = "Keeps nametags readable far away (gentle distance falloff + minimum size).";
    m_minScale->description         = "Smallest size relative to Scale when far away.";
    m_minScale->visible             = [this] { return m_autoScale->value; };

    setEnabled(false);
}

void NameTags::onEnable()
{
    Logger::Info("NameTags", "Enabled — Java renderer path (Lion-style nt_renderWorld + RenderItem icons)");
    pushAll();
    logSettings("onEnable");
}

void NameTags::onDisable()
{
    Patcher::put("nametags_enabled",  "false");
    Patcher::put("nonametag_enabled", "false");
    Logger::Info("NameTags", "Disabled — vanilla nametags restored");
}

void NameTags::onTick()
{
    if (!isEnabled()) return;
    static int s_tickCounter = 0;
    pushAll();
    if (++s_tickCounter >= 200)
    {
        s_tickCounter = 0;
        logSettings("sync");
    }
}

void NameTags::logSettings(const char* reason)
{
    auto b = [](bool v) { return v ? "true" : "false"; };
    std::ostringstream oss;
    oss << reason << " | scale=" << m_scale->value
        << " equipment=" << b(m_playersEquipment->value)
        << " enchants=" << b(m_playersEnchants->value)
        << " health=" << b(m_playersHealth->value)
        << " bg=" << b(m_background->value)
        << " bgOpacity=" << m_bgOpacity->value << "%"
        << " hideBots=" << b(m_hideBots->value);
    Logger::Debug("NameTags", oss.str());
}

void NameTags::pushAll()
{
    // Java renderer (ClassPatcher nt_intercept): MC FontRenderer + RenderItem equipment row.
    Patcher::put("nametags_enabled",              "true");
    Patcher::put("nonametag_enabled",             "false");
    Patcher::put("nametags_ignore_invisibles",    bstr(m_ignoreInvisibles->value));
    Patcher::put("nametags_scale",                fstr(m_scale->value));
    Patcher::put("nametags_autoscale",            bstr(m_autoScale->value));
    Patcher::put("nametags_min_scale",            fstr(m_minScale->value));
    Patcher::put("nametags_hide_bots",            bstr(m_hideBots->value));
    Patcher::put("nametags_show_own",             bstr(m_showOwnNametag->value));
    Patcher::put("nametags_background",           bstr(m_background->value));
    Patcher::put("nametags_bg_opacity",           fstr(m_bgOpacity->value));
    Patcher::put("nametags_text_shadow",          bstr(m_textShadow->value));
    Patcher::put("nametags_render_players",       bstr(m_renderPlayers->value));
    Patcher::put("nametags_players_health",       bstr(m_playersHealth->value));
    Patcher::put("nametags_players_distance",     bstr(m_playersDistance->value));
    Patcher::put("nametags_players_regen",        bstr(m_playersRegen->value));
    Patcher::put("nametags_players_equipment",    bstr(m_playersEquipment->value));
    Patcher::put("nametags_enchantments",         bstr(m_playersEnchants->value));
    Patcher::put("nametags_players_durability",  bstr(m_playersDurability->value));
    Patcher::put("nametags_icon_spacing",         fstr(m_iconSpacing->value));

    std::ostringstream fcsv;
    bool first = true;
    for (const auto& name : Friends::List) {
        if (!first) fcsv << ",";
        fcsv << name;
        first = false;
    }
    Patcher::put("nametags_friends", fcsv.str());
}

void NameTags::onRender2D()
{
    // Rendering is handled in Java (RendererLivingEntity hook). C++ only syncs settings.
}
