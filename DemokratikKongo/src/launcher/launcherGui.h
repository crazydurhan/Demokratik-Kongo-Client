#pragma once

#include "injector.h"
#include "logger.h"
#include "process_scanner.h"

#include "imgui.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace dk {

enum class StatusKind : uint8_t
{
    Neutral,
    Ready,
    Busy,
    Success,
    Warning,
    Error,
};

// Fonts are rebuilt whenever the DPI changes; the GUI only ever reads the
// current pointers, never caches metrics derived from them.
struct LauncherFonts
{
    ImFont* regular = nullptr; // 15px  Inter Medium
    ImFont* caption = nullptr; // 12px  Inter Medium (named to dodge the Windows `small` macro)
    ImFont* bold = nullptr;    // 15px  Inter Bold
    ImFont* title = nullptr;   // 22px  Inter Bold
    ImFont* mono = nullptr;    // 13px  JetBrains Mono
    ImFont* icon = nullptr;    // 15px  Font Awesome 6 Solid
};

struct Toast
{
    std::string text;
    ImU32 color = 0;
    std::chrono::steady_clock::time_point born;
    float anim = 0.0f; // eased 0..1 presence
};

class LauncherGui {
public:
    // Layout constants in unscaled pixels. LauncherApp sizes the OS window
    // from these times the DPI scale; the log panel grows it by
    // kLogPanelHeight while open.
    static constexpr float kBaseWidth = 560.0f;
    static constexpr float kBaseHeight = 420.0f;
    static constexpr float kLogPanelHeight = 240.0f;

    void init(HWND hwnd, const LauncherFonts& fonts, float dpiScale);
    void shutdown();

    // Runs before ImGui::NewFrame: animates the OS window size so the
    // swap chain is resized in the same iteration that draws the new size.
    void preFrame();
    void render();

    // Called by LauncherApp after a WM_DPICHANGED font rebuild.
    void onDpiChanged(const LauncherFonts& fonts, float dpiScale);

    void setStatus(const std::string& text, StatusKind kind = StatusKind::Neutral);

    // Scaled metrics for the Win32 hit-test: the custom titlebar drags the
    // window while the caption buttons stay clickable.
    float titlebarHeight() const { return s(kTitlebarBase); }
    float captionButtonsWidth() const;

private:
    static constexpr float kTitlebarBase = 44.0f;
    static constexpr float kPadBase = 20.0f;

    // ------------------------------------------------------------ logic
    void refreshProcesses();
    void injectSelected();
    int pickBestProcess() const;
    void pushToast(const std::string& text, ImU32 color);
    void copyLogToClipboard();
    void applyStyle();

    // ------------------------------------------------------------ drawing
    // Each section draws at the given top y and returns the y below itself.
    void drawTitlebar();
    float drawStatusRow(float y);
    float drawHero(float y);
    float drawProcessCard(float y);
    void drawProcessPopup(ImVec2 anchor, float width);
    float drawInjectButton(float y);
    float drawProgress(float y);
    void drawLogPanel(float top, float bottom);
    void drawBottomBar(float top);
    void drawToasts();

    // Custom-drawn widgets. Animation state lives in ImGui::GetStateStorage()
    // keyed by the widget's ImGuiID.
    bool iconButton(const char* id, unsigned int glyph, ImVec2 pos, ImVec2 size,
                    ImU32 hoverFill, ImU32 hoverGlyph, float spin = 0.0f);
    bool textButton(const char* id, const char* label, ImVec2 pos, bool accent = false);
    bool toggleSwitch(const char* id, ImVec2 pos, bool* value);
    ImVec2 badgeSize(const char* label) const;
    void badge(ImDrawList* dl, ImVec2 pos, const char* label, ImU32 color, float alpha = 1.0f) const;

    // Scale helper: unscaled px -> device px.
    float s(float v) const { return v * scale_; }

    ImU32 statusColor(StatusKind kind) const;
    ImU32 launcherBadgeColor(LauncherKind kind) const;

    // ------------------------------------------------------------ state
    ProcessScanner scanner_;
    Injector injector_;
    std::vector<McProcess> processes_;
    int selectedIndex_ = -1;
    bool manualSelect_ = false;
    std::wstring payloadPath_;
    DWORD embeddedPayloadBytes_ = 0;
    bool pendingRefresh_ = true;
    bool autoScrollLog_ = true;
    bool logOpen_ = false;
    float logAnim_ = 0.0f;
    float refreshSpin_ = 0.0f; // 1 right after a refresh, eases back to 0

    HWND hwnd_ = nullptr;
    float scale_ = 1.0f;
    LauncherFonts fonts_;

    // Per-frame layout (screen space), set at the top of render().
    ImVec2 origin_;
    float width_ = 0.0f;
    float height_ = 0.0f;

    std::thread injectThread_;
    std::atomic<bool> injecting_{ false };
    std::mutex injectMutex_;
    int progressStep_ = 0;
    int progressTotal_ = 1;
    std::string progressLabel_;

    mutable std::mutex statusMutex_;
    std::string status_;
    StatusKind statusKind_ = StatusKind::Neutral;

    std::vector<Toast> toasts_;
    std::mutex toastMutex_;
};

} // namespace dk
