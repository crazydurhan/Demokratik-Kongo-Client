#include "arif/gui/ClickGUI.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "arif/gui/IconPack.hpp"
#include "arif/gui/Icons.hpp"
#include "arif/gui/Panels.hpp"
#include "arif/gui/Widgets.hpp"

namespace arif::gui {

namespace {
// ---------------------------------------------------------------------------
// Geometri (8px grid)
//
//   |<------------------- window ------------------->|
//   | 0 |           title strip (28)  [—][x]        |  <- surukleme + pencere kontrolleri
//   | 0 |  logo  [Home][▭][☰]         [⟳][⚙]        |  <- ana bar (56)
//   | 16 | sidebar | 16 | content ... | 16 |        |
// ---------------------------------------------------------------------------
constexpr float kTitleStripH = 28.0f;                                    // en ust surukleme seridi
constexpr float kTopBarH     = 56.0f;                                    // ana bar
constexpr float kOuter       = kUnit * 2.0f;                             // 16
constexpr float kSidebarW    = 220.0f;
constexpr float kSidebarGap  = kUnit * 3.0f;                             // 24
constexpr float kContentPad  = kUnit * 2.0f;                             // 16
constexpr float kContentLeft = kOuter + kSidebarW + kSidebarGap;         // 260
constexpr float kContentTop  = kTitleStripH + kTopBarH + kOuter;         // 100
constexpr float kContentHead = kUnit * 6.0f;                             // 48 (baslikli sayfalar)
constexpr float kDockW       = kUnit * 42.0f;                            // 336 sag panel
constexpr float kBlockGap    = kUnit * 2.0f;                             // 16
constexpr float kNavButton   = 32.0f;
constexpr float kNavGap      = kUnit;

// DK-PORT: serbest duran pencere olculeri. Boyut sabittir; kucuk ekranlarda
// oransal olarak kuculur (bkz. RenderClickGUI) ve pencere asla ekrandan
// tamamen tasmaz.
constexpr float kWindowW     = 1160.0f;                                  // varsayilan genislik
constexpr float kWindowH     = 680.0f;                                   // varsayilan yukseklik
constexpr float kWindowFill  = 0.86f;                                    // ekranin en fazla %86'si
constexpr float kWindowRound = 12.0f;                                    // pencere kose yuvarlakligi
} // namespace

const char* tabName(Tab tab) {
    switch (tab) {
        case Tab::Home:     return "HOME";
        case Tab::Profiles: return "PROFILES";
        case Tab::Friends:  return "FRIENDS";
        default:            return "";
    }
}

void ClickGUI::initialize() {
    SetupGUIStyle();
    LoadFonts(1.0f);
    // DK-PORT: no demo content. The host (arifHost.cpp) populates
    // modules/friends/configs from the live client each frame.
    openAnim_.snap(0.0f);
}

void ClickGUI::update(float deltaTime) {
    openAnim_.setTarget(open ? 1.0f : 0.0f);
    openAnim_.update(deltaTime);

    // ESC: en ustteki panel kapanir (once Options, sonra modul ayarlari).
    // Acik bir popup (inline deger girisi) varken ESC yalnizca popup'i kapatir.
    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (optionsOpen_) {
            optionsOpen_ = false;
        } else if (inspected != nullptr) {
            inspected = nullptr;
        }
    }

    const bool panelOpen = (inspected != nullptr) || optionsOpen_;
    panelAnim_.setTarget(panelOpen ? 1.0f : 0.0f);
    panelAnim_.update(deltaTime);

    // Modul ayarlari (detay): sidebar soldan cikar, liste sola kayar.
    // panelAnim_ ile ayni hizda ilerler; boylece panel ile icerik senkron kayar.
    const bool detailOpen = (inspected != nullptr) && (activeTab == Tab::Home);
    detailAnim_.setTarget(detailOpen ? 1.0f : 0.0f);
    detailAnim_.update(deltaTime);

    refreshSpin_.update(deltaTime);
    modules.update(deltaTime);
}

void ClickGUI::navigateTab(Tab tab) {
    activeTab = tab;
    inspected = nullptr;
    optionsOpen_ = false;
}

void ClickGUI::navigateCategory(Category category) {
    selectedCategory = category;
    activeTab = Tab::Home;
    inspected = nullptr;
    optionsOpen_ = false;
}

