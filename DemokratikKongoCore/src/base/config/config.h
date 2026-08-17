#pragma once

#include <string>
#include <vector>

/*
========================================================================
    PROJECTX :: Config / Profile system
------------------------------------------------------------------------
    Persists every Setting on every Module to disk as a single JSON file
    per profile. Schema:

        {
          "profile": "default",
          "menuKey": 45,
          "modules": {
            "ModuleName": {
              "_enabled":  true,
              "_keybind":  82,
              "Setting":   40.0,
              ...
            },
            ...
          }
        }

    The serializer is hand-written (no third-party JSON dep) and only
    supports the subset we emit. It's tiny, deterministic, and stable.
========================================================================
*/

class ProfileManager
{
public:
    static inline std::string ActiveProfile = "default";

    // Path defaults to %APPDATA%/RuntimeHost/<name>.json
    static std::string profilePath(const std::string& name);

    // Save / load every module's settings into the profile file.
    // Returns true on success.
    static bool save(const std::string& name);
    static bool load(const std::string& name);
    static bool remove(const std::string& name);

    // Lists every .json profile in %APPDATA%/RuntimeHost
    static std::vector<std::string> listProfiles();
};
