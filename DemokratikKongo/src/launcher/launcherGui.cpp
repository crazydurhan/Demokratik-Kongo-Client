#include "launcherGui.h"
#include "resourceLoader.h"
#include "util.h"

#include "imgui.h"

#include <cstring>
#include <sstream>

namespace dk {

void LauncherGui::init(ID3D11Device* /*device*/)
{
    applyTheme();

    auto& log = LauncherLog::I();
    log.info("DemokratikKongo launcher — embedded core + optional sibling RuntimeHostCore.dll");

    embeddedPayloadBytes_ = embeddedPayloadSize();
    if (embeddedPayloadBytes_ == 0) {
        log.warn("No embedded core in EXE — will use RuntimeHostCore.dll if placed next to launcher.");
    } else {
        log.info("Embedded core: " + std::to_string(embeddedPayloadBytes_) + " bytes inside EXE");
    }

    payloadPath_ = ensureEmbeddedPayload();
    if (payloadPath_.empty()) {
        setStatus("Core missing — add RuntimeHostCore.dll");
    } else {
        log.debug("Payload: " + wideToUtf8(payloadPath_));
        setStatus("Ready");
    }
}

void LauncherGui::shutdown()
{
}

void LauncherGui::applyTheme()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    style.WindowRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.WindowPadding = ImVec2(16.0f, 16.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.CellPadding = ImVec2(8.0f, 6.0f);

    colors[ImGuiCol_Text] = ImVec4(0.92f, 0.94f, 0.96f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.54f, 0.58f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.11f, 0.14f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.11f, 0.14f, 0.98f);
    colors[ImGuiCol_Border] = ImVec4(0.18f, 0.20f, 0.24f, 1.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.16f, 0.20f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.20f, 0.26f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.24f, 0.30f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.06f, 0.07f, 0.09f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.12f, 0.16f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.24f, 0.32f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.32f, 0.42f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.38f, 0.50f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.20f, 0.45f, 0.85f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.52f, 0.92f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.16f, 0.38f, 0.76f, 1.00f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.12f, 0.14f, 0.18f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.18f, 0.20f, 0.24f, 1.00f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.14f, 0.16f, 0.20f, 1.00f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.24f, 0.28f, 0.34f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.36f, 0.44f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.36f, 0.44f, 0.54f, 1.00f);
}

ImVec4 LauncherGui::launcherBadgeColor(LauncherKind kind) const
{
    switch (kind) {
    case LauncherKind::Lunar:    return ImVec4(0.55f, 0.65f, 1.00f, 1.0f);
    case LauncherKind::Badlion:  return ImVec4(1.00f, 0.55f, 0.20f, 1.0f);
    case LauncherKind::Forge:    return ImVec4(0.85f, 0.45f, 0.25f, 1.0f);
    case LauncherKind::Fabric:   return ImVec4(0.75f, 0.55f, 0.95f, 1.0f);
    case LauncherKind::OptiFine: return ImVec4(0.35f, 0.85f, 0.55f, 1.0f);
    case LauncherKind::Vanilla:  return ImVec4(0.45f, 0.85f, 0.45f, 1.0f);
    case LauncherKind::Custom:   return ImVec4(0.95f, 0.75f, 0.35f, 1.0f);
    default:                     return ImVec4(0.55f, 0.58f, 0.62f, 1.0f);
    }
}

void LauncherGui::setStatus(const std::string& text)
{
    status_ = text;
}

void LauncherGui::copyLogToClipboard()
{
    std::ostringstream oss;
    for (const LogEntry& e : LauncherLog::I().entries()) {
        oss << '[' << e.timestamp << "] [" << LauncherLog::levelLabel(e.level) << "] "
            << e.message << '\n';
    }
    const std::string text = oss.str();
    if (text.empty())
        return;

    if (OpenClipboard(nullptr)) {
        EmptyClipboard();
        const SIZE_T bytes = (text.size() + 1) * sizeof(char);
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (mem) {
            void* ptr = GlobalLock(mem);
            if (ptr) {
                memcpy(ptr, text.c_str(), bytes);
                GlobalUnlock(mem);
                SetClipboardData(CF_TEXT, mem);
            } else {
                GlobalFree(mem);
            }
        }
        CloseClipboard();
        LauncherLog::I().info("Log copied to clipboard.");
    }
}

void LauncherGui::refreshProcesses()
{
    auto& log = LauncherLog::I();
    log.info("Scanning for Minecraft / JVM processes...");

    processes_ = scanner_.scan();
    if (selectedIndex_ >= static_cast<int>(processes_.size()))
        selectedIndex_ = processes_.empty() ? -1 : static_cast<int>(processes_.size()) - 1;

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

    setStatus("Found " + std::to_string(processes_.size()) + " process(es)");
}

void LauncherGui::injectSelected()
{
    auto& log = LauncherLog::I();

    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(processes_.size())) {
        log.warn("No process selected — click a row in the table first.");
        setStatus("Select a process first");
        return;
    }

