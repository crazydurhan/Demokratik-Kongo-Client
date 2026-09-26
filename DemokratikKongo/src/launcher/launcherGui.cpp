#include "launcherGui.h"
#include "resourceLoader.h"
#include "util.h"

#include "version.h"
#include "ui/uiMotion.h"
#include "ui/uiPalette.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

/*
========================================================================
    RuntimeHost launcher GUI
------------------------------------------------------------------------
    Everything is custom-drawn through ImDrawList so the launcher looks
    like the in-game ClickGUI. Stock ImGui is only used for hit-testing
    (InvisibleButton), the process popup shell, and the scrolling log
    child. All motion is frame-rate independent (dk::ui::Ease over
    io.DeltaTime) and per-widget state lives in ImGui::GetStateStorage()
    keyed by the widget's ImGuiID.
========================================================================
*/

namespace dk {

namespace pal = dk::ui::palette;
using dk::ui::Clamp01;
using dk::ui::Ease;
using dk::ui::SmoothStep;

namespace {

constexpr float kToastLifetime = 3.5f;
constexpr float kPi = 3.14159265358979323846f;

// Font Awesome 6 Solid codepoints present in the embedded subset.
constexpr unsigned int kGlyphMinus = 0xF068;
constexpr unsigned int kGlyphClose = 0xF00D;
constexpr unsigned int kGlyphCube = 0xF1B2;
constexpr unsigned int kGlyphRefresh = 0xF2F1;
constexpr unsigned int kGlyphChevronDown = 0xF078;
constexpr unsigned int kGlyphBolt = 0xF0E7;
constexpr unsigned int kGlyphCheck = 0xF00C;
constexpr unsigned int kGlyphTerminal = 0xF120;

// ------------------------------------------------------------ anim state

float& stateFloat(ImGuiID id, float def = 0.0f)
{
    return *ImGui::GetStateStorage()->GetFloatRef(id, def);
}

// Eases the stored value towards target and returns it.
float easeState(ImGuiID id, float target, float speed, float def = 0.0f)
{
    float& v = stateFloat(id, def);
    Ease(v, target, speed);
    if (std::fabs(v - target) < 0.0005f)
        v = target;
    return v;
}

ImGuiID subId(ImGuiID base, const char* tag)
{
    return ImHashStr(tag, 0, base);
}

// ------------------------------------------------------------ colours

ImU32 lerpCol(ImU32 a, ImU32 b, float t)
{
    return ImGui::ColorConvertFloat4ToU32(ImLerp(pal::ToVec4(a), pal::ToVec4(b), Clamp01(t)));
}

ImU32 vecCol(const ImVec4& v, float alpha = 1.0f)
{
    return ImGui::ColorConvertFloat4ToU32(ImVec4(v.x, v.y, v.z, v.w * Clamp01(alpha)));
}

// ------------------------------------------------------------ text

ImFont* safeFont(ImFont* font)
{
    return (font && font->IsLoaded()) ? font : ImGui::GetFont();
}

ImVec2 textSize(ImFont* font, const char* text)
{
    font = safeFont(font);
    return font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, text);
}

void text(ImDrawList* dl, ImFont* font, ImVec2 pos, ImU32 col, const char* str)
{
    font = safeFont(font);
    dl->AddText(font, font->FontSize, pos, col, str);
}

// align (0,0)=top-left, (0.5,0.5)=centre, (1,0.5)=right-middle.
void textIn(ImDrawList* dl, ImFont* font, ImVec2 min, ImVec2 max, ImU32 col, const char* str,
            ImVec2 align = ImVec2(0.5f, 0.5f))
{
    const ImVec2 size = textSize(font, str);
    const ImVec2 pos(min.x + (max.x - min.x - size.x) * align.x,
                     min.y + (max.y - min.y - size.y) * align.y);
    text(dl, font, ImVec2(std::floor(pos.x), std::floor(pos.y)), col, str);
}

void textEllipsis(ImDrawList* dl, ImFont* font, ImVec2 pos, float maxWidth, ImU32 col, const char* str)
{
    font = safeFont(font);
    const float full = textSize(font, str).x;
    if (full <= maxWidth) {
        text(dl, font, pos, col, str);
        return;
    }
    const float dotsW = textSize(font, "...").x;
    const char* end = str;
    float w = 0.0f;
    while (*end) {
        unsigned int c = 0;
        const int len = ImTextCharFromUtf8(&c, end, nullptr);
        const float cw = font->GetCharAdvance(static_cast<ImWchar>(c));
        if (w + cw + dotsW > maxWidth)
            break;
        w += cw;
        end += len > 0 ? len : 1;
    }
    char buf[256];
    const size_t n = std::min<size_t>(static_cast<size_t>(end - str), sizeof(buf) - 4);
    memcpy(buf, str, n);
    memcpy(buf + n, "...", 4);
    text(dl, font, pos, col, buf);
}

// Draws one icon-font glyph centred on `center`, optionally rotated. The
// rotation works on the emitted vertices, which is the only way to spin
// text through ImDrawList.
void glyph(ImDrawList* dl, ImFont* font, unsigned int cp, ImVec2 center, ImU32 col,
           float angle = 0.0f, float scale = 1.0f)
{
    font = safeFont(font);
    char buf[5];
    ImTextCharToUtf8(buf, cp);
    const float size = font->FontSize * scale;
    const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, buf);
    const ImVec2 pos(center.x - sz.x * 0.5f, center.y - sz.y * 0.5f);

    const int start = dl->VtxBuffer.Size;
    dl->AddText(font, size, pos, col, buf);
    if (angle != 0.0f) {
        const float c = std::cos(angle), s = std::sin(angle);
        for (int i = start; i < dl->VtxBuffer.Size; ++i) {
            ImVec2& p = dl->VtxBuffer[i].pos;
            const ImVec2 d(p.x - center.x, p.y - center.y);
            p = ImVec2(center.x + d.x * c - d.y * s, center.y + d.x * s + d.y * c);
        }
    }
}

} // namespace

// ============================================================ lifecycle

void LauncherGui::init(HWND hwnd, const LauncherFonts& fonts, float dpiScale)
{
    hwnd_ = hwnd;
    fonts_ = fonts;
    scale_ = dpiScale > 0.0f ? dpiScale : 1.0f;

    applyStyle();

    auto& log = LauncherLog::I();
    log.info("RuntimeHost launcher v" DK_VERSION_SHORT_STR " - embedded core + optional sibling RuntimeHostCore.dll");

    embeddedPayloadBytes_ = embeddedPayloadSize();
    if (embeddedPayloadBytes_ == 0) {
        log.warn("No embedded core in EXE - will use RuntimeHostCore.dll if placed next to launcher.");
    } else {
        log.info("Embedded core: " + std::to_string(embeddedPayloadBytes_) + " bytes inside EXE");
    }

    payloadPath_ = ensureEmbeddedPayload();
    if (payloadPath_.empty()) {
        setStatus("Core missing", StatusKind::Warning);
    } else {
        log.debug("Payload: " + wideToUtf8(payloadPath_));
        setStatus("Ready", StatusKind::Ready);
    }
}

void LauncherGui::shutdown()
{
    if (injectThread_.joinable())
        injectThread_.join();
}

void LauncherGui::onDpiChanged(const LauncherFonts& fonts, float dpiScale)
{
    fonts_ = fonts;
    scale_ = dpiScale > 0.0f ? dpiScale : 1.0f;
    applyStyle();
}

float LauncherGui::captionButtonsWidth() const
{
    // Two 36px buttons, 6px gap, right padding.
    return s(36.0f * 2.0f + 6.0f + kPadBase);
}

