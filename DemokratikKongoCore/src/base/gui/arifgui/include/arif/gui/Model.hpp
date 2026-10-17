// ArifClient ClickGUI - Veri modeli: modul, ayar ve yoneticiler.
// Oyun mantigi yoktur; sadece UI'nin ihtiyac duydugu durum.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <imgui.h>

#include "arif/gui/Animation.hpp"
#include "arif/gui/Widgets.hpp"

namespace arif::gui {

// Sol sidebar kategorileri
// DK-PORT: Movement + Misc added so the real client module taxonomy maps 1:1.
enum class Category {
    Favorites,
    Combat,
    Movement,
    Render,
    Utility,
    Misc,
    Count
};

// Modul kartlarinda kullanilan vektorel ikonlar
enum class ModuleIcon {
    Sun,
    Fist,
    Potion,
    Arrows,
    Teleport,
    Sword,
    Eye,
    Person,
    Search,
    Generic
};

const char* categoryName(Category category);

// ---------------------------------------------------------------------------
// Ayar tipleri
// ---------------------------------------------------------------------------
enum class SettingKind {
    Bool,
    Int,
    Float,
    Enum,
    Color,
    Keybind,
    // DK-PORT: host integration - free text rows and action buttons.
    Text,
    Action
};

struct GUISetting {
    std::string id;
    std::string label;
    std::string description;
    SettingKind kind = SettingKind::Bool;

    // Degerler
    bool  boolValue = false;
    int   intValue = 0;
    float floatValue = 0.0f;
    ImVec4 colorValue = ImVec4(0.54f, 0.17f, 0.89f, 1.0f);
    int   keyValue = 0;

    // Aralik / secenekler
    int intMin = 0;
    int intMax = 100;
    float floatMin = 0.0f;
    float floatMax = 1.0f;
    const char* floatFormat = "%.2f";
    std::vector<std::string> options;

    // Widget durumlari
    widgets::EditState edit;
    bool listeningKey = false;

    // DK-PORT: host integration fields.
    std::string textValue;              // SettingKind::Text current value
    char        textBuffer[264] = {};   // widget-side edit buffer (Text)
    std::string actionLabel;            // SettingKind::Action button face
    std::function<void()> onAction;     // SettingKind::Action callback

    // Kosullu gorunurluk (or. yalnizca "Mode == X" iken goster)
    std::function<bool()> visibleIf;

    bool visible() const { return !visibleIf || visibleIf(); }
};

// ---------------------------------------------------------------------------
// Modul: bir kart = baslik + toggle + akordeon ayar listesi
// ---------------------------------------------------------------------------
class GUIModule {
public:
    std::string id;
    std::string name;
    std::string description;   // kart alt metni (sub-text) + arama
    Category category = Category::Combat;
    ModuleIcon icon = ModuleIcon::Generic;

    bool enabled = false;
    bool favorite = false;
    int  keybind = 0;

    // Animasyonlar (0..1)
    AnimationState toggleAnim;
    AnimationState hoverAnim;

    std::vector<std::unique_ptr<GUISetting>> settings;

    GUISetting& addBool(std::string settingId, std::string label, bool value, std::string settingDescription = "");
    GUISetting& addInt(std::string settingId, std::string label, int value, int min, int max,
                       std::string settingDescription = "");
    GUISetting& addFloat(std::string settingId, std::string label, float value, float min, float max,
                         std::string settingDescription = "", const char* format = "%.2f");
    GUISetting& addEnum(std::string settingId, std::string label, int value, std::vector<std::string> options,
                        std::string settingDescription = "");
    GUISetting& addColor(std::string settingId, std::string label, ImVec4 value, std::string settingDescription = "");
    GUISetting& addKeybind(std::string settingId, std::string label, int value, std::string settingDescription = "");

    // DK-PORT: host integration rows (free text + action button).
    GUISetting& addText(std::string settingId, std::string label, std::string value,
                        std::string settingDescription = "");
    GUISetting& addAction(std::string settingId, std::string label, std::string buttonText,
                          std::function<void()> onAction, std::string settingDescription = "");

    void update(float deltaTime);
};

// ---------------------------------------------------------------------------
// Kayit defteri
// ---------------------------------------------------------------------------
class ModuleRegistry {
public:
    std::vector<std::unique_ptr<GUIModule>> modules;

    GUIModule& add(std::string id, std::string name, std::string description, Category category);
    std::vector<GUIModule*> byCategory(Category category) const;
    std::vector<GUIModule*> search(const std::string& query) const;
    void update(float deltaTime);
};

// ---------------------------------------------------------------------------
// Friends / Target manager
// ---------------------------------------------------------------------------
struct PlayerEntry {
    std::string name;
    int ping = 0;
};

class FriendManager {
public:
    std::vector<PlayerEntry> online;
    std::vector<PlayerEntry> friends;
    std::vector<PlayerEntry> targets;

    bool isFriend(const std::string& name) const;
    bool isTarget(const std::string& name) const;
    void addFriend(const std::string& name);
    void addTarget(const std::string& name);
    void removeFriend(const std::string& name);
    void removeTarget(const std::string& name);
};

// ---------------------------------------------------------------------------
// Config / Profile manager
// ---------------------------------------------------------------------------
struct ConfigProfile {
    std::string name;
    bool builtin = false;
    bool active = false;
};

class ConfigManager {
public:
    std::vector<ConfigProfile> profiles;

    // Oyun tarafi entegrasyon kancalari (UI bunlara bagimli degildir).
    std::function<void(const std::string&)> onLoad;
    std::function<void(const std::string&)> onSave;
    std::function<void(const std::string&)> onDelete;
    std::function<void(const std::string&)> onCreate;
    std::function<std::string()> onExport;
    std::function<bool(const std::string&)> onImport;

    void load(const std::string& name);
    void save(const std::string& name);
    void remove(const std::string& name);
    void create(const std::string& name);
    std::string exportText() const;
    bool importText(const std::string& text);
};

// Demo icerik (oyun baglantisi olmadan ClickGUI'yi test etmek icin).
void populateDemoData(ModuleRegistry& registry, FriendManager& friends, ConfigManager& configs);

} // namespace arif::gui
