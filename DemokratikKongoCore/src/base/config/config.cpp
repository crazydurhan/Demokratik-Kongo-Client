#include "config.h"

#include "../moduleManager/moduleManager.h"
#include "../moduleManager/modules/combat/friends.h"
#include "../menu/menu.h"
#include "../gui/guiCore.h"

#include "stealthNames.h"

#include <Windows.h>
#include <Shlobj.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

/*
    Hand-rolled mini-JSON. We never read external JSON, so we can be
    super tolerant of formatting (and we only emit our own schema).
    The on-disk format is human-readable, indented two spaces.
*/

namespace
{
    std::string baseDir()
    {
        wchar_t* path = nullptr;
        SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path);
        std::wstring w(path ? path : L"");
        CoTaskMemFree(path);

        // Proper UTF-8 conversion. The old `std::string(w.begin(), w.end())`
        // truncated each wchar_t to a char, corrupting any non-ASCII path
        // segment (e.g. Turkish user names) and silently breaking save/load.
        std::string s;
        if (!w.empty())
        {
            int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (len > 1)
            {
                s.resize(len - 1);
                WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), len, nullptr, nullptr);
            }
        }
        s += "\\";
        s += dk::stealth::kAppFolderUtf8;
        std::error_code ec;
        fs::create_directories(s, ec);
        return s;
    }

    std::string escape(const std::string& s)
    {
        std::string out; out.reserve(s.size() + 2);
        for (char c : s) {
            if (c == '"' || c == '\\') { out.push_back('\\'); out.push_back(c); }
            else if (c == '\n') out += "\\n";
            else                 out.push_back(c);
        }
        return out;
    }

    // ultra-permissive JSON-ish parser - finds "key": "value" or "key": NUM/BOOL
    // returns the raw token as a string (stripped quotes).
    bool findValue(const std::string& doc, const std::string& objKey,
                   const std::string& key, std::string& out)
    {
        // locate the object scope: "objKey": { ... }
        size_t obj = doc.find('"' + objKey + '"');
        if (obj == std::string::npos) return false;
        size_t open = doc.find('{', obj);
        if (open == std::string::npos) return false;
        // naive matching brace
        int depth = 0; size_t close = open;
        for (size_t i = open; i < doc.size(); ++i) {
            if (doc[i] == '{') depth++;
            else if (doc[i] == '}') { depth--; if (depth == 0) { close = i; break; } }
        }
        if (close <= open) return false;

        std::string scope = doc.substr(open, close - open + 1);
        size_t kp = scope.find('"' + key + '"');
        if (kp == std::string::npos) return false;
        size_t colon = scope.find(':', kp);
        if (colon == std::string::npos) return false;

        size_t start = colon + 1;
        while (start < scope.size() && (scope[start] == ' ' || scope[start] == '\t')) ++start;
        if (start >= scope.size()) return false;

        if (scope[start] == '"') {
            ++start;
            size_t end = scope.find('"', start);
            if (end == std::string::npos) return false;
            out = scope.substr(start, end - start);
        } else {
            size_t end = start;
            while (end < scope.size() && scope[end] != ',' && scope[end] != '}' &&
                   scope[end] != '\n' && scope[end] != '\r') ++end;
            out = scope.substr(start, end - start);
            // trim
            while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) out.pop_back();
        }
        return true;
    }

    // Older profiles used different module keys (AimAssist vs "Aim Assist").
    const char* legacyModuleKey(const std::string& name)
    {
        if (name == "Aim Assist") return "AimAssist";
        if (name == "PlayerESP") return "3D ESP";
        if (name == "Auto Block") return "AutoBlock";
        if (name == "Auto Tool") return "AutoTool";
        if (name == "Fast Mine") return "FastMine";
        if (name == "Knockback Delay") return "KnockbackDelay";
        if (name == "Anti Debuff") return "AntiDebuff";
        if (name == "Item Lock") return "ItemLock";
        if (name == "Item Logger") return "ItemLogger";
        return nullptr;
    }

    bool findModuleValue(const std::string& doc, const std::string& moduleName,
                         const std::string& key, std::string& out)
    {
        if (findValue(doc, moduleName, key, out))
            return true;
        if (const char* legacy = legacyModuleKey(moduleName))
            return findValue(doc, legacy, key, out);
        return false;
    }
}

std::string ProfileManager::profilePath(const std::string& name)
{
    return baseDir() + "\\" + name + ".json";
}