void LauncherGui::applyStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle(); // reset before scaling so repeated calls do not compound
    ImVec4* colors = style.Colors;

    style.WindowRounding = 0.0f;
    style.ChildRounding = s(10.0f);
    style.FrameRounding = s(8.0f);
    style.GrabRounding = s(8.0f);
    style.PopupRounding = s(10.0f);
    style.ScrollbarRounding = s(6.0f);
    style.WindowPadding = ImVec2(0.0f, 0.0f);
    style.FramePadding = ImVec2(s(12.0f), s(8.0f));
    style.ItemSpacing = ImVec2(s(8.0f), s(6.0f));
    style.ItemInnerSpacing = ImVec2(s(8.0f), s(6.0f));
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.ScrollbarSize = s(10.0f);
    style.AntiAliasedLines = true;
    style.AntiAliasedFill = true;

    colors[ImGuiCol_Text] = pal::ToVec4(pal::kText);
    colors[ImGuiCol_TextDisabled] = pal::ToVec4(pal::kTextDim);
    colors[ImGuiCol_WindowBg] = pal::ToVec4(pal::kBackground);
    colors[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_PopupBg] = pal::ToVec4(pal::kPanel);
    colors[ImGuiCol_Border] = pal::ToVec4(pal::kBorder);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_FrameBg] = pal::ToVec4(pal::kWidget);
    colors[ImGuiCol_FrameBgHovered] = pal::ToVec4(pal::kWidgetHover);
    colors[ImGuiCol_FrameBgActive] = pal::ToVec4(pal::kWidgetHover);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_ScrollbarGrab] = pal::ToVec4(pal::kBorder);
    colors[ImGuiCol_ScrollbarGrabHovered] = pal::ToVec4(pal::kTextMute);
    colors[ImGuiCol_ScrollbarGrabActive] = pal::ToVec4(pal::kTextDim);
    colors[ImGuiCol_TextSelectedBg] = pal::ToVec4(pal::kAccentSoft);
    colors[ImGuiCol_NavHighlight] = pal::ToVec4(pal::kAccent);
}

// ============================================================ logic

ImU32 LauncherGui::launcherBadgeColor(LauncherKind kind) const
{
    switch (kind) {
    case LauncherKind::Lunar:    return IM_COL32(140, 166, 255, 255);
    case LauncherKind::Badlion:  return IM_COL32(255, 140, 51, 255);
    case LauncherKind::Forge:    return IM_COL32(217, 115, 64, 255);
    case LauncherKind::Fabric:   return IM_COL32(191, 140, 242, 255);
    case LauncherKind::OptiFine: return IM_COL32(89, 217, 140, 255);
    case LauncherKind::Vanilla:  return IM_COL32(115, 217, 115, 255);
    case LauncherKind::Custom:   return IM_COL32(242, 191, 89, 255);
    default:                     return pal::kTextDim;
    }
}

ImU32 LauncherGui::statusColor(StatusKind kind) const
{
    switch (kind) {
    case StatusKind::Ready:   return pal::kGood;
    case StatusKind::Busy:    return pal::kWarning;
    case StatusKind::Success: return pal::kGood;
    case StatusKind::Warning: return pal::kWarning;
    case StatusKind::Error:   return pal::kDanger;
    default:                  return pal::kTextDim;
    }
}

void LauncherGui::setStatus(const std::string& text, StatusKind kind)
{
    std::lock_guard lock(statusMutex_);
    status_ = text;
    statusKind_ = kind;
}

void LauncherGui::pushToast(const std::string& text, ImU32 color)
{
    std::lock_guard lock(toastMutex_);
    toasts_.push_back({ text, color, std::chrono::steady_clock::now(), 0.0f });
}

void LauncherGui::copyLogToClipboard()
{
    std::ostringstream oss;
    LauncherLog::I().withEntries([&](const std::vector<LogEntry>& entries) {
        for (const LogEntry& e : entries) {
            oss << '[' << e.timestamp << "] [" << LauncherLog::levelLabel(e.level) << "] "
                << e.message << '\n';
        }
    });
    const std::string text = oss.str();
    if (text.empty())
        return;

    if (!OpenClipboard(nullptr))
        return;

    bool ok = false;
    const SIZE_T bytes = (text.size() + 1) * sizeof(char);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem) {
        void* ptr = GlobalLock(mem);
        if (ptr) {
            memcpy(ptr, text.c_str(), bytes);
            GlobalUnlock(mem);
            // Only wipe the previous clipboard content once we have data
            // ready to set; otherwise a failed prep would destroy it.
            EmptyClipboard();
            ok = SetClipboardData(CF_TEXT, mem) != nullptr;
        }
        if (!ok)
            GlobalFree(mem);
    }
    CloseClipboard();

    if (ok) {
        LauncherLog::I().info("Log copied to clipboard.");
        pushToast("Log copied", pal::kAccent);
    }
}

int LauncherGui::pickBestProcess() const
{
    if (processes_.empty())
        return -1;

    // Prefer a process that actually has a JVM + LWJGL loaded, else the first.
    for (size_t i = 0; i < processes_.size(); ++i) {
        if (processes_[i].hasJvm && processes_[i].hasLwjgl)
            return static_cast<int>(i);
    }
    for (size_t i = 0; i < processes_.size(); ++i) {
        if (processes_[i].hasJvm)
            return static_cast<int>(i);
    }
    return 0;
}

void LauncherGui::refreshProcesses()
{
    auto& log = LauncherLog::I();
    log.info("Scanning for Minecraft / JVM processes...");

    processes_ = scanner_.scan();
    refreshSpin_ = 1.0f;

    if (!manualSelect_ || processes_.empty())
        selectedIndex_ = pickBestProcess();
    else if (selectedIndex_ >= static_cast<int>(processes_.size()))
        selectedIndex_ = pickBestProcess();

    if (processes_.empty()) {
        log.warn("No Minecraft processes found. Launch the game first, then Refresh.");
    } else {
        log.ok("Found " + std::to_string(processes_.size()) + " candidate process(es):");
        for (size_t i = 0; i < processes_.size(); ++i) {
            const McProcess& p = processes_[i];
            std::ostringstream line;
            line << "  [" << i << "] PID " << p.pid
                 << " | " << wideToUtf8(p.exeName)
                 << " | " << scanner_.launcherName(p.launcher)
                 << " | " << (p.x64 ? "x64" : "x86")
                 << " | JVM=" << (p.hasJvm ? "yes" : "no")
                 << " LWJGL=" << (p.hasLwjgl ? "yes" : "no");
            const std::string title = wideToUtf8(p.windowTitle);
            if (!title.empty())
                line << " | \"" << title << '"';
            log.debug(line.str());
        }
    }

    setStatus("Found " + std::to_string(processes_.size()) + " process(es)",
              processes_.empty() ? StatusKind::Warning : StatusKind::Ready);
}

