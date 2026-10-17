#include "arif/gui/Model.hpp"

#include <algorithm>
#include <cctype>

namespace arif::gui {

namespace {

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return value;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return toLower(haystack).find(needle) != std::string::npos;
}

} // namespace

// ---------------------------------------------------------------------------
// Kategori metinleri
// ---------------------------------------------------------------------------
const char* categoryName(Category category) {
    switch (category) {
        case Category::Favorites: return "Favorites";
        case Category::Combat:    return "Combat";
        case Category::Movement:  return "Movement";
        case Category::Render:    return "Render";
        case Category::Utility:   return "Utility";
        case Category::Misc:      return "Misc";
        default:                  return "?";
    }
}

// ---------------------------------------------------------------------------
// GUIModule - ayar ekleme yardimcilari
// ---------------------------------------------------------------------------
static GUISetting& pushSetting(GUIModule& module, std::unique_ptr<GUISetting> setting) {
    module.settings.push_back(std::move(setting));
    return *module.settings.back();
}

GUISetting& GUIModule::addBool(std::string settingId, std::string label, bool value, std::string settingDescription) {
    auto setting = std::make_unique<GUISetting>();
    setting->id = std::move(settingId);
    setting->label = std::move(label);
    setting->description = std::move(settingDescription);
    setting->kind = SettingKind::Bool;
    setting->boolValue = value;
    return pushSetting(*this, std::move(setting));
}

GUISetting& GUIModule::addInt(std::string settingId, std::string label, int value, int min, int max,
                              std::string settingDescription) {
    auto setting = std::make_unique<GUISetting>();
    setting->id = std::move(settingId);
    setting->label = std::move(label);
    setting->description = std::move(settingDescription);
    setting->kind = SettingKind::Int;
    setting->intValue = value;
    setting->intMin = min;
    setting->intMax = max;
    return pushSetting(*this, std::move(setting));
}

GUISetting& GUIModule::addFloat(std::string settingId, std::string label, float value, float min, float max,
                                std::string settingDescription, const char* format) {
    auto setting = std::make_unique<GUISetting>();
    setting->id = std::move(settingId);
    setting->label = std::move(label);
    setting->description = std::move(settingDescription);
    setting->kind = SettingKind::Float;
    setting->floatValue = value;
    setting->floatMin = min;
    setting->floatMax = max;
    setting->floatFormat = format;
    return pushSetting(*this, std::move(setting));
}

GUISetting& GUIModule::addEnum(std::string settingId, std::string label, int value,
                               std::vector<std::string> options, std::string settingDescription) {
    auto setting = std::make_unique<GUISetting>();
    setting->id = std::move(settingId);
    setting->label = std::move(label);
    setting->description = std::move(settingDescription);
    setting->kind = SettingKind::Enum;
    setting->intValue = value;
    setting->options = std::move(options);
    return pushSetting(*this, std::move(setting));
}

GUISetting& GUIModule::addColor(std::string settingId, std::string label, ImVec4 value, std::string settingDescription) {
    auto setting = std::make_unique<GUISetting>();
    setting->id = std::move(settingId);
    setting->label = std::move(label);
    setting->description = std::move(settingDescription);
    setting->kind = SettingKind::Color;
    setting->colorValue = value;
    return pushSetting(*this, std::move(setting));
}

GUISetting& GUIModule::addKeybind(std::string settingId, std::string label, int value, std::string settingDescription) {
    auto setting = std::make_unique<GUISetting>();
    setting->id = std::move(settingId);
    setting->label = std::move(label);
    setting->description = std::move(settingDescription);
    setting->kind = SettingKind::Keybind;
    setting->keyValue = value;
    return pushSetting(*this, std::move(setting));
}

GUISetting& GUIModule::addText(std::string settingId, std::string label, std::string value,
                               std::string settingDescription) {
    auto setting = std::make_unique<GUISetting>();
    setting->id = std::move(settingId);
    setting->label = std::move(label);
    setting->description = std::move(settingDescription);
    setting->kind = SettingKind::Text;
    setting->textValue = std::move(value);
    return pushSetting(*this, std::move(setting));
}