void ClickGUI::invalidateSelection() {
    // DK-PORT: see ClickGUI.hpp - called by the host before the module
    // registry is rebuilt.
    inspected = nullptr;
    panelShown_ = nullptr;
    optionsOpen_ = false;
}

// ---------------------------------------------------------------------------
// Ana cizim
// ---------------------------------------------------------------------------
void ClickGUI::RenderClickGUI() {
    if (!open && openAnim_.value() < 0.01f) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 display = io.DisplaySize;

    // DK-PORT: oyunun tamamini kaplayan bir perde YOK. Pencere sabit boyutlu
    // serbest bir panel: ilk acilista ortalanir, baslik seridinden / ana
    // bardan suruklenir ve daima ekran icinde kalir.
    frameSize_.x = std::min(kWindowW, display.x * kWindowFill);
    frameSize_.y = std::min(kWindowH, display.y * kWindowFill);
    if (!framePlaced_) {
        framePlaced_ = true;
        framePos_ = ImVec2((display.x - frameSize_.x) * 0.5f, (display.y - frameSize_.y) * 0.5f);
    }
    framePos_.x = std::min(std::max(0.0f, framePos_.x), std::max(0.0f, display.x - frameSize_.x));
    framePos_.y = std::min(std::max(0.0f, framePos_.y), std::max(0.0f, display.y - frameSize_.y));

    ImGui::SetNextWindowPos(framePos_, ImGuiCond_Always);
    ImGui::SetNextWindowSize(frameSize_, ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                             ImGuiWindowFlags_NoSavedSettings;
    // DK-PORT: NoBringToFrontOnFocus removed - the in-game overlay draws the
    // module HUD in a second window and the menu must stay focused on top,
    // exactly like the previous ClickGUI did.
    if (!open) {
        flags |= ImGuiWindowFlags_NoInputs;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, std::min(1.0f, std::max(0.0f, openAnim_.value())));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::Begin("##arif_clickgui", nullptr, flags);

    const ImVec2 windowPos = ImGui::GetWindowPos();
    const ImVec2 windowSize = ImGui::GetWindowSize();
    const ImVec2 windowMax(windowPos.x + windowSize.x, windowPos.y + windowSize.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Arka plan: yalnizca pencere alani doldurulur; disarida oyun gorunur.
    dl->AddRectFilled(windowPos, windowMax, palette().bgTop, kWindowRound);

    const Rect sidebar{ImVec2(windowPos.x + kOuter, windowPos.y + kContentTop),
                       ImVec2(windowPos.x + kOuter + kSidebarW, windowPos.y + windowSize.y - kOuter)};

    // Detay modu (modul ayarlari): sidebar soldan kayarak cikar, liste sola kayar.
    // Gecis detailAnim_ ile animasyonludur; panel ile ayni hizda ilerler.
    const bool homeTab = (activeTab == Tab::Home);
    const bool detailMode = (inspected != nullptr) && homeTab;
    const float detailSlide = std::min(1.0f, std::max(0.0f, detailAnim_.value()));
    const float contentLeft = homeTab ? (kContentLeft + (kOuter - kContentLeft) * detailSlide)
                                      : kOuter;
    const float rightInset = kOuter + ((kDockW + kUnit) - kOuter) * detailSlide;
    const float contentRight = windowPos.x + windowSize.x - rightInset;
    const Rect content{ImVec2(windowPos.x + contentLeft, windowPos.y + kContentTop),
                       ImVec2(contentRight, windowPos.y + windowSize.y - kOuter)};

    renderTopBar();
    if (homeTab) {
        // Sidebar detay acilirken sola kayip soluyor; kapaninca geri gelir.
        const float offset = -detailSlide * (kSidebarW + kSidebarGap);
        Rect sidebarArea = sidebar;
        sidebarArea.min.x += offset;
        sidebarArea.max.x += offset;
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, std::max(0.0f, 1.0f - detailSlide));
        renderSidebar(sidebarArea);
        ImGui::PopStyleVar();
    }
    renderContent(content, detailMode);

    // Pencere cercevesi icerigin USTUNE cizilir (panel koselerini de sarar).
    dl->AddRect(windowPos, windowMax, withAlpha(palette().outline, 0.9f), kWindowRound, 0, 1.0f);

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

// ---------------------------------------------------------------------------
// Ust alan: title strip (sürükleme + minimize/close) ve ana bar (logo/nav/gear)
//   Not: Panel acikken ana bar, panelin kapladigi alani isgal ETMEZ; aksi halde
//   ImGui'nin "once gonderilen hover'i kilitler" kurali panel ogelerini bloklar.
// ---------------------------------------------------------------------------
void ClickGUI::renderTopBar() {
    const float deltaTime = ImGui::GetIO().DeltaTime;

    const bool panelsOpen = (inspected != nullptr) || optionsOpen_;
    const float barW = panelsOpen ? std::max(120.0f, frameSize_.x - kDockW) : frameSize_.x;

    auto dragWindow = [&]() {
        if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            draggingWindow_ = true;
        }
        if (draggingWindow_) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                const ImVec2 delta = ImGui::GetIO().MouseDelta;
                // dragWindow her karede iki kez cagrilir (title strip + ana bar);
                // delta'yi kare basina yalnizca bir kez uygula.
                const int frame = ImGui::GetFrameCount();
                if ((delta.x != 0.0f || delta.y != 0.0f) && dragApplyFrame_ != frame) {
                    dragApplyFrame_ = frame;
                    // DK-PORT: pencere serbest duran bir panel; surukleme artik
                    // yerlesik. Host istege bagli olarak onDragDelta'ya baglanip
                    // ek davranis ekleyebilir (arifHost bunu kullanmiyor).
                    framePos_.x += delta.x;
                    framePos_.y += delta.y;
                    framePlaced_ = true;
                    if (onDragDelta) {
                        onDragDelta(delta.x, delta.y);
                    }
                }
            } else {
                draggingWindow_ = false;
            }
        }
    };

    // ---- Title strip: pencere genisligi (panel seridin ALTINDAN baslar) ------
    ImGui::SetCursorScreenPos(framePos_);
    ImGui::BeginChild("##titlestrip", ImVec2(frameSize_.x, kTitleStripH), false,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                      ImGuiWindowFlags_NoScrollWithMouse);
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetWindowPos();
        const float centerY = origin.y + kTitleStripH * 0.5f;

        const float stripBtnW = 30.0f;
        const float stripBtnH = 20.0f;
        const float stripGap = 6.0f;

        struct StripDef {
            icons::UiIcon icon;
            const char* packIcon;
            bool danger;
            bool isMinimize;
        };
        const StripDef stripDefs[2] = {
            {icons::UiIcon::Minimize, "minimize", false, true},
            {icons::UiIcon::Close, "close", true, false},
        };

        float stripX = origin.x + frameSize_.x - kUnit - stripBtnW;
        for (int i = 1; i >= 0; --i) {
            const StripDef& def = stripDefs[i];
            ImGui::SetCursorPos(ImVec2(stripX - origin.x, (kTitleStripH - stripBtnH) * 0.5f));
            ImGui::PushID(400 + i);
            ImGui::InvisibleButton("##strip", ImVec2(stripBtnW, stripBtnH));
            const bool hovered = ImGui::IsItemHovered();
            const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            ImGui::PopID();

            const ImVec2 itemMin = ImGui::GetItemRectMin();
            const ImVec2 itemMax = ImGui::GetItemRectMax();
            if (hovered) {
                dl->AddRectFilled(itemMin, itemMax,
                                  def.danger ? withAlpha(palette().danger, 0.85f) : palette().panelSoft, 6.0f);
            }
            const ImU32 stripTint = (def.danger && hovered) ? IM_COL32(255, 255, 255, 255)
                                                            : (hovered ? palette().text : palette().textDim);
            const ImVec2 stripCenter((itemMin.x + itemMax.x) * 0.5f, centerY);
            if (!iconpack::drawCentered(dl, def.packIcon, stripCenter, 16.0f, stripTint)) {
                icons::drawUiIcon(dl, def.icon, stripCenter, 6.0f, stripTint);
            }

            if (clicked) {
                if (def.isMinimize) {
                    if (onMinimize) {
                        onMinimize();
                    }
                } else {
                    if (onClose) {
                        onClose();
                    }
                }
            }

            stripX -= stripBtnW + stripGap;
        }

        dragWindow();
        ImGui::EndChild();
    }

    // ---- Ana bar: nav + refresh/gear ------------------------------------------
    ImGui::SetCursorScreenPos(ImVec2(framePos_.x, framePos_.y + kTitleStripH));
    ImGui::BeginChild("##topbar", ImVec2(barW, kTopBarH), false,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                      ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    const float mainCenterY = origin.y + kTopBarH * 0.5f;

    // Nav: kutu/daire yok — ikonlar dogrudan durur; aktif sekmenin etiketi
    // yaninda genislik + alpha animasyonu ile acilir.
    struct NavDef {
        Tab tab;
        icons::UiIcon icon;
        const char* packIcon;
        const char* label;
    };
    const NavDef navDefs[3] = {
        {Tab::Home, icons::UiIcon::Home, "home", "Home"},
        {Tab::Profiles, icons::UiIcon::Profiles, "profiles", "Profiles"},
        // DK-PORT: Friends replaced the demo Scripts tab.
        {Tab::Friends, icons::UiIcon::Profiles, nullptr, "Friends"},
    };

    const float navBtn = 40.0f;   // tik hedefi (kutu cizilmez)
    float navX = origin.x + kOuter;
    for (int i = 0; i < 3; ++i) {
        const bool isActive = (activeTab == navDefs[i].tab);
        tabAnim_[i].setTarget(isActive ? 1.0f : 0.0f);
        tabAnim_[i].update(deltaTime);
        const float t = std::min(1.0f, std::max(0.0f, tabAnim_[i].value()));

        const float labelW = measure(fonts().body, navDefs[i].label).x;
        const float buttonW = navBtn + t * (labelW + kUnit);

        ImGui::SetCursorPos(ImVec2(navX - origin.x, (kTopBarH - navBtn) * 0.5f));
        ImGui::PushID(100 + i);
        ImGui::InvisibleButton("##nav", ImVec2(std::max(navBtn, buttonW), navBtn));
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            navigateTab(navDefs[i].tab);
        }
        ImGui::PopID();

        const ImVec2 itemMin = ImGui::GetItemRectMin();
        const ImVec2 itemMax = ImGui::GetItemRectMax();

        const ImU32 iconColor = (hovered || t > 0.5f) ? palette().text : palette().textDim;
        const ImVec2 navIconCenter(itemMin.x + navBtn * 0.5f, mainCenterY);
        if (!iconpack::drawCentered(dl, navDefs[i].packIcon, navIconCenter, 24.0f, iconColor)) {
            icons::drawUiIcon(dl, navDefs[i].icon, navIconCenter, 10.5f, iconColor);
        }

        // Etiket: aktif sekmede genislikle birlikte animasyonla acilir.
        if (t > 0.01f) {
            const float labelX = itemMin.x + navBtn * 0.5f + 16.0f;
            dl->PushClipRect(ImVec2(labelX - 2.0f, itemMin.y), itemMax, true);
            dl->AddText(fonts().body, fonts().body->FontSize,
                        ImVec2(labelX, mainCenterY - fonts().body->FontSize * 0.5f),
                        withAlpha(palette().text, t), navDefs[i].label);
            dl->PopClipRect();
        }

        navX += std::max(navBtn, buttonW) + kNavGap;
    }

    // Sag: refresh + gear. Panel acikken panel bu alani kaplar; kontroller
    // gizlenir (gear modul ayarlarinin ALTINDA kalir, ortaya cikarilmaz).
    if (!panelsOpen) {
        const float controlsTotal = kNavButton * 2.0f + kNavGap;
        float controlX = origin.x + barW - kOuter - controlsTotal;
        int controlIndex = 0;

        auto iconButton = [&](const char* packIcon, icons::UiIcon icon, bool isActive, float rotation) {
            ImGui::SetCursorPos(ImVec2(controlX - origin.x, (kTopBarH - kNavButton) * 0.5f));
            ImGui::PushID(200 + controlIndex);
            ImGui::InvisibleButton("##ctrl", ImVec2(kNavButton, kNavButton));
            const bool hovered = ImGui::IsItemHovered();
            const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            ImGui::PopID();
            ++controlIndex;

            const ImVec2 itemMin = ImGui::GetItemRectMin();
            const ImVec2 itemMax = ImGui::GetItemRectMax();

            ImU32 background = palette().panel;
            if (isActive) {
                background = mix(palette().panelSoft, palette().accent, 0.30f);
            } else if (hovered) {
                background = palette().panelSoft;
            }
            dl->AddRectFilled(itemMin, itemMax, background, 8.0f);
            if (isActive) {
                dl->AddRect(itemMin, itemMax, withAlpha(palette().accent, 0.45f), 8.0f, 0, 1.0f);
            }

            const ImVec2 ctrlCenter((itemMin.x + itemMax.x) * 0.5f, mainCenterY);
            const ImU32 ctrlTint = (isActive || hovered) ? palette().text : palette().textDim;
            if (!iconpack::drawRotated(dl, packIcon, ctrlCenter, 16.0f, rotation, ctrlTint)) {
                icons::drawUiIcon(dl, icon, ctrlCenter, 8.5f, ctrlTint, rotation);
            }

            controlX += kNavButton + kNavGap;
            return clicked;
        };

        if (iconButton("refresh", icons::UiIcon::Refresh, false, refreshSpin_.value() * 2.0f * kPi)) {
            refreshSpin_.play(0.7f);
        }
        if (iconButton("settings", icons::UiIcon::Gear, optionsOpen_, 0.0f)) {
            // Gear = Options overlay'ini ac/kapat; aktif sekme korunur
            // (Profiles/Scripts uzerinde de acilabilir, Home'a zorlanmaz).
            optionsOpen_ = !optionsOpen_;
        }
    }

    dragWindow();
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Sidebar: kategori listesi (sol dikey indicator) + "ARIF LITE" alt yazisi
// ---------------------------------------------------------------------------
void ClickGUI::renderSidebar(const Rect& area) {
    ImGui::SetCursorScreenPos(area.min);
    ImGui::BeginChild("##sidebar", area.size(), false,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                      ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float deltaTime = ImGui::GetIO().DeltaTime;

    // DK-PORT: 6 rows = Favorites + the client's five real categories.
    static const Category kCategories[6] = {
        Category::Favorites, Category::Combat, Category::Movement,
        Category::Render, Category::Utility, Category::Misc
    };

    const float itemH = 40.0f;
    const float itemGap = 24.0f;   // 64px ritim
    const float labelX = 28.0f;
    float y = 8.0f;

    const bool showCategories = (activeTab == Tab::Home);
    for (int i = 0; i < 6; ++i) {
        const Category category = kCategories[i];

        // Kategoriler yalnizca Home'da gorunur; diger sekmelerde hicbir sey
        // cizilmez (animasyonlar sifira cekilir, yer de kaplanmaz).
        if (!showCategories) {
            AnimationState& anim = categoryAnim_[static_cast<int>(category)];
            anim.setTarget(0.0f);
            anim.update(deltaTime);
            continue;
        }

        ImGui::SetCursorPos(ImVec2(0.0f, y));
        ImGui::PushID(i);
        ImGui::InvisibleButton("##cat", ImVec2(area.width(), itemH));
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            navigateCategory(category);
        }
        ImGui::PopID();

        const ImVec2 itemMin = ImGui::GetItemRectMin();
        const ImVec2 itemMax = ImGui::GetItemRectMax();
        const float itemCenterY = (itemMin.y + itemMax.y) * 0.5f;

        AnimationState& anim = categoryAnim_[static_cast<int>(category)];
        anim.setTarget((selectedCategory == category && activeTab == Tab::Home) ? 1.0f : 0.0f);
        anim.update(deltaTime);
        const float t = std::min(1.0f, std::max(0.0f, anim.value()));

        if (hovered || t > 0.01f) {
            const float alpha = std::max(t, hovered ? 0.45f : 0.0f);
            dl->AddRectFilled(itemMin, itemMax, withAlpha(palette().panelSoft, 0.5f * alpha), 8.0f);
        }

        // Sol dikey aktiflik gostergesi
        if (t > 0.01f) {
            dl->AddRectFilled(ImVec2(itemMin.x + 12.0f, itemCenterY - 8.0f),
                              ImVec2(itemMin.x + 14.5f, itemCenterY + 8.0f),
                              withAlpha(palette().accent, t), 1.25f);
        }

        dl->AddText(fonts().body, fonts().body->FontSize,
                    ImVec2(itemMin.x + labelX, itemCenterY - fonts().body->FontSize * 0.5f),
                    mix(palette().textDim, palette().text, t), categoryName(category));

        y += itemH + itemGap;
    }

    // "ARIF LITE" alt yazi (quick-access izgarasi kaldirildi)
    const float footerH = fonts().tiny ? fonts().tiny->FontSize : 12.0f;
    dl->AddText(fonts().tiny, footerH,
                ImVec2(area.min.x + labelX, area.min.y + area.height() - 14.0f - footerH),
                palette().textMuted, "ARIF LITE");

    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Content: Home listesi + sag panel (Options overlay / modul ayarlari)
// ---------------------------------------------------------------------------
void ClickGUI::renderContent(const Rect& area, bool detailMode) {
    // Panel acikken icerik penceresi panelin ALTINA UZANMAZ (aksi halde ImGui'nin
    // "once gonderilen hover'i kilitler" kurali panel ogelerini bloklar) ve sinir
    // panelin ANLIK sol kenarini takip eder; boylece animasyon sirasinda panelin
    // arkasinda bos/siyah bant olusmaz, liste panel kayarken kisalir.
    const ImVec2 frameMin = framePos_;
    const ImVec2 frameMax(framePos_.x + frameSize_.x, framePos_.y + frameSize_.y);
    const bool panelOpen = detailMode || optionsOpen_;
    const float slide = panelAnim_.value();
    const float panelLeft = frameMax.x - kDockW + (1.0f - slide) * (kDockW + kUnit * 3.0f);
    float contentW = area.width();
    if (panelOpen) {
        contentW = std::min(contentW, panelLeft - area.min.x - (detailMode ? kUnit : 0.0f));
        if (contentW < 60.0f) {
            contentW = 60.0f;
        }
    }

    // Icerik alani: zemini YOK (client'in duz rengi gorunur); sadece modul
    // kutularinin kendi ic rengi farklidir.
    ImGui::SetCursorScreenPos(area.min);
    ImGui::BeginChild("##content", ImVec2(contentW, area.height()), false, ImGuiWindowFlags_NoBackground);

    ImGui::Indent(kContentPad);

    const float pageWidth = area.width() - kContentPad * 2.0f;

    if (activeTab == Tab::Home) {
        ImGui::SetCursorPos(ImVec2(kContentPad, kContentPad));
        panels::modulesPage(modules, selectedCategory, pageWidth, inspected);
        if (inspected != nullptr) {
            optionsOpen_ = false;   // modul satiri secilince Options kapanir, detay acilir
        }
    } else {
        // Baslikli sayfalar: Profiles / Scripts
        const float headerH = fonts().header ? fonts().header->FontSize : 14.0f;
        trackedText(ImGui::GetWindowDrawList(), fonts().header, headerH,
                    ImVec2(area.min.x + kContentPad, area.min.y + kContentPad),
                    tabName(activeTab), palette().accent, headerH * 0.05f);

        ImGui::SetCursorPos(ImVec2(kContentPad, kContentHead));
        switch (activeTab) {
            case Tab::Profiles:
                panels::configsPage(configs, newConfigBuffer_, (int)sizeof(newConfigBuffer_));
                break;
            case Tab::Friends:
                panels::friendsPage(friends, friendSearchBuffer_, (int)sizeof(friendSearchBuffer_));
                break;
            default:
                break;
        }
    }

    ImGui::Unindent(kContentPad);
    ImGui::EndChild();

    // --- Sag panel: seridin altindan baslar, sag kenara yapisik ---------------
    if (panelOpen) {
        panelShown_ = optionsOpen_ ? nullptr : inspected;   // Options oncelikli
    }

    const float panelTop = kTitleStripH;   // title strip (— ✕) kapatilmaz; paneller altindan baslar
    const float panelTopY = framePos_.y + panelTop;

    if (slide > 0.002f) {
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Options acikken pencerenin kendi icerigine hafif siyah tint (scrim).
        // (Full-screen bir perde artik YOK; disarida oyun normal kalir.)
        if (optionsOpen_) {
            const float dim = 0.38f * slide;
            dl->AddRectFilled(ImVec2(frameMin.x, panelTopY), frameMax, IM_COL32(0, 0, 0, (int)(dim * 255.0f)));
            ImGui::SetCursorScreenPos(ImVec2(frameMin.x, panelTopY));
            ImGui::InvisibleButton("##scrim", ImVec2(frameSize_.x, frameSize_.y - panelTop));
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                optionsOpen_ = false;
            }
        }

        const float panelW = kDockW;
        const float x = frameMax.x - panelW + (1.0f - slide) * (panelW + kUnit * 3.0f);
        ImGui::SetCursorScreenPos(ImVec2(x, panelTopY));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, slide);
        const bool closeRequested = panels::dockPanel(panelShown_, ImVec2(panelW, frameSize_.y - panelTop));
        ImGui::PopStyleVar();

        if (closeRequested) {
            if (optionsOpen_) {
                optionsOpen_ = false;
            } else if (inspected != nullptr) {
                inspected = nullptr;
            }
        }
    }
    if (!panelOpen && slide <= 0.002f) {
        panelShown_ = nullptr;
    }
}

} // namespace arif::gui

