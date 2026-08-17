#pragma once

#include <Windows.h>
#include <vector>
#include <memory>

#include "module.h"

/*
========================================================================
    PROJECTX :: ModuleManager
------------------------------------------------------------------------
    Owns the global module list, dispatches lifecycle events, and feeds
    the GUI with grouped-by-category iterators.
========================================================================
*/

struct ModuleManager
{
    static void Init();
    static void Kill();

    // --- lifecycle ----------------------------------------------------------
    static void UpdateModules();              // called every cheat-loop iteration
    static void RunPatcherTick();
    static void RunDataUpdate();
    static void RunModuleTicks();
    static void OnRender2D();                 // called every frame, 2D pass
    static void OnRender3D(float partialTicks); // called from JVM via ClassPatcher
    static void OnGameTick();                   // client thread — walking update pre
    static void OnRunTickPre();                 // client thread — Minecraft.runTick HEAD
    static void OnKey(int vk, bool down, LPARAM lParam = 0);

    // --- query --------------------------------------------------------------
    static const std::vector<std::unique_ptr<Module>>& All();
    static std::vector<Module*> ByCategory(Category c);

    template <typename T>
    static T* Get() {
        for (auto& m : All()) if (auto* p = dynamic_cast<T*>(m.get())) return p;
        return nullptr;
    }
};