void LauncherGui::injectSelected()
{
    if (injecting_)
        return;

    // The previous worker has finished (injecting_ is false) but is still
    // joinable; reassigning a joinable std::thread would std::terminate.
    if (injectThread_.joinable())
        injectThread_.join();

    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(processes_.size())) {
        setStatus("No target", StatusKind::Warning);
        pushToast("No Minecraft process found", pal::kWarning);
        return;
    }

    if (payloadPath_.empty()) {
        payloadPath_ = forceExtractEmbeddedPayload();
        if (payloadPath_.empty()) {
            setStatus("Core extract failed", StatusKind::Error);
            pushToast("Failed to extract core", pal::kDanger);
            return;
        }
    } else {
        payloadPath_ = ensureEmbeddedPayload();
        if (payloadPath_.empty()) {
            setStatus("Core sync failed", StatusKind::Error);
            pushToast("Core sync failed", pal::kDanger);
            return;
        }
    }

    const McProcess target = processes_[static_cast<size_t>(selectedIndex_)];
    const std::wstring payload = payloadPath_;
    const DWORD pid = target.pid;

    {
        std::lock_guard lock(injectMutex_);
        progressStep_ = 0;
        progressTotal_ = 9;
        progressLabel_ = "Preparing...";
    }
    injecting_ = true;
    setStatus("Injecting...", StatusKind::Busy);

    LauncherLog::I().info("Target: PID " + std::to_string(pid) +
                          " (" + wideToUtf8(target.exeName) + ", " +
                          scanner_.launcherName(target.launcher) + ", " +
                          (target.x64 ? "x64" : "x86") + ")");

    injectThread_ = std::thread([this, pid, payload]() {
        InjectionResult result = injector_.inject(pid, payload, [this](int step, int total, const char* label) {
            std::lock_guard lock(injectMutex_);
            progressStep_ = step;
            progressTotal_ = total;
            progressLabel_ = label ? label : "";
        });

        if (result.ok) {
            setStatus("Injection successful", StatusKind::Success);
            pushToast("Injected into PID " + std::to_string(pid), pal::kGood);
        } else {
            setStatus("Injection failed", StatusKind::Error);
            pushToast(std::string("Injection failed: ") + injectStepName(result.failedStep), pal::kDanger);
        }
        injecting_ = false;
    });
}

// ============================================================ widgets

bool LauncherGui::iconButton(const char* id, unsigned int cp, ImVec2 pos, ImVec2 size,
                             ImU32 hoverFill, ImU32 hoverGlyph, float spin)
{
    ImGui::SetCursorScreenPos(pos);
    const bool clicked = ImGui::InvisibleButton(id, size);
    const ImGuiID wid = ImGui::GetItemID();
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    const float hover = easeState(subId(wid, "hover"), hovered ? 1.0f : 0.0f, 14.0f);
    const float press = easeState(subId(wid, "press"), active ? 1.0f : 0.0f, 22.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + size.x, pos.y + size.y);
    if (hover > 0.01f)
        dl->AddRectFilled(pos, max, pal::Alpha(hoverFill, hover), s(8.0f));

    const ImU32 col = lerpCol(pal::kTextDim, hoverGlyph, hover);
    const ImVec2 center((pos.x + max.x) * 0.5f, (pos.y + max.y) * 0.5f);
    glyph(dl, fonts_.icon, cp, center, col, spin * 2.0f * kPi, 1.0f - press * 0.12f);
    return clicked;
}

bool LauncherGui::textButton(const char* id, const char* label, ImVec2 pos, bool accent)
{
    const ImVec2 ts = textSize(fonts_.caption, label);
    const ImVec2 size(ts.x + s(16.0f), s(22.0f));

    ImGui::SetCursorScreenPos(pos);
    const bool clicked = ImGui::InvisibleButton(id, size);
    const ImGuiID wid = ImGui::GetItemID();
    const float hover = easeState(subId(wid, "hover"), ImGui::IsItemHovered() ? 1.0f : 0.0f, 14.0f);
    const float press = easeState(subId(wid, "press"), ImGui::IsItemActive() ? 1.0f : 0.0f, 22.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + size.x, pos.y + size.y);
    const ImU32 fill = accent ? pal::kAccentSoft : pal::kWidgetHover;
    dl->AddRectFilled(pos, max, pal::Alpha(fill, 0.35f + hover * 0.65f - press * 0.2f), s(6.0f));
    const ImU32 col = lerpCol(accent ? pal::kAccentGlow : pal::kTextDim, pal::kText, hover);
    textIn(dl, fonts_.caption, pos, max, col, label);
    return clicked;
}

bool LauncherGui::toggleSwitch(const char* id, ImVec2 pos, bool* value)
{
    const ImVec2 size(s(36.0f), s(20.0f));
    ImGui::SetCursorScreenPos(pos);
    const bool clicked = ImGui::InvisibleButton(id, size);
    if (clicked)
        *value = !*value;

    const ImGuiID wid = ImGui::GetItemID();
    const float target = *value ? 1.0f : 0.0f;
    const float on = easeState(subId(wid, "on"), target, 15.0f, target);
    const float hover = easeState(subId(wid, "hover"), ImGui::IsItemHovered() ? 1.0f : 0.0f, 13.0f);

    // Spring knob: position and velocity persist across frames in the state
    // storage so the overshoot survives without any static variables.
    float& kx = stateFloat(subId(wid, "kx"), target);
    float& kv = stateFloat(subId(wid, "kv"), 0.0f);
    dk::ui::Spring spring = dk::ui::Spring::Make(dk::ui::SpringStyle::Bouncy, kx);
    spring.v = kv;
    spring.Drive(target);
    kx = spring.x;
    kv = spring.v;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + size.x, pos.y + size.y);
    const float radius = size.y * 0.5f;
    dl->AddRectFilled(pos, max, pal::kWidget, radius);
    if (hover > 0.01f)
        dl->AddRectFilled(pos, max, pal::Alpha(pal::kWidgetHover, hover), radius);
    if (on > 0.01f)
        dl->AddRectFilled(pos, max, pal::Alpha(pal::kAccent, on), radius);
    dl->AddRect(pos, max, pal::Alpha(pal::kBorder, 0.9f - on * 0.5f), radius, 0, 1.0f);

    const float travelMin = pos.x + radius;
    const float travelMax = max.x - radius;
    const float t = Clamp01(kx);
    const float knobX = travelMin + (travelMax - travelMin) * t;
    const float knobY = pos.y + radius;
    const float squash = std::min(std::fabs(kv) * 0.045f, 0.32f);
    const float base = radius - s(3.0f);
    const float hw = base * (1.0f + squash);
    const float hh = base * (1.0f - squash * 0.55f);
    dl->AddRectFilled(ImVec2(knobX - hw, knobY - hh), ImVec2(knobX + hw, knobY + hh),
                      IM_COL32(250, 250, 253, 255), std::min(hw, hh));
    return clicked;
}

ImVec2 LauncherGui::badgeSize(const char* label) const
{
    const ImVec2 ts = textSize(fonts_.caption, label);
    return ImVec2(ts.x + s(12.0f), s(18.0f));
}

void LauncherGui::badge(ImDrawList* dl, ImVec2 pos, const char* label, ImU32 color, float alpha) const
{
    const ImVec2 size = badgeSize(label);
    const ImVec2 max(pos.x + size.x, pos.y + size.y);
    dl->AddRectFilled(pos, max, pal::Alpha(color, 0.16f * alpha), s(5.0f));
    dl->AddRect(pos, max, pal::Alpha(color, 0.45f * alpha), s(5.0f), 0, 1.0f);
    textIn(dl, fonts_.caption, pos, max, pal::Alpha(color, alpha), label);
}

// ============================================================ sections

