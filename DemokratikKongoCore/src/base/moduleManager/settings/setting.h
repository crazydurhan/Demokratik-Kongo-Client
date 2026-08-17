#pragma once

#include <string>
#include <vector>
#include <sstream>
#include <functional>

/*
========================================================================
    PROJECTX :: Setting Hierarchy (Data Binding Backbone)
------------------------------------------------------------------------
    Every Module owns a list of Setting* pointers. The GUI iterates the
    list, picks a renderer per Setting type via the polymorphic getType()
    enum, and edits the value in-place.

    This is the SINGLE source of truth for module state.
    No more "inline static bool Enabled = true" littering the codebase.

    Persistence is done by serializing each Setting to a JSON-ish line
    "<owner>.<name> = <value>" via toString()/fromString().
========================================================================
*/

enum class SettingType : uint8_t
{
    Bool,
    Number,
    Enum,
    Keybind,
    Color,
    String,
    IntRange,
    FloatRange,
    Action
};

struct Setting
{
    Setting(const char* n, SettingType t) : name(n), type(t) {}
    virtual ~Setting() = default;

    const char* const name;
    const SettingType  type;

    // human friendly tooltip / hover text (optional)
    const char* description = "";

    // visibility predicate: lets a Setting be hidden if its parent
    // option is disabled (e.g. hide "Smooth" if mode != "Smooth").
    std::function<bool()> visible = []{ return true; };

    virtual std::string toString() const = 0;
    virtual void        fromString(const std::string& s) = 0;

    // Default-value snapshot. Captured by Module::add() right after
    // construction so any setting can later be restored via
    // resetToDefault() (GUI: Ctrl + right-click on the setting row).
    std::string defaultSnapshot;
    bool        hasDefault = false;
    void captureDefault() { defaultSnapshot = toString(); hasDefault = true; }
    void resetToDefault() { if (hasDefault) fromString(defaultSnapshot); }
};

// ----------------------------------------------------------- BoolSetting
struct BoolSetting : Setting
{
    bool value;
    BoolSetting(const char* n, bool def) : Setting(n, SettingType::Bool), value(def) {}
    std::string toString() const override { return value ? "true" : "false"; }
    void fromString(const std::string& s) override { value = (s == "true" || s == "1"); }
    operator bool() const { return value; }
};

// --------------------------------------------------------- NumberSetting
struct NumberSetting : Setting
{
    float value, min, max, step;
    const char* suffix = "";

    // When true the GUI renders this as a discrete slot picker (hotbar chips)
    // instead of a slider. Only meaningful for small integer ranges.
    bool slotPicker = false;

    NumberSetting(const char* n, float def, float mn, float mx, float st = 0.1f)
        : Setting(n, SettingType::Number), value(def), min(mn), max(mx), step(st) {}
    std::string toString() const override { std::ostringstream os; os << value; return os.str(); }
    void fromString(const std::string& s) override { try { value = std::stof(s); } catch(...) {} }
    operator float() const { return value; }
};

// ----------------------------------------------------------- EnumSetting
struct EnumSetting : Setting
{
    int   index;
    std::vector<const char*> options;
    EnumSetting(const char* n, std::vector<const char*> opts, int def = 0)
        : Setting(n, SettingType::Enum), index(def), options(std::move(opts)) {}
    const char* current() const { return options[index]; }
    std::string toString() const override { std::ostringstream os; os << index; return os.str(); }
    void fromString(const std::string& s) override {
        try { int v = std::stoi(s); if (v >= 0 && v < (int)options.size()) index = v; } catch(...) {}
    }
};

// -------------------------------------------------------- KeybindSetting
struct KeybindSetting : Setting
{
    int  virtualKey;        // 0 = unbound
    bool listening = false; // GUI sets this true while waiting for input
    KeybindSetting(const char* n, int def = 0)
        : Setting(n, SettingType::Keybind), virtualKey(def) {}
    std::string toString() const override { std::ostringstream os; os << virtualKey; return os.str(); }
    void fromString(const std::string& s) override { try { virtualKey = std::stoi(s); } catch(...) {} }
};

// ---------------------------------------------------------- ColorSetting
struct Color
{
    float r, g, b, a;
    uint32_t toU32() const {
        auto c = [](float x){ return (uint8_t)(x < 0 ? 0 : (x > 1 ? 255 : x * 255)); };
        return ((uint32_t)c(a) << 24) | ((uint32_t)c(b) << 16) | ((uint32_t)c(g) << 8) | (uint32_t)c(r);
    }
};