bool ProfileManager::save(const std::string& name)
{
    std::ostringstream o;
    o << "{\n";
    o << "  \"profile\": \"" << escape(name) << "\",\n";
    o << "  \"menuKey\": " << Menu::Keybind << ",\n";

    // GUI preferences (accent colour + interface toggles).
    {
        const Gui::Preferences& p = Gui::Prefs();
        o << "  \"gui\": {\n";
        o << "    \"accentR\": " << p.accent[0] << ",\n";
        o << "    \"accentG\": " << p.accent[1] << ",\n";
        o << "    \"accentB\": " << p.accent[2] << ",\n";
        o << "    \"reduceMotion\": " << (p.reduceMotion ? "true" : "false") << ",\n";
        o << "    \"showDescriptions\": " << (p.showDescriptions ? "true" : "false") << ",\n";
        o << "    \"notifyOnToggle\": " << (p.notifyOnToggle ? "true" : "false") << ",\n";
        o << "    \"notificationsEnabled\": " << (p.notificationsEnabled ? "true" : "false") << ",\n";
        o << "    \"notifyModuleToggles\": " << (p.notifyModuleToggles ? "true" : "false") << ",\n";
        o << "    \"notifyProfileConfigs\": " << (p.notifyProfileConfigs ? "true" : "false") << ",\n";
        o << "    \"notifyFriendToggles\": " << (p.notifyFriendToggles ? "true" : "false") << ",\n";
        o << "    \"notifyPlaySounds\": " << (p.notifyPlaySounds ? "true" : "false") << ",\n";
        o << "    \"menuLayout\": " << p.menuLayout << "\n";
        o << "  },\n";
    }

    // Friends list persistence
    {
        std::ostringstream fcsv;
        bool first = true;
        for (const auto& n : Friends::List) {
            if (!first) fcsv << ",";
            fcsv << n;
            first = false;
        }
        o << "  \"friendsCsv\": \"" << escape(fcsv.str()) << "\",\n";
        o << "  \"friendsMiddleClick\": " << (Friends::MiddleClick ? "true" : "false") << ",\n";

        o << "  \"favoritesCsv\": \"" << escape(Gui::Prefs().favoritesCsv) << "\",\n";
    }

    o << "  \"modules\": {\n";

    auto& all = ModuleManager::All();
    for (size_t i = 0; i < all.size(); ++i)
    {
        Module* m = all[i].get();
        o << "    \"" << escape(m->name()) << "\": {\n";
        o << "      \"_enabled\": " << (m->isEnabled() ? "true" : "false") << ",\n";
        o << "      \"_keybind\": " << m->keybind().virtualKey;

        for (Setting* s : m->settings())
        {
            o << ",\n      \"" << escape(s->name) << "\": ";
            if (s->type == SettingType::Bool)            o << s->toString();
            else if (s->type == SettingType::Number)     o << s->toString();
            else if (s->type == SettingType::Enum)       o << s->toString();
            else if (s->type == SettingType::Keybind)    o << s->toString();
            else /* Color, String, IntRange, Action */    o << "\"" << escape(s->toString()) << "\"";
        }
        o << "\n    }" << (i + 1 < all.size() ? "," : "") << "\n";
    }
    o << "  }\n}\n";

    std::ofstream out(profilePath(name), std::ios::binary);
    if (!out) return false;
    out << o.str();
    ActiveProfile = name;
    return true;
}