GUISetting& GUIModule::addAction(std::string settingId, std::string label, std::string buttonText,
                                 std::function<void()> onAction, std::string settingDescription) {
    auto setting = std::make_unique<GUISetting>();
    setting->id = std::move(settingId);
    setting->label = std::move(label);
    setting->description = std::move(settingDescription);
    setting->kind = SettingKind::Action;
    setting->actionLabel = std::move(buttonText);
    setting->onAction = std::move(onAction);
    return pushSetting(*this, std::move(setting));
}

void GUIModule::update(float deltaTime) {
    toggleAnim.setTarget(enabled ? 1.0f : 0.0f);

    toggleAnim.update(deltaTime);
    hoverAnim.update(deltaTime);
}

// ---------------------------------------------------------------------------
// ModuleRegistry
// ---------------------------------------------------------------------------
GUIModule& ModuleRegistry::add(std::string id, std::string name, std::string description, Category category) {
    auto module = std::make_unique<GUIModule>();
    module->id = std::move(id);
    module->name = std::move(name);
    module->description = std::move(description);
    module->category = category;
    modules.push_back(std::move(module));
    return *modules.back();
}

std::vector<GUIModule*> ModuleRegistry::byCategory(Category category) const {
    std::vector<GUIModule*> result;
    for (const auto& module : modules) {
        if (category == Category::Favorites) {
            if (module->favorite) {
                result.push_back(module.get());
            }
        } else if (module->category == category) {
            result.push_back(module.get());
        }
    }
    return result;
}

std::vector<GUIModule*> ModuleRegistry::search(const std::string& query) const {
    std::vector<GUIModule*> result;
    if (query.empty()) {
        return result;
    }
    const std::string needle = toLower(query);
    for (const auto& module : modules) {
        if (contains(module->name, needle) || contains(module->description, needle)) {
            result.push_back(module.get());
        }
    }
    return result;
}

void ModuleRegistry::update(float deltaTime) {
    for (auto& module : modules) {
        module->update(deltaTime);
    }
}

// ---------------------------------------------------------------------------
// FriendManager
// ---------------------------------------------------------------------------
bool FriendManager::isFriend(const std::string& name) const {
    return std::any_of(friends.begin(), friends.end(),
                       [&](const PlayerEntry& entry) { return entry.name == name; });
}

bool FriendManager::isTarget(const std::string& name) const {
    return std::any_of(targets.begin(), targets.end(),
                       [&](const PlayerEntry& entry) { return entry.name == name; });
}

void FriendManager::addFriend(const std::string& name) {
    if (name.empty() || isFriend(name)) {
        return;
    }
    friends.push_back(PlayerEntry{name, 0});
    removeTarget(name);
}

void FriendManager::addTarget(const std::string& name) {
    if (name.empty() || isTarget(name)) {
        return;
    }
    targets.push_back(PlayerEntry{name, 0});
    removeFriend(name);
}

void FriendManager::removeFriend(const std::string& name) {
    friends.erase(std::remove_if(friends.begin(), friends.end(),
                                 [&](const PlayerEntry& entry) { return entry.name == name; }),
                  friends.end());
}

void FriendManager::removeTarget(const std::string& name) {
    targets.erase(std::remove_if(targets.begin(), targets.end(),
                                 [&](const PlayerEntry& entry) { return entry.name == name; }),
                  targets.end());
}

// ---------------------------------------------------------------------------
// ConfigManager
// ---------------------------------------------------------------------------
void ConfigManager::load(const std::string& name) {
    for (auto& profile : profiles) {
        profile.active = (profile.name == name);
    }
    if (onLoad) {
        onLoad(name);
    }
}

void ConfigManager::save(const std::string& name) {
    if (onSave) {
        onSave(name);
    }
}

void ConfigManager::remove(const std::string& name) {
    profiles.erase(std::remove_if(profiles.begin(), profiles.end(),
                                  [&](const ConfigProfile& profile) {
                                      return profile.name == name && !profile.builtin;
                                  }),
                   profiles.end());
    if (onDelete) {
        onDelete(name);
    }
}

void ConfigManager::create(const std::string& name) {
    if (name.empty()) {
        return;
    }
    const bool exists = std::any_of(profiles.begin(), profiles.end(),
                                    [&](const ConfigProfile& profile) { return profile.name == name; });
    if (exists) {
        return;
    }
    profiles.push_back(ConfigProfile{name, false, false});
    if (onCreate) {
        onCreate(name);
    }
}

std::string ConfigManager::exportText() const {
    if (onExport) {
        return onExport();
    }
    return "{}";
}