struct ColorSetting : Setting
{
    Color value;
    ColorSetting(const char* n, Color def)
        : Setting(n, SettingType::Color), value(def) {}
    std::string toString() const override {
        std::ostringstream os;
        os << value.r << ',' << value.g << ',' << value.b << ',' << value.a;
        return os.str();
    }
    void fromString(const std::string& s) override {
        std::stringstream ss(s);
        char comma;
        try { ss >> value.r >> comma >> value.g >> comma >> value.b >> comma >> value.a; } catch(...) {}
    }
};

// ---------------------------------------------------------- StringSetting
// A user-editable, single-line text value. Used for things like a custom
// HUD watermark/overlay text, server addresses, etc.
struct StringSetting : Setting
{
    std::string value;
    size_t      maxLength;   // soft cap - UI prevents typing past this

    // When true the GUI renders this comma separated value as a chip picker
    // (preset items plus user added entries) instead of a plain text field.
    bool itemList = false;

    // When true the GUI renders this comma separated value as a hotbar slot
    // picker (chips 1-9, multi-select) instead of a plain text field.
    bool slotPicker = false;

    StringSetting(const char* n, std::string def, size_t maxLen = 64)
        : Setting(n, SettingType::String), value(std::move(def)), maxLength(maxLen) {}

    std::string toString() const override { return value; }
    void fromString(const std::string& s) override { value = s; }

    operator const std::string&() const { return value; }
    const char* c_str() const { return value.c_str(); }
};

// ------------------------------------------------------- IntRangeSetting
// A paired (low, high) integer setting rendered as a single double-handle
// slider. Useful for things like "Min CPS .. Max CPS" or "Min Delay .. Max
// Delay" that currently require two separate NumberSetting rows.
struct IntRangeSetting : Setting
{
    int low;
    int high;
    int min;
    int max;
    int step;
    const char* suffix = "";

    IntRangeSetting(const char* n, int lo, int hi, int mn, int mx, int st = 1)
        : Setting(n, SettingType::IntRange),
          low(lo), high(hi), min(mn), max(mx), step(st) {}

    int  getLow()  const { return low <= high ? low  : high; }
    int  getHigh() const { return low <= high ? high : low;  }

    std::string toString() const override {
        std::ostringstream os; os << low << ',' << high; return os.str();
    }
    void fromString(const std::string& s) override {
        std::stringstream ss(s);
        char comma;
        try { ss >> low >> comma >> high; } catch(...) {}
    }
};

// ----------------------------------------------------- FloatRangeSetting
// Same as IntRangeSetting but for fractional values (e.g. reach blocks).
struct FloatRangeSetting : Setting
{
    float low;
    float high;
    float min;
    float max;
    float step;
    const char* suffix = "";

    FloatRangeSetting(const char* n, float lo, float hi, float mn, float mx, float st = 0.1f)
        : Setting(n, SettingType::FloatRange),
          low(lo), high(hi), min(mn), max(mx), step(st) {}

    float getLow()  const { return low <= high ? low  : high; }
    float getHigh() const { return low <= high ? high : low;  }

    std::string toString() const override {
        std::ostringstream os; os << low << ',' << high; return os.str();
    }
    void fromString(const std::string& s) override {
        std::stringstream ss(s);
        char comma;
        try { ss >> low >> comma >> high; } catch(...) {}
    }
};

// --------------------------------------------------------- ActionSetting
// A clickable button setting. Renders as a label + button that fires the
// given callback when pressed. Optional valueProvider returns a short
// string shown on the button face (e.g. "OPEN", "RESET", "12 entries").
// Persisted as nothing (no state of its own).
struct ActionSetting : Setting
{
    std::function<void()>        onClick;
    std::function<std::string()> valueProvider;
    const char*                  buttonText = "RUN";

    ActionSetting(const char* n, std::function<void()> cb)
        : Setting(n, SettingType::Action), onClick(std::move(cb)) {}

    ActionSetting(const char* n, std::function<void()> cb, const char* btn)
        : Setting(n, SettingType::Action), onClick(std::move(cb)), buttonText(btn) {}

    std::string toString() const override { return ""; }
    void fromString(const std::string&) override {}

    std::string label() const {
        return valueProvider ? valueProvider() : std::string(buttonText);
    }
};