bool ProfileManager::load(const std::string& name)
{
    std::ifstream in(profilePath(name), std::ios::binary);
    if (!in) return false;
    std::stringstream ss; ss << in.rdbuf();
    std::string doc = ss.str();

    // Guard against malformed JSON - if parsing fails, return false
    // to load default settings instead of crashing
    try {
    // global
    std::string mk;
    if (findValue(doc, "modules", "_dummy_", mk)) {} // no-op, schema marker
    {
        size_t p = doc.find("\"menuKey\"");
        if (p != std::string::npos) {
            size_t c = doc.find(':', p);
            if (c != std::string::npos) Menu::Keybind = std::atoi(doc.c_str() + c + 1);
        }
    }

    // GUI preferences. Anything missing keeps its current (default) value.
    {
        Gui::Preferences& p = Gui::Prefs();
        std::string raw;

        auto readFloat = [&](const char* key, float& dst) {
            if (findValue(doc, "gui", key, raw))
            {
                try { dst = std::stof(raw); } catch (...) {}
            }
        };
        auto readBool = [&](const char* key, bool& dst) {
            if (findValue(doc, "gui", key, raw))
                dst = (raw == "true" || raw == "1");
        };

        readFloat("accentR", p.accent[0]);
        readFloat("accentG", p.accent[1]);
        readFloat("accentB", p.accent[2]);
        readBool("reduceMotion", p.reduceMotion);
        readBool("showDescriptions", p.showDescriptions);
        readBool("notifyOnToggle", p.notifyOnToggle);
        readBool("notificationsEnabled", p.notificationsEnabled);
        readBool("notifyModuleToggles", p.notifyModuleToggles);
        readBool("notifyProfileConfigs", p.notifyProfileConfigs);
        readBool("notifyFriendToggles", p.notifyFriendToggles);
        readBool("notifyPlaySounds", p.notifyPlaySounds);
        if (findValue(doc, "gui", "menuLayout", raw))
        {
            const int layout = std::atoi(raw.c_str());
            p.menuLayout = (layout == 1) ? 1 : 0;
        }

        Gui::ApplyPreferences();
    }

    // Friends list: prefer new root key, then fall back to legacy gui.friends.
    {
        std::string raw;
        bool hasFriends = false;
        size_t p = doc.find("\"friendsCsv\"");
        if (p != std::string::npos) {
            size_t c = doc.find(':', p);
            size_t q1 = (c != std::string::npos) ? doc.find('"', c + 1) : std::string::npos;
            size_t q2 = (q1 != std::string::npos) ? doc.find('"', q1 + 1) : std::string::npos;
            if (q1 != std::string::npos && q2 != std::string::npos) {
                raw = doc.substr(q1 + 1, q2 - q1 - 1);
                hasFriends = true;
            }
        }
        if (!hasFriends)
            hasFriends = findValue(doc, "gui", "friends", raw);
        if (hasFriends && !raw.empty()) {
            Friends::List.clear();
            size_t start = 0;
            while (start < raw.size()) {
                size_t comma = raw.find(',', start);
                if (comma == std::string::npos) comma = raw.size();
                std::string friendName = raw.substr(start, comma - start);
                if (!friendName.empty()) Friends::List.insert(friendName);
                start = comma + 1;
            }
        }

        // friendsMiddleClick is a root-level key (not inside an object).
        {
            size_t p = doc.find("\"friendsMiddleClick\"");
            if (p != std::string::npos) {
                size_t c = doc.find(':', p);
                if (c != std::string::npos)
                    Friends::MiddleClick = (doc.substr(c + 1).find("true") != std::string::npos);
            }
        }
    }

    // Favourited module names (root-level CSV, mirrors friendsCsv).
    {
        size_t p = doc.find("\"favoritesCsv\"");
        if (p != std::string::npos) {
            size_t c = doc.find(':', p);
            size_t q1 = (c != std::string::npos) ? doc.find('"', c + 1) : std::string::npos;
            size_t q2 = (q1 != std::string::npos) ? doc.find('"', q1 + 1) : std::string::npos;
            if (q1 != std::string::npos && q2 != std::string::npos)
                Gui::Prefs().favoritesCsv = doc.substr(q1 + 1, q2 - q1 - 1);
        }
    }

    auto& all = ModuleManager::All();
    for (auto& mp : all)
    {
        Module* m = mp.get();

        std::string raw;
        if (m->toggleable() && findModuleValue(doc, m->name(), "_enabled", raw))
            m->setEnabled(raw == "true" || raw == "1");
        if (m->keybindEditable() && findModuleValue(doc, m->name(), "_keybind", raw))
            m->keybind().virtualKey = std::atoi(raw.c_str());

        for (Setting* s : m->settings()) {
            if (findModuleValue(doc, m->name(), s->name, raw))
                s->fromString(raw);
        }
    }
    ActiveProfile = name;
    return true;

    } catch (...) {
        // Malformed JSON - return false to load defaults
        return false;
    }
}

bool ProfileManager::remove(const std::string& name)
{
    if (name.empty() || name == "default")
        return false;

    std::error_code ec;
    const bool ok = fs::remove(profilePath(name), ec) && !ec;
    if (ok && ActiveProfile == name)
        ActiveProfile = "default";
    return ok;
}

std::vector<std::string> ProfileManager::listProfiles()
{
    std::vector<std::string> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(baseDir(), ec)) {
        if (e.path().extension() == ".json") out.push_back(e.path().stem().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}