void LauncherGui::drawTitlebar()
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = titlebarHeight();
    const ImVec2 min = origin_;
    const ImVec2 max(origin_.x + width_, origin_.y + h);

    dl->AddRectFilled(min, max, pal::kBackground);
    dl->AddLine(ImVec2(min.x, max.y - 0.5f), ImVec2(max.x, max.y - 0.5f), pal::kLine, 1.0f);

    // Brand: accent square + wordmark + "launcher" + version pill.
    float x = min.x + s(kPadBase);
    const float cy = (min.y + max.y) * 0.5f;

    const float mark = s(10.0f);
    dl->AddRectFilled(ImVec2(x, cy - mark * 0.5f), ImVec2(x + mark, cy + mark * 0.5f), pal::kAccent, s(3.0f));
    dl->AddRectFilled(ImVec2(x + mark * 0.5f, cy - mark * 0.5f), ImVec2(x + mark, cy), pal::kAccentGlow, s(2.0f));
    x += mark + s(10.0f);

    const char* brand = "RUNTIMEHOST";
    const ImVec2 brandSize = textSize(fonts_.bold, brand);
    text(dl, fonts_.bold, ImVec2(x, std::floor(cy - brandSize.y * 0.5f)), pal::kText, brand);
    x += brandSize.x + s(8.0f);

    const char* sub = "launcher";
    const ImVec2 subSize = textSize(fonts_.regular, sub);
    text(dl, fonts_.regular, ImVec2(x, std::floor(cy - subSize.y * 0.5f)), pal::kTextMute, sub);
    x += subSize.x + s(10.0f);

    const char* ver = "v" DK_VERSION_SHORT_STR;
    const ImVec2 verSize = textSize(fonts_.mono, ver);
    const ImVec2 pillMin(x, cy - s(9.0f));
    const ImVec2 pillMax(x + verSize.x + s(12.0f), cy + s(9.0f));
    dl->AddRectFilled(pillMin, pillMax, pal::kWidget, s(5.0f));
    dl->AddRect(pillMin, pillMax, pal::kBorder, s(5.0f), 0, 1.0f);
    textIn(dl, fonts_.mono, pillMin, pillMax, pal::kTextDim, ver);

    // Caption buttons.
    const ImVec2 btnSize(s(36.0f), s(28.0f));
    const float btnY = cy - btnSize.y * 0.5f;
    const float closeX = max.x - s(kPadBase) - btnSize.x;
    const float minX = closeX - s(6.0f) - btnSize.x;

    if (iconButton("##minimize", kGlyphMinus, ImVec2(minX, btnY), btnSize, pal::kWidgetHover, pal::kText))
        ShowWindow(hwnd_, SW_MINIMIZE);
    if (iconButton("##close", kGlyphClose, ImVec2(closeX, btnY), btnSize, pal::kDanger, pal::kText))
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
}

float LauncherGui::drawStatusRow(float y)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float x = origin_.x + s(kPadBase);
    const float right = origin_.x + width_ - s(kPadBase);
    const float h = s(28.0f);

    std::string statusText;
    StatusKind kind;
    {
        std::lock_guard lock(statusMutex_);
        statusText = status_;
        kind = statusKind_;
    }

    // Colour eases between kinds; the dot pulses while busy.
    const ImGuiID id = ImGui::GetID("##status");
    const ImVec4 target = pal::ToVec4(statusColor(kind));
    float& cr = stateFloat(subId(id, "r"), target.x);
    float& cg = stateFloat(subId(id, "g"), target.y);
    float& cb = stateFloat(subId(id, "b"), target.z);
    ImVec4 cur(cr, cg, cb, 1.0f);
    Ease(cur, target, 10.0f);
    cr = cur.x; cg = cur.y; cb = cur.z;

    const bool busy = kind == StatusKind::Busy || injecting_;
    const float pulse = busy ? 0.55f + 0.45f * (0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 5.0f)) : 1.0f;
    const ImU32 dotCol = vecCol(cur, pulse);

    const ImVec2 ts = textSize(fonts_.regular, statusText.c_str());
    const ImVec2 pillMin(x, y);
    const ImVec2 pillMax(x + s(40.0f) + ts.x, y + h);
    dl->AddRectFilled(pillMin, pillMax, pal::kPanel, h * 0.5f);
    dl->AddRect(pillMin, pillMax, pal::kBorder, h * 0.5f, 0, 1.0f);

    const ImVec2 dotC(pillMin.x + s(14.0f), y + h * 0.5f);
    if (busy)
        dl->AddCircleFilled(dotC, s(7.0f), vecCol(cur, 0.18f * pulse));
    dl->AddCircleFilled(dotC, s(4.0f), dotCol);
    text(dl, fonts_.regular, ImVec2(dotC.x + s(12.0f), std::floor(y + (h - ts.y) * 0.5f)), pal::kText, statusText.c_str());

    // Refresh button on the right; spins once after every scan.
    Ease(refreshSpin_, 0.0f, 6.0f);
    if (refreshSpin_ < 0.002f)
        refreshSpin_ = 0.0f;
    const ImVec2 btnSize(h, h);
    if (iconButton("##refresh", kGlyphRefresh, ImVec2(right - btnSize.x, y), btnSize, pal::kWidgetHover, pal::kText,
                   1.0f - refreshSpin_))
        pendingRefresh_ = true;

    return y + h;
}

// Democratic Republic of the Congo flag as a clipped, rounded hero banner:
// sky-blue field, yellow-fimbriated red diagonal, yellow star; overlaid with
// a soft bottom gradient so the caption stays readable.
float LauncherGui::drawHero(float y)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float x = origin_.x + s(kPadBase);
    const float w = width_ - s(kPadBase) * 2.0f;
    const float h = s(96.0f);
    const ImVec2 p(x, y);
    const ImVec2 q(x + w, y + h);
    const float r = s(12.0f);

    const ImU32 blue = IM_COL32(8, 128, 217, 255);
    const ImU32 yellow = IM_COL32(247, 209, 41, 255);
    const ImU32 red = IM_COL32(217, 26, 31, 255);

    dl->AddRectFilled(p, q, blue, r);
    dl->PushClipRect(p, q, true);

    const ImVec2 a(p.x, q.y);
    const ImVec2 b(q.x, p.y);
    const float bandW = s(30.0f);
    ImVec2 dir(b.x - a.x, b.y - a.y);
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    dir.x /= len; dir.y /= len;
    const ImVec2 nrm(-dir.y, dir.x);
    const ImVec2 off1(dir.x * bandW, dir.y * bandW);
    const ImVec2 off2(nrm.x * bandW, nrm.y * bandW);

    auto band = [&](float k, ImU32 col) {
        ImVec2 pts[4] = {
            ImVec2(a.x - off1.x + off2.x * k, a.y - off1.y + off2.y * k),
            ImVec2(a.x - off1.x - off2.x * k, a.y - off1.y - off2.y * k),
            ImVec2(b.x + off1.x - off2.x * k, b.y + off1.y - off2.y * k),
            ImVec2(b.x + off1.x + off2.x * k, b.y + off1.y + off2.y * k),
        };
        dl->AddConvexPolyFilled(pts, 4, col);
    };
    band(1.0f, yellow);
    band(0.55f, red);

    // Star with a slow breathing glow.
    const float t = static_cast<float>(ImGui::GetTime());
    const float glow = 0.5f + 0.5f * std::sin(t * 1.6f);
    const ImVec2 c(p.x + w * 0.16f, p.y + h * 0.36f);
    const float R = h * 0.19f;
    dl->AddCircleFilled(c, R * (1.9f + glow * 0.4f), pal::Alpha(yellow, 0.10f + glow * 0.10f), 32);
    ImVec2 star[10];
    for (int i = 0; i < 10; ++i) {
        const float ang = -kPi * 0.5f + static_cast<float>(i) * kPi * 0.2f;
        const float rad = (i % 2 == 0) ? R : R * 0.45f;
        star[i] = ImVec2(c.x + std::cos(ang) * rad, c.y + std::sin(ang) * rad);
    }
    dl->AddConcavePolyFilled(star, 10, yellow);

    // Bottom gradient + caption.
    dl->AddRectFilledMultiColor(ImVec2(p.x, p.y + h * 0.35f), q,
                                IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0),
                                IM_COL32(8, 8, 12, 190), IM_COL32(8, 8, 12, 190));
    const char* title = "RuntimeHost";
    const char* caption = "Minecraft 1.8.9  -  injectable client";
    const ImVec2 titleSize = textSize(fonts_.title, title);
    text(dl, fonts_.title, ImVec2(p.x + s(16.0f) + 1.0f, q.y - s(14.0f) - titleSize.y - s(14.0f) + 1.0f),
         IM_COL32(0, 0, 0, 120), title);
    text(dl, fonts_.title, ImVec2(p.x + s(16.0f), q.y - s(14.0f) - titleSize.y - s(14.0f)), pal::kText, title);
    text(dl, fonts_.caption, ImVec2(p.x + s(16.0f), q.y - s(14.0f) - s(12.0f)), pal::Alpha(pal::kText, 0.7f), caption);

    dl->PopClipRect();

    // ImDrawList clips to rectangles only, so mask the square corners left by
    // the clipped polygons: a thick stroke in the background colour drawn on a
    // rect grown by half its thickness has an inner edge of exactly radius r.
    const float mask = s(8.0f);
    dl->AddRect(ImVec2(p.x - mask * 0.5f, p.y - mask * 0.5f), ImVec2(q.x + mask * 0.5f, q.y + mask * 0.5f),
                pal::kBackground, r + mask * 0.5f, 0, mask);
    dl->AddRect(p, q, pal::Alpha(IM_COL32(255, 255, 255, 255), 0.06f), r, 0, 1.0f);
    return y + h;
}