bool ConfigManager::importText(const std::string& text) {
    if (onImport) {
        return onImport(text);
    }
    return !text.empty();
}

// ---------------------------------------------------------------------------
// Demo verisi
//   Kart alt metni = module.description (Vape'teki "90 Horizontal +2 more" ozeti).
//   Kayit sirasi Favorites listesinin sirasini belirler.
// ---------------------------------------------------------------------------
void populateDemoData(ModuleRegistry& registry, FriendManager& friends, ConfigManager& configs) {
    // ---- Favoriler (referans ekranindaki sira) ----
    {
        GUIModule& m = registry.add("velocity", "Velocity", "90 Horizontal, 100 Vertical +2 more", Category::Combat);
        m.icon = ModuleIcon::Arrows;
        m.favorite = true;
        m.enabled = true;
        m.addFloat("horizontal", "Horizontal", 90.0f, 0.0f, 100.0f);
        m.addFloat("vertical", "Vertical", 100.0f, 0.0f, 100.0f);
        m.addEnum("mode", "Mode", 1, {"Normal", "Dynamic"});
        m.addBool("only_targeting", "Only while targeting", true);
    }
    {
        GUIModule& m = registry.add("nametags", "Nametags", "Text Health, Players, 0.3 Size +1 more", Category::Render);
        m.icon = ModuleIcon::Person;
        m.favorite = true;
        m.enabled = true;
        m.addEnum("mode", "Mode", 0, {"Graphical", "Text", "Both"});
        m.addBool("health", "Text Health", true);
        m.addBool("players", "Players", true);
        m.addFloat("scale", "Size", 0.3f, 0.1f, 1.0f);
        m.addColor("name_color", "Name Color", ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    }
    {
        GUIModule& m = registry.add("throwpot", "Throwpot", "100 Delay, Both, Dynamic", Category::Utility);
        m.icon = ModuleIcon::Potion;
        m.favorite = true;
        m.addInt("delay", "Delay (ms)", 100, 0, 500);
        m.addEnum("potion", "Potion", 2, {"Health", "Speed", "Both"});
        m.addBool("dynamic", "Dynamic", true);
    }
    {
        GUIModule& m = registry.add("rightclicker", "RightClicker", "Clicks, Extra, Hold to click +3 more", Category::Combat);
        m.icon = ModuleIcon::Fist;
        m.favorite = true;
        m.addInt("min_cps", "Min CPS", 12, 1, 20);
        m.addInt("max_cps", "Max CPS", 15, 1, 20);
        m.addBool("extra", "Extra", true);
        m.addBool("hold", "Hold to click", false);
    }
    {
        GUIModule& m = registry.add("search", "Search", "Only caves, 30 Range", Category::Render);
        m.icon = ModuleIcon::Search;
        m.favorite = true;
        m.addInt("range", "Range", 30, 5, 100);
        m.addBool("only_caves", "Only caves", true);
    }
    {
        GUIModule& m = registry.add("refill", "Refill", "100 Delay, Both, Horizontal", Category::Utility);
        m.icon = ModuleIcon::Potion;
        m.favorite = true;
        m.addInt("delay", "Delay (ms)", 100, 0, 500);
        m.addEnum("potion", "Potion", 2, {"Health", "Speed", "Both"});
        m.addBool("horizontal", "Horizontal", false);
    }

    // ---- Combat ----
    {
        GUIModule& m = registry.add("autoclicker", "AutoClicker", "12-15 CPS, Extra, Hold to click +2 more", Category::Combat);
        m.icon = ModuleIcon::Generic;
        m.enabled = true;
        m.addInt("min_cps", "Min CPS", 12, 1, 20);
        m.addInt("max_cps", "Max CPS", 15, 1, 20);
    }
    {
        GUIModule& m = registry.add("aimassist", "AimAssist", "60 HS, 0.8 Distance, 120 MA +4 more", Category::Combat);
        m.icon = ModuleIcon::Generic;
        m.enabled = true;
        m.addFloat("hs", "HS", 60.0f, 0.0f, 100.0f);
        m.addFloat("distance", "Distance", 0.8f, 0.0f, 5.0f);
    }
    {
        GUIModule& m = registry.add("reach", "Reach", "3.0-3.2 Reach", Category::Combat);
        m.icon = ModuleIcon::Fist;
        m.addFloat("range", "Range", 3.2f, 3.0f, 6.0f);
        m.addEnum("mode", "Mode", 1, {"Normal", "Combo"});
    }
    {
        GUIModule& m = registry.add("killaura", "KillAura", "3.0 Range, 12 CPS +1 more", Category::Combat);
        m.icon = ModuleIcon::Sword;
        m.keybind = ImGuiKey_R;
        m.addFloat("range", "Range", 3.0f, 1.0f, 6.0f);
        m.addFloat("cps", "CPS", 12.0f, 1.0f, 20.0f);
        m.addEnum("targets", "Targets", 1, {"Players", "Mobs", "Both"});
    }
    {
        GUIModule& m = registry.add("criticals", "Criticals", "Packet", Category::Combat);
        m.icon = ModuleIcon::Sword;
        m.addEnum("mode", "Mode", 0, {"Packet", "Jump"});
    }

    // ---- Render ----
    {
        GUIModule& m = registry.add("tracers", "Tracers", "Invisibles, Players", Category::Render);
        m.icon = ModuleIcon::Arrows;
        m.addBool("invisibles", "Invisibles", true);
        m.addBool("players", "Players", true);
        m.addColor("line_color", "Line Color", ImVec4(0.18f, 0.50f, 0.93f, 1.0f));
    }
    {
        GUIModule& m = registry.add("fullbright", "Fullbright", "Fade", Category::Render);
        m.icon = ModuleIcon::Sun;
        m.enabled = true;
        m.keybind = ImGuiKey_G;
        m.addEnum("mode", "Mode", 0, {"Fade", "Instant"});
        m.addFloat("brightness", "Brightness", 1.2f, 0.1f, 2.5f);
    }
    {
        GUIModule& m = registry.add("esp", "ESP", "2D External", Category::Render);
        m.icon = ModuleIcon::Eye;
        m.addEnum("mode", "Mode", 0, {"2D External", "3D", "Outline"});
        m.addBool("invisibles", "Invisibles", false);
        m.addBool("players", "Players", true);
        m.addColor("players_outline", "Players Outline Color", ImVec4(1.0f, 0.10f, 0.15f, 1.0f));
        m.addColor("players_fill", "Players Fill Color", ImVec4(0.54f, 0.17f, 0.89f, 0.35f));
        m.addBool("mobs", "Mobs", false);
        m.addBool("animals", "Animals", false);
    }
    {
        GUIModule& m = registry.add("storageesp", "StorageESP", "Chests", Category::Render);
        m.icon = ModuleIcon::Generic;
        m.addBool("chests", "Chests", true);
        m.addBool("enders", "Ender Chests", true);
        m.addColor("chest_color", "Chest Color", ImVec4(1.0f, 0.65f, 0.20f, 1.0f));
    }

    // ---- Utility ----
    {
        GUIModule& m = registry.add("blink", "Blink", "Auto Send, 1000 Timeout", Category::Utility);
        m.icon = ModuleIcon::Teleport;
        m.enabled = true;
        m.addInt("timeout", "Timeout (ms)", 1000, 100, 5000);
        m.addBool("auto_send", "Auto Send", true);
    }
    {
        GUIModule& m = registry.add("sprint", "Sprint", "Keep on Water", Category::Utility);
        m.icon = ModuleIcon::Arrows;
        m.enabled = true;
        m.addBool("keep_on_water", "Keep on Water", false);
    }
    {
        GUIModule& m = registry.add("fastplace", "FastPlace", "0 Delay", Category::Utility);
        m.icon = ModuleIcon::Generic;
        m.addInt("delay", "Delay (ms)", 0, 0, 4);
    }

    // ---- FRIENDS (demo) ----
    friends.online = {
        {"Notch", 42}, {"jeb_", 51}, {"Dinnerbone", 66}, {"Grumm", 28},
        {"Hypixel_Dev", 90}, {"FarmerJoe", 120}, {"xX_Player_Xx", 74}
    };
    friends.friends = { friends.online[0], friends.online[2] };
    friends.targets = { friends.online[4] };

    // ---- CONFIGS (demo) ----
    configs.profiles = {
        {"Legit", true, false},
        {"Rage", true, false},
        {"Bedwars", false, true},
        {"Custom", false, false}
    };
}

} // namespace arif::gui