    if (payloadPath_.empty()) {
        log.warn("Core cache missing — extracting from embedded EXE...");
        payloadPath_ = forceExtractEmbeddedPayload();
        if (payloadPath_.empty()) {
            setStatus("Core extract failed");
            return;
        }
    } else {
        payloadPath_ = ensureEmbeddedPayload();
        if (payloadPath_.empty()) {
            setStatus("Core sync failed");
            return;
        }
    }

    const McProcess& target = processes_[static_cast<size_t>(selectedIndex_)];
    log.info("Target: PID " + std::to_string(target.pid) +
             " (" + wideToUtf8(target.exeName) + ", " +
             scanner_.launcherName(target.launcher) + ", " +
             (target.x64 ? "x64" : "x86") + ")");
    setStatus("Injecting...");

    const InjectionResult result = injector_.inject(target.pid, payloadPath_);
    if (result.ok) {
        setStatus("Injection successful");
    } else {
        log.error("Injection failed at step: " + std::string(injectStepName(result.failedStep)));
        if (result.systemError)
            log.debug("Last Win32 error: " + lastErrorString(result.systemError));
        setStatus("Injection failed");
    }
}

void LauncherGui::renderLogPanel()
{
    ImGui::TextUnformatted("Log");
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu entries)", LauncherLog::I().entries().size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear"))
        LauncherLog::I().clear();
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy"))
        copyLogToClipboard();
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &autoScrollLog_);

    const std::string logPath = LauncherLog::I().logFilePath();
    if (!logPath.empty())
        ImGui::TextDisabled("File: %s", logPath.c_str());

    ImGui::BeginChild("LogArea", ImVec2(0.0f, 0.0f), true);

    for (const LogEntry& e : LauncherLog::I().entries()) {
        ImGui::PushStyleColor(ImGuiCol_Text, LauncherLog::levelColor(e.level));

        ImGui::TextDisabled("[%s]", e.timestamp.c_str());
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextDisabled("[%s]", LauncherLog::levelLabel(e.level));
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::TextWrapped("%s", e.message.c_str());

        ImGui::PopStyleColor();
    }

    if (autoScrollLog_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);

    ImGui::EndChild();
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

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("##HostRuntimeMain", nullptr, flags);

    ImGui::TextUnformatted("DemokratikKongo");
    ImGui::SameLine();
    ImGui::TextDisabled("| Tek EXE injector");
    ImGui::SameLine(ImGui::GetWindowWidth() - 280.0f);
    ImGui::TextDisabled("Status: %s", status_.c_str());

    if (embeddedPayloadBytes_ > 0) {
        ImGui::TextDisabled("Core %u byte EXE icinde gomulu — baskasina sadece bu dosyayi gonder.",
            embeddedPayloadBytes_);
    } else {
        ImGui::TextColored(ImVec4(1.f, 0.45f, 0.35f, 1.f),
            "HATA: Gomulu core yok — solution yeniden derlenmeli.");
    }

    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Refresh", ImVec2(110.0f, 0.0f)))
        refreshProcesses();

    ImGui::SameLine();
    const bool canInject = selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(processes_.size()) && !payloadPath_.empty();
    if (!canInject)
        ImGui::BeginDisabled();
    if (ImGui::Button("Inject", ImVec2(110.0f, 0.0f)))
        injectSelected();
    if (!canInject)
        ImGui::EndDisabled();

    ImGui::Spacing();

    const ImGuiTableFlags tableFlags =
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_BordersOuter |
        ImGuiTableFlags_BordersV |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_Resizable;

    const float tableHeight = ImGui::GetContentRegionAvail().y * 0.50f;
    if (ImGui::BeginTable("ProcessList", 5, tableFlags, ImVec2(0.0f, tableHeight))) {
        ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Window Title", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Arch", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Launcher", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(processes_.size()); ++i) {
            const McProcess& p = processes_[static_cast<size_t>(i)];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            const std::string rowId = "##row" + std::to_string(i);
            const bool selected = (selectedIndex_ == i);
            if (ImGui::Selectable((std::to_string(p.pid) + rowId).c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
                selectedIndex_ = i;

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(wideToUtf8(p.exeName).c_str());

            ImGui::TableSetColumnIndex(2);
            const std::string title = wideToUtf8(p.windowTitle);
            ImGui::TextUnformatted(title.empty() ? "(no title)" : title.c_str());

            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(p.x64 ? "x64" : "x86");

            ImGui::TableSetColumnIndex(4);
            const ImVec4 badge = launcherBadgeColor(p.launcher);
            ImGui::PushStyleColor(ImGuiCol_Text, badge);
            ImGui::TextUnformatted(scanner_.launcherName(p.launcher));
            ImGui::PopStyleColor();
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    renderLogPanel();

    ImGui::End();
}

} // namespace dk