float LauncherGui::drawProcessCard(float y)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float x = origin_.x + s(kPadBase);
    const float w = width_ - s(kPadBase) * 2.0f;

    text(dl, fonts_.caption, ImVec2(x, y), pal::kTextMute, "TARGET");
    y += s(18.0f);

    const float h = s(58.0f);
    const ImVec2 min(x, y);
    const ImVec2 max(x + w, y + h);

    ImGui::SetCursorScreenPos(min);
    const bool clicked = ImGui::InvisibleButton("##targetcard", ImVec2(w, h));
    const ImGuiID wid = ImGui::GetItemID();
    const bool popupOpen = ImGui::IsPopupOpen("##targetpopup");
    const float hover = easeState(subId(wid, "hover"), (ImGui::IsItemHovered() || popupOpen) ? 1.0f : 0.0f, 14.0f);
    const float open = easeState(subId(wid, "open"), popupOpen ? 1.0f : 0.0f, 16.0f);

    const bool hasTarget = selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(processes_.size());

    dl->AddRectFilled(min, max, lerpCol(pal::kCard, pal::kCardHover, hover), s(10.0f));
    dl->AddRect(min, max, lerpCol(pal::kBorder, pal::kAccent, hover * 0.6f + open * 0.4f), s(10.0f), 0, 1.0f);

    // Icon tile.
    const float tile = s(36.0f);
    const ImVec2 tileMin(min.x + s(11.0f), min.y + (h - tile) * 0.5f);
    const ImVec2 tileMax(tileMin.x + tile, tileMin.y + tile);
    dl->AddRectFilled(tileMin, tileMax, hasTarget ? pal::kAccentSoft : pal::kWidget, s(8.0f));
    glyph(dl, fonts_.icon, kGlyphCube, ImVec2((tileMin.x + tileMax.x) * 0.5f, (tileMin.y + tileMax.y) * 0.5f),
          hasTarget ? pal::kAccentGlow : pal::kTextMute);

    // Chevron rotates 180 degrees while the popup is open.
    const ImVec2 chevC(max.x - s(22.0f), (min.y + max.y) * 0.5f);
    glyph(dl, fonts_.icon, kGlyphChevronDown, chevC, lerpCol(pal::kTextMute, pal::kText, hover), open * kPi, 0.8f);

    const float textX = tileMax.x + s(12.0f);
    const float textRight = chevC.x - s(18.0f);

    if (hasTarget) {
        const McProcess& p = processes_[static_cast<size_t>(selectedIndex_)];
        const std::string name = wideToUtf8(p.exeName);
        char sub[96];
        snprintf(sub, sizeof(sub), "PID %lu  \xC2\xB7  %s", static_cast<unsigned long>(p.pid), p.x64 ? "x64" : "x86");

        const char* kindName = scanner_.launcherName(p.launcher);
        const ImVec2 bsz = badgeSize(kindName);
        const float badgeX = textRight - bsz.x;

        textEllipsis(dl, fonts_.bold, ImVec2(textX, min.y + s(11.0f)), badgeX - s(10.0f) - textX, pal::kText, name.c_str());
        text(dl, fonts_.mono, ImVec2(textX, min.y + s(32.0f)), pal::kTextDim, sub);
        badge(dl, ImVec2(badgeX, (min.y + max.y) * 0.5f - bsz.y * 0.5f), kindName, launcherBadgeColor(p.launcher));
    } else {
        text(dl, fonts_.bold, ImVec2(textX, min.y + s(11.0f)), pal::kTextDim, "No process found");
        text(dl, fonts_.caption, ImVec2(textX, min.y + s(33.0f)), pal::kTextMute, "Start Minecraft 1.8.9, then refresh");
    }

    if (clicked)
        ImGui::OpenPopup("##targetpopup");

    drawProcessPopup(ImVec2(min.x, max.y + s(6.0f)), w);
    return max.y;
}

