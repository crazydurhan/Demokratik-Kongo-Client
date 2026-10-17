// ArifClient ClickGUI - Ana pencere ve layout.
#pragma once

#include <functional>
#include <string>

#include "arif/gui/Model.hpp"
#include "arif/gui/Theme.hpp"

namespace arif::gui {

struct SessionStats {
    int fps = 0;
    int ping = 0;
    std::string username = "Player";
};

// Ust navigasyon sekmeleri (frameless pencere)
// DK-PORT: Scripts -> Friends (the client has no scripts; it has a friends
// manager, whose page already lives in Panels.cpp).
enum class Tab {
    Home,
    Profiles,
    Friends,
    Count
};

const char* tabName(Tab tab);

// ---------------------------------------------------------------------------
// Ana ClickGUI.
//
//   +--------------------------------------------------------------------+
//   | ARIF   [Home] [][] []                          [] [] [] [_-] [X]    |  TOP BAR
//   +--------------------------------------------------------------------+
//   | SIDEBAR       | CONTENT (tek sutun modul listesi)                   |
//   |  Favorites    |  [ icon  Title               slider  toggle  ::: ]  |
//   |  Combat       |  [ icon  Title (secili: mavi gradyan) ...        ]  |
//   |  Visual       |                                                     |
//   |  Utility      |                                                     |
//   |  [][][] grid  |                                                     |
//   +--------------------------------------------------------------------+
// ---------------------------------------------------------------------------
class ClickGUI {
public:
    void initialize();
    void update(float deltaTime);
    void RenderClickGUI();

    void toggle() { open = !open; }
    bool isOpen() const { return open; }

    // DK-PORT: drop any reference to GUI modules (the host rebuilds the
    // registry when the client's module list changes). Invalidates the
    // inspected module AND the closing-panel pointer, so no draw path can
    // dereference a freed GUIModule.
    void invalidateSelection();

    // Frameless pencere kancalari (main.cpp tarafindan baglanir)
    std::function<void()> onMinimize;
    std::function<void()> onClose;
    std::function<void(float, float)> onDragDelta;

    // Durum: oyun entegrasyonu bu alanlari doldurur (UI bagimsizdir)
    bool open = false;
    Tab activeTab = Tab::Home;
    Category selectedCategory = Category::Favorites;
    SessionStats stats;
    ModuleRegistry modules;
    FriendManager friends;
    ConfigManager configs;
    GUIModule* inspected = nullptr;  // inspector'da gosterilen modul

private:
    void navigateTab(Tab tab);
    void navigateCategory(Category category);
    void renderTopBar();
    void renderSidebar(const Rect& area);
    void renderContent(const Rect& area, bool detailMode);

    char friendSearchBuffer_[64] = {};
    char newConfigBuffer_[48] = {};
    bool draggingWindow_ = false;
    int dragApplyFrame_ = -1;    // ayni karede iki kez surukleme deltasi uygulanmaz
    bool optionsOpen_ = false;   // gear ile acilan overlay

    // DK-PORT: ana pencere artik full-screen degil, serbest duran bir panel.
    // Konum/boyut ekran uzayinda tutulur; ilk acilista ortalanir, baslik
    // seridinden veya ana bardan suruklenir, her zaman ekran icinde kalir.
    ImVec2 framePos_{};
    ImVec2 frameSize_{};
    bool framePlaced_ = false;

    AnimationState openAnim_{0.0f};
    AnimationState panelAnim_{0.0f};   // sag overlay paneli (Options / modul ayarlari)
    AnimationState detailAnim_{0.0f};  // modul ayarlari: sidebar cikis + liste sola kayma
    GUIModule* panelShown_ = nullptr;  // kapanis animasyonu icin son icerik
    TimedAnimation refreshSpin_;
    AnimationState tabAnim_[static_cast<int>(Tab::Count)];
    AnimationState categoryAnim_[static_cast<int>(Category::Count)];
};

} // namespace arif::gui
