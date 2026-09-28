#pragma once

#include <string>
#include <vector>
#include <memory>

#include "settings/setting.h"

/*
========================================================================
    PROJECTX :: Module
------------------------------------------------------------------------
    The Module base. Every concrete module (ESP, Reach, ...) inherits
    from this. Modules:
      - declare their settings via add<...>(...)
      - implement onEnable/onDisable for lifecycle
      - opt-in to events by overriding onTick / onRender / onKey

    The ModuleManager owns the unique_ptr<Module> list. The GUI just
    iterates getCategoryModules() for rendering.
========================================================================
*/

enum class Category : uint8_t
{
    Combat,
    Movement,
    Render,
    Utility,
    Misc,
    _Count
};

// Used by ArrayList::SuffixMode -> the GUI passes one of these values to
// Module::arrayListSuffix() so the module can format itself appropriately.
enum class SuffixDetail : uint8_t
{
    None,        // do not show any suffix
    Basic,       // a short summary (e.g. selected mode)
    Extended,    // detailed runtime stats (e.g. CPS, range, ...)
};

    inline const char* CategoryName(Category c)
    {
        switch (c) {
            case Category::Combat:   return "Combat";
            case Category::Movement: return "Movement";
            case Category::Render:   return "Render";
            case Category::Utility:  return "Utility";
            case Category::Misc:     return "Misc";
            default: return "?";
        }
    }

class Module
{
public:
    Module(const char* name, const char* desc, Category cat, int defaultKey = 0)
        : m_name(name), m_description(desc), m_category(cat),
          m_enabled("Enabled", false),
          m_keybind("Keybind", defaultKey)
    {
    }

    virtual ~Module() = default;

    // ------------------------------------------------------------- core API
    virtual void onEnable()  {}
    virtual void onDisable() {}
    virtual void onTick()    {}      // game tick
    virtual void onRender2D() {}     // 2D overlay (ImDrawList in screen space)

    // One-shot modules (e.g. XrayBypass) keep drawing their results after
    // auto-disable; the manager still dispatches onRender2D for them.
    virtual bool renderWhenDisabled() const { return false; }

    // 3D world-space pass. Called from inside Minecraft's world render,
    // AFTER MC has set up its modelview/projection matrices but BEFORE
    // particles. The model matrix is in CAMERA SPACE (entity positions
    // need to be expressed relative to the interpolated camera origin).
    // partialTicks lets you interpolate animations smoothly.
    //
    // Wired via an ASM patch on EntityRenderer.renderWorldPass that
    // calls ClassPatcher.invokeNative3DHook(F)V at the appropriate
    // injection point; the JVM bounces back to C++ through a
    // RegisterNatives'd JNI bridge into ModuleManager::OnRender3D.
    virtual void onRender3D(float /*partialTicks*/) {}

    // Optional ArrayList suffix. Default returns empty; specific modules can
    // override to return e.g. "12 CPS" or "Mode: Smooth" so the ArrayList
    // HUD can render extra context next to the module name.
    virtual std::string arrayListSuffix(SuffixDetail /*detail*/) const { return ""; }

    // Optional per-module ArrayList color override. Returns {a < 0} to mean
    // "no override" so the ArrayList falls back to its global Color Mode.
    // Modules use this to flash a row red while actively firing (e.g. Reach
    // is extending range) or amber while holding a buffered packet, etc.
    // Caller checks the returned alpha (`a >= 0`) to decide whether to use.
    virtual Color arrayListColorOverride() const { return Color{ -1, -1, -1, -1 }; }

    // Whether this module can be turned on/off at all. Pure "settings
    // holder" modules (e.g. the ClickGUI customization module) return
    // false: the GUI hides their toggle, keybinds won't flip them and the
    // ArrayList never lists them.
    virtual bool toggleable() const { return true; }

    // When false the module stays registered (ticks, config, keybinds) but
    // is omitted from the ClickGUI grid, search, and binds overview.
    virtual bool showInMenu() const { return true; }

    // Whether the ClickGUI module card shows an editable keybind chip.
    // ClickGUI keeps true; Friends returns false.
    virtual bool keybindEditable() const { return true; }

    bool hideFromArrayList() const;

    // The GUI drives this from a control on the module card instead of
    // listing it with the module's own settings.
    BoolSetting* hideFromArrayListSetting() { return m_hideFromArrayList; }

    // ------------------------------------------------------------ accessors
    const char* name()        const { return m_name; }
    const char* description() const { return m_description; }
    Category    category()    const { return m_category; }

    bool        isEnabled()   const { return m_enabled.value; }
    void        setEnabled(bool v);
    void        toggle() { setEnabled(!isEnabled()); }

    KeybindSetting&            keybind()           { return m_keybind; }
    BoolSetting&               enabled()           { return m_enabled;  }
    std::vector<Setting*>&     settings()          { return m_settings; }

    // GUI-driven: should the settings panel be open ?
    bool& expanded() { return m_expanded; }

    // animation state owned by GUI; lives here so it survives across frames
    float& expandAnim() { return m_expandAnim; }

    // Called once by ModuleManager after each module is constructed.
    void finalizeRegistration();

protected:
    // Helper used by subclasses to register a setting at construction.
    // The Setting object lives in the heap via unique_ptr.
    template <typename T, typename... Args>
    T& add(Args&&... args)
    {
        auto* s = new T(std::forward<Args>(args)...);
        s->captureDefault();
        m_owned.emplace_back(s);
        m_settings.push_back(s);
        return *s;
    }

    void appendCommonSettings();

    const char* const m_name;
    const char* const m_description;
    const Category    m_category;

    BoolSetting       m_enabled;
    KeybindSetting    m_keybind;

    std::vector<Setting*>                  m_settings; // weak ptrs, in render order
    std::vector<std::unique_ptr<Setting>>  m_owned;    // ownership

    BoolSetting* m_hideFromArrayList   = nullptr;
    bool         m_commonSettingsAdded = false;

    bool   m_expanded   = false;
    float  m_expandAnim = 0.0f;
};