void LauncherGui::drawProcessPopup(ImVec2 anchor, float width)
{
    // Fade/slide state lives in the main window's storage because the popup
    // window does not exist while closed.
    const ImGuiID id = ImGui::GetID("##targetpopupanim");
    float& a = stateFloat(id, 0.0f);
    if (!ImGui::IsPopupOpen("##targetpopup")) {
        a = 0.0f;
        return;
    }
    Ease(a, 1.0f, 18.0f);
    const float alpha = SmoothStep(a);

    const float rowH = s(46.0f);
    const int rows = std::max<int>(1, static_cast<int>(processes_.size()));
    const float headerH = s(30.0f);
    const float footerH = s(38.0f);
    // Cap the list height so the popup never escapes the main window; the list
    // region scrolls when there are more processes than fit.
    const float maxListH = s(250.0f);
    const float listH = ImMin(rowH * static_cast<float>(rows), maxListH);
    const bool listScrolls = rowH * static_cast<float>(rows) > listH + 1.0f;
    const float popupH = headerH + listH + s(10.0f) + footerH + s(8.0f);

    ImGui::SetNextWindowPos(ImVec2(anchor.x, anchor.y + ImLerp(-s(8.0f), 0.0f, alpha)));
    ImGui::SetNextWindowSize(ImVec2(width, popupH));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, pal::ToVec4(pal::kPanel));
    ImGui::PushStyleColor(ImGuiCol_Border, pal::ToVec4(pal::kBorder));

    if (ImGui::BeginPopup("##targetpopup", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 wp = ImGui::GetWindowPos();
        const float x = wp.x;
        float y = wp.y;

        textIn(dl, fonts_.caption, ImVec2(x + s(14.0f), y), ImVec2(x + width, y + headerH),
               pal::Alpha(pal::kTextMute, alpha), "SELECT PROCESS", ImVec2(0.0f, 0.5f));
        dl->AddLine(ImVec2(x + s(10.0f), y + headerH - 0.5f), ImVec2(x + width - s(10.0f), y + headerH - 0.5f),
                    pal::Alpha(pal::kLine, alpha), 1.0f);
        y += headerH;
        const float listTop = y;

        ImGui::SetCursorScreenPos(ImVec2(x + s(6.0f), y));
        const ImGuiWindowFlags listFlags = ImGuiWindowFlags_NoBackground |
            (listScrolls ? ImGuiWindowFlags_AlwaysVerticalScrollbar : ImGuiWindowFlags_None);
        ImGui::BeginChild("##proclist", ImVec2(width - s(12.0f), listH), ImGuiChildFlags_None, listFlags);
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float rowW = ImGui::GetContentRegionAvail().x;

            if (processes_.empty()) {
                const ImVec2 et = ImGui::GetCursorScreenPos();
                textIn(dl, fonts_.regular, et, ImVec2(et.x + rowW, et.y + rowH),
                       pal::Alpha(pal::kTextMute, alpha), "No candidates found");
                ImGui::Dummy(ImVec2(rowW, rowH));
            }

            for (int i = 0; i < static_cast<int>(processes_.size()); ++i) {
                const McProcess& p = processes_[static_cast<size_t>(i)];
                ImGui::PushID(i);
                ImGui::SetCursorPos(ImVec2(0.0f, static_cast<float>(i) * rowH));
                const bool rowClicked = ImGui::InvisibleButton("##row", ImVec2(rowW, rowH));
                const ImGuiID rid = ImGui::GetItemID();
                const float hover = easeState(subId(rid, "hover"), ImGui::IsItemHovered() ? 1.0f : 0.0f, 16.0f);
                const bool selected = selectedIndex_ == i;
                const float sel = easeState(subId(rid, "sel"), selected ? 1.0f : 0.0f, 16.0f, selected ? 1.0f : 0.0f);
                ImGui::PopID();

                const ImVec2 itemMin = ImGui::GetItemRectMin();
                const ImVec2 rmin(itemMin.x, itemMin.y + s(2.0f));
                const ImVec2 rmax(ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y - s(2.0f));
            if (hover > 0.01f)
                dl->AddRectFilled(rmin, rmax, pal::Alpha(pal::kCardHover, hover * alpha), s(7.0f));
            if (sel > 0.01f) {
                dl->AddRectFilled(rmin, rmax, pal::Alpha(pal::kAccentSoft, sel * alpha), s(7.0f));
                dl->AddRectFilled(ImVec2(rmin.x, rmin.y + s(10.0f)), ImVec2(rmin.x + s(3.0f), rmax.y - s(10.0f)),
                                  pal::Alpha(pal::kAccent, sel * alpha), s(2.0f));
            }

            const std::string name = wideToUtf8(p.exeName);
            char sub[96];
            snprintf(sub, sizeof(sub), "PID %lu  \xC2\xB7  %s  \xC2\xB7  %s", static_cast<unsigned long>(p.pid),
                     scanner_.launcherName(p.launcher), p.x64 ? "x64" : "x86");

            // Status chips on the right.
            float chipX = rmax.x - s(10.0f);
            const char* chips[2] = { "LWJGL", "JVM" };
            const bool chipOn[2] = { p.hasLwjgl, p.hasJvm };
            for (int k = 0; k < 2; ++k) {
                const ImVec2 bs = badgeSize(chips[k]);
                chipX -= bs.x;
                badge(dl, ImVec2(chipX, (rmin.y + rmax.y) * 0.5f - bs.y * 0.5f), chips[k],
                      chipOn[k] ? pal::kGood : pal::kTextMute, alpha);
                chipX -= s(6.0f);
            }

            const float tx = rmin.x + s(14.0f);
            textEllipsis(dl, fonts_.bold, ImVec2(tx, rmin.y + s(6.0f)), chipX - s(10.0f) - tx,
                         pal::Alpha(lerpCol(pal::kTextDim, pal::kText, std::max(hover, sel)), alpha), name.c_str());
            text(dl, fonts_.mono, ImVec2(tx, rmin.y + s(24.0f)), pal::Alpha(pal::kTextDim, alpha), sub);

            if (rowClicked) {
                selectedIndex_ = i;
                manualSelect_ = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndChild();
        }

        y = listTop + listH;
        dl->AddLine(ImVec2(x + s(10.0f), y + s(3.5f)), ImVec2(x + width - s(10.0f), y + s(3.5f)),
                    pal::Alpha(pal::kLine, alpha), 1.0f);
        y += s(8.0f);

        // Footer: refresh row.
        ImGui::SetCursorScreenPos(ImVec2(x + s(6.0f), y));
        const bool refreshClicked = ImGui::InvisibleButton("##refreshrow", ImVec2(width - s(12.0f), footerH - s(4.0f)));
        const ImGuiID fid = ImGui::GetItemID();
        const float fh = easeState(subId(fid, "hover"), ImGui::IsItemHovered() ? 1.0f : 0.0f, 16.0f);
        const ImVec2 fmin(x + s(6.0f), y);
        const ImVec2 fmax(x + width - s(6.0f), y + footerH - s(4.0f));
        if (fh > 0.01f)
            dl->AddRectFilled(fmin, fmax, pal::Alpha(pal::kCardHover, fh * alpha), s(7.0f));
        glyph(dl, fonts_.icon, kGlyphRefresh, ImVec2(fmin.x + s(20.0f), (fmin.y + fmax.y) * 0.5f),
              pal::Alpha(lerpCol(pal::kTextDim, pal::kAccentGlow, fh), alpha), (1.0f - refreshSpin_) * 2.0f * kPi, 0.85f);
        textIn(dl, fonts_.regular, ImVec2(fmin.x + s(36.0f), fmin.y), fmax,
               pal::Alpha(lerpCol(pal::kTextDim, pal::kText, fh), alpha), "Refresh list", ImVec2(0.0f, 0.5f));
        if (refreshClicked)
            pendingRefresh_ = true;

        ImGui::EndPopup();
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

float LauncherGui::drawInjectButton(float y)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float x = origin_.x + s(kPadBase);
    const float w = width_ - s(kPadBase) * 2.0f;
    const float h = s(44.0f);

    const bool hasTarget = selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(processes_.size());
    const bool enabled = !injecting_ && hasTarget && !payloadPath_.empty();

    ImGui::SetCursorScreenPos(ImVec2(x, y));
    if (!enabled)
        ImGui::BeginDisabled();
    const bool clicked = ImGui::InvisibleButton("##inject", ImVec2(w, h));
    const ImGuiID wid = ImGui::GetItemID();
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    if (!enabled)
        ImGui::EndDisabled();

    const float hover = easeState(subId(wid, "hover"), (hovered && enabled) ? 1.0f : 0.0f, 14.0f);
    const float press = easeState(subId(wid, "press"), (active && enabled) ? 1.0f : 0.0f, 22.0f);
    const float on = easeState(subId(wid, "enabled"), enabled ? 1.0f : 0.0f, 12.0f, enabled ? 1.0f : 0.0f);
    const float busy = easeState(subId(wid, "busy"), injecting_ ? 1.0f : 0.0f, 12.0f);

    // Press shrinks the button a touch; disabled fades it towards the panel.
    const float inset = press * s(2.0f);
    const ImVec2 min(x + inset, y + inset);
    const ImVec2 max(x + w - inset, y + h - inset);
    const float r = s(10.0f);

    const ImU32 fill = lerpCol(lerpCol(pal::kWidget, pal::kAccent, on), pal::kAccentGlow, hover * 0.7f);
    dl->AddRectFilled(min, max, fill, r);
    if (hover > 0.01f && on > 0.5f) {
        // Soft outer glow on hover.
        dl->AddRect(ImVec2(min.x - 1.0f, min.y - 1.0f), ImVec2(max.x + 1.0f, max.y + 1.0f),
                    pal::Alpha(pal::kAccentGlow, 0.35f * hover), r + 1.0f, 0, s(2.0f));
    }

    if (busy > 0.01f) {
        // Indeterminate shimmer sweeping left to right while injecting.
        dl->PushClipRect(min, max, true);
        const float band = w * 0.35f;
        const float t = std::fmod(static_cast<float>(ImGui::GetTime()) * 0.55f, 1.0f);
        const float sx = min.x - band + (w + band) * t;
        dl->AddRectFilledMultiColor(ImVec2(sx, min.y), ImVec2(sx + band * 0.5f, max.y),
                                    IM_COL32(255, 255, 255, 0), pal::Alpha(IM_COL32(255, 255, 255, 255), 0.16f * busy),
                                    pal::Alpha(IM_COL32(255, 255, 255, 255), 0.16f * busy), IM_COL32(255, 255, 255, 0));
        dl->AddRectFilledMultiColor(ImVec2(sx + band * 0.5f, min.y), ImVec2(sx + band, max.y),
                                    pal::Alpha(IM_COL32(255, 255, 255, 255), 0.16f * busy), IM_COL32(255, 255, 255, 0),
                                    IM_COL32(255, 255, 255, 0), pal::Alpha(IM_COL32(255, 255, 255, 255), 0.16f * busy));
        dl->PopClipRect();
    }

    const char* label = injecting_ ? "Injecting..." : (hasTarget ? "Inject" : "No target");
    const ImU32 textCol = lerpCol(pal::kTextMute, pal::kOnAccent, on);
    const ImVec2 ls = textSize(fonts_.bold, label);
    const float iconW = s(14.0f);
    const float total = ls.x + iconW + s(8.0f);
    const float startX = (min.x + max.x) * 0.5f - total * 0.5f;
    const float cy = (min.y + max.y) * 0.5f;
    glyph(dl, fonts_.icon, kGlyphBolt, ImVec2(startX + iconW * 0.5f, cy), textCol, 0.0f, 0.85f);
    text(dl, fonts_.bold, ImVec2(std::floor(startX + iconW + s(8.0f)), std::floor(cy - ls.y * 0.5f)), textCol, label);

    if (clicked && enabled)
        injectSelected();

    return y + h;
}

float LauncherGui::drawProgress(float y)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float x = origin_.x + s(kPadBase);
    const float w = width_ - s(kPadBase) * 2.0f;

    int step = 0, total = 1;
    std::string label;
    {
        std::lock_guard lock(injectMutex_);
        step = progressStep_;
        total = progressTotal_;
        label = progressLabel_;
    }

    const ImGuiID id = ImGui::GetID("##progress");
    const float show = easeState(subId(id, "show"), injecting_ ? 1.0f : 0.0f, 12.0f);
    const float frac = total > 0 ? Clamp01(static_cast<float>(step) / static_cast<float>(total)) : 0.0f;
    float& fill = stateFloat(subId(id, "fill"), 0.0f);
    if (!injecting_ && show < 0.02f)
        fill = 0.0f; // reset for the next run once faded out
    else
        Ease(fill, frac, 18.0f);

    if (show < 0.01f)
        return y;

    const float textY = y + ImLerp(s(6.0f), 0.0f, show);
    char counter[24];
    snprintf(counter, sizeof(counter), "%d/%d", step, total);
    const ImVec2 cs = textSize(fonts_.mono, counter);

    textEllipsis(dl, fonts_.caption, ImVec2(x, textY), w - cs.x - s(12.0f), pal::Alpha(pal::kTextDim, show),
                 label.empty() ? "Working..." : label.c_str());
    text(dl, fonts_.mono, ImVec2(x + w - cs.x, textY), pal::Alpha(pal::kTextDim, show), counter);

    const float trackY = textY + s(18.0f);
    const float trackH = s(6.0f);
    dl->AddRectFilled(ImVec2(x, trackY), ImVec2(x + w, trackY + trackH), pal::Alpha(pal::kWidget, show), trackH * 0.5f);
    const float fillW = std::max(trackH, w * fill);
    dl->AddRectFilled(ImVec2(x, trackY), ImVec2(x + fillW, trackY + trackH), pal::Alpha(pal::kAccent, show), trackH * 0.5f);
    dl->AddRectFilled(ImVec2(x, trackY - 1.0f), ImVec2(x + fillW, trackY + trackH + 1.0f),
                      pal::Alpha(pal::kAccentGlow, 0.25f * show), trackH * 0.5f);

    return trackY + trackH;
}

void LauncherGui::drawLogPanel(float top, float bottom)
{
    const float h = bottom - top;
    if (h < s(24.0f) || logAnim_ < 0.01f)
        return;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float x = origin_.x + s(kPadBase);
    const float w = width_ - s(kPadBase) * 2.0f;
    const float alpha = SmoothStep(logAnim_);
    const ImVec2 min(x, top);
    const ImVec2 max(x + w, bottom);

    dl->AddRectFilled(min, max, pal::Alpha(pal::kPanel, alpha), s(10.0f));
    dl->AddRect(min, max, pal::Alpha(pal::kBorder, alpha), s(10.0f), 0, 1.0f);

    // Header: title, count, actions.
    const float headerH = s(34.0f);
    const float cy = top + headerH * 0.5f;
    glyph(dl, fonts_.icon, kGlyphTerminal, ImVec2(min.x + s(18.0f), cy), pal::Alpha(pal::kAccentGlow, alpha), 0.0f, 0.8f);
    const ImVec2 tsz = textSize(fonts_.bold, "Log");
    text(dl, fonts_.bold, ImVec2(min.x + s(32.0f), std::floor(cy - tsz.y * 0.5f)), pal::Alpha(pal::kText, alpha), "Log");

    size_t count = 0;
    LauncherLog::I().withEntries([&](const std::vector<LogEntry>& e) { count = e.size(); });
    char countBuf[32];
    snprintf(countBuf, sizeof(countBuf), "%zu entries", count);
    text(dl, fonts_.caption, ImVec2(min.x + s(32.0f) + tsz.x + s(8.0f), std::floor(cy - textSize(fonts_.caption, countBuf).y * 0.5f)),
         pal::Alpha(pal::kTextMute, alpha), countBuf);

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);

    // Right-aligned actions: [Clear] [Copy]  Auto-scroll [switch]
    float ax = max.x - s(12.0f) - s(36.0f);
    toggleSwitch("##autoscroll", ImVec2(ax, cy - s(10.0f)), &autoScrollLog_);
    const ImVec2 asz = textSize(fonts_.caption, "Auto-scroll");
    ax -= s(8.0f) + asz.x;
    text(dl, fonts_.caption, ImVec2(ax, std::floor(cy - asz.y * 0.5f)), pal::Alpha(pal::kTextDim, alpha), "Auto-scroll");
    ax -= s(14.0f) + textSize(fonts_.caption, "Copy").x + s(16.0f);
    if (textButton("##copy", "Copy", ImVec2(ax, cy - s(11.0f))))
        copyLogToClipboard();
    ax -= s(6.0f) + textSize(fonts_.caption, "Clear").x + s(16.0f);
    if (textButton("##clear", "Clear", ImVec2(ax, cy - s(11.0f))))
        LauncherLog::I().clear();

    dl->AddLine(ImVec2(min.x + s(10.0f), top + headerH - 0.5f), ImVec2(max.x - s(10.0f), top + headerH - 0.5f),
                pal::Alpha(pal::kLine, alpha), 1.0f);

    // Scrolling entries.
    const ImVec2 areaMin(min.x + s(6.0f), top + headerH + s(4.0f));
    const ImVec2 areaSize(w - s(12.0f), std::max(s(10.0f), bottom - s(6.0f) - areaMin.y));
    ImGui::SetCursorScreenPos(areaMin);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(s(8.0f), s(4.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(s(4.0f), s(3.0f)));
    ImGui::BeginChild("##logarea", areaSize, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground);
    ImGui::PushFont(safeFont(fonts_.mono));
    LauncherLog::I().withEntries([&](const std::vector<LogEntry>& entries) {
        for (const LogEntry& e : entries) {
            ImGui::PushStyleColor(ImGuiCol_Text, LauncherLog::levelColor(e.level));
            ImGui::TextDisabled("%s", e.timestamp.c_str());
            ImGui::SameLine(0.0f, s(6.0f));
            ImGui::TextDisabled("%-5s", LauncherLog::levelLabel(e.level));
            ImGui::SameLine(0.0f, s(8.0f));
            ImGui::TextWrapped("%s", e.message.c_str());
            ImGui::PopStyleColor();
        }
    });
    ImGui::PopFont();
    if (autoScrollLog_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::PopStyleVar(2);

    ImGui::PopStyleVar(); // Alpha
}

void LauncherGui::drawBottomBar(float top)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float x = origin_.x + s(kPadBase);
    const float right = origin_.x + width_ - s(kPadBase);
    const float h = s(28.0f);
    const float cy = top + h * 0.5f;

    // Log toggle with a chevron that flips while open.
    const char* label = "Log";
    const ImVec2 ls = textSize(fonts_.caption, label);
    const ImVec2 size(ls.x + s(34.0f), s(24.0f));
    ImGui::SetCursorScreenPos(ImVec2(x, cy - size.y * 0.5f));
    if (ImGui::InvisibleButton("##logtoggle", size))
        logOpen_ = !logOpen_;
    const ImGuiID wid = ImGui::GetItemID();
    const float hover = easeState(subId(wid, "hover"), ImGui::IsItemHovered() ? 1.0f : 0.0f, 14.0f);
    const ImVec2 bmin(x, cy - size.y * 0.5f);
    const ImVec2 bmax(x + size.x, cy + size.y * 0.5f);
    dl->AddRectFilled(bmin, bmax, pal::Alpha(pal::kWidgetHover, 0.4f + hover * 0.6f), s(6.0f));
    const ImU32 col = lerpCol(pal::kTextDim, pal::kText, std::max(hover, logAnim_));
    glyph(dl, fonts_.icon, kGlyphChevronDown, ImVec2(bmin.x + s(14.0f), cy), col, kPi * (1.0f - logAnim_), 0.7f);
    text(dl, fonts_.caption, ImVec2(bmin.x + s(26.0f), std::floor(cy - ls.y * 0.5f)), col, label);

    // Payload summary on the right.
    char info[96];
    if (embeddedPayloadBytes_ > 0)
        snprintf(info, sizeof(info), "core embedded  \xC2\xB7  %.1f MB", static_cast<double>(embeddedPayloadBytes_) / (1024.0 * 1024.0));
    else
        snprintf(info, sizeof(info), "core: sibling RuntimeHostCore.dll");
    const ImVec2 is = textSize(fonts_.caption, info);
    text(dl, fonts_.caption, ImVec2(right - is.x, std::floor(cy - is.y * 0.5f)), pal::kTextMute, info);
    if (!payloadPath_.empty())
        glyph(dl, fonts_.icon, kGlyphCheck, ImVec2(right - is.x - s(12.0f), cy), pal::kGood, 0.0f, 0.6f);
}

void LauncherGui::drawToasts()
{
    std::lock_guard lock(toastMutex_);
    if (toasts_.empty())
        return;

    const auto now = std::chrono::steady_clock::now();
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    const float right = origin_.x + width_ - s(kPadBase);
    float y = origin_.y + height_ - s(kPadBase) - s(28.0f);

    for (Toast& t : toasts_) {
        const float age = std::chrono::duration<float>(now - t.born).count();
        const bool leaving = age >= kToastLifetime;
        Ease(t.anim, leaving ? 0.0f : 1.0f, leaving ? 12.0f : 16.0f);
        const float a = SmoothStep(t.anim);
        if (a < 0.005f)
            continue;

        const ImVec2 ts = textSize(fonts_.regular, t.text.c_str());
        const ImVec2 pad(s(14.0f), s(10.0f));
        const ImVec2 box(ts.x + pad.x * 2.0f + s(10.0f), ts.y + pad.y * 2.0f);
        const float slide = ImLerp(s(28.0f), 0.0f, a);
        const ImVec2 pMin(right - box.x + slide, y - box.y);
        const ImVec2 pMax(pMin.x + box.x, pMin.y + box.y);

        dl->AddRectFilled(pMin, pMax, pal::Alpha(pal::kCard, 0.96f * a), s(8.0f));
        dl->AddRect(pMin, pMax, pal::Alpha(pal::kBorder, a), s(8.0f), 0, 1.0f);
        dl->AddRectFilled(ImVec2(pMin.x, pMin.y + s(8.0f)), ImVec2(pMin.x + s(3.0f), pMax.y - s(8.0f)),
                          pal::Alpha(t.color, a), s(2.0f));
        text(dl, fonts_.regular, ImVec2(pMin.x + pad.x + s(6.0f), pMin.y + pad.y), pal::Alpha(pal::kText, a), t.text.c_str());

        y -= (box.y + s(10.0f)) * a;
    }

    toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(), [&](const Toast& t) {
        const float age = std::chrono::duration<float>(now - t.born).count();
        return age >= kToastLifetime && t.anim < 0.01f;
    }), toasts_.end());
}

// ============================================================ frame

void LauncherGui::preFrame()
{
    if (!hwnd_)
        return;

    Ease(logAnim_, logOpen_ ? 1.0f : 0.0f, 14.0f);
    if (std::fabs(logAnim_ - (logOpen_ ? 1.0f : 0.0f)) < 0.002f)
        logAnim_ = logOpen_ ? 1.0f : 0.0f;

    const int targetW = static_cast<int>(std::lround(s(kBaseWidth)));
    const int targetH = static_cast<int>(std::lround(s(kBaseHeight) + s(kLogPanelHeight) * SmoothStep(logAnim_)));

    RECT rc{};
    if (!GetWindowRect(hwnd_, &rc))
        return;
    const int curW = rc.right - rc.left;
    const int curH = rc.bottom - rc.top;
    if (curW != targetW || curH != targetH) {
        SetWindowPos(hwnd_, nullptr, 0, 0, targetW, targetH,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
    }
}

void LauncherGui::render()
{
    if (pendingRefresh_) {
        refreshProcesses();
        pendingRefresh_ = false;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("##HostRuntimeMain", nullptr, flags);

    origin_ = ImGui::GetWindowPos();
    width_ = ImGui::GetWindowWidth();
    height_ = ImGui::GetWindowHeight();

    drawTitlebar();

    float y = origin_.y + titlebarHeight() + s(14.0f);
    y = drawStatusRow(y) + s(14.0f);
    y = drawHero(y) + s(16.0f);
    y = drawProcessCard(y) + s(14.0f);
    y = drawInjectButton(y) + s(8.0f);
    drawProgress(y);

    // The bottom bar sits at the base height; the log panel fills whatever
    // extra height the window has been animated to.
    const float bottomBarTop = origin_.y + height_ - s(38.0f);
    const float logTop = origin_.y + s(kBaseHeight) - s(38.0f) + s(2.0f);
    drawLogPanel(logTop, bottomBarTop - s(8.0f));
    drawBottomBar(bottomBarTop);

    drawToasts();

    ImGui::End();
}

} // namespace dk
