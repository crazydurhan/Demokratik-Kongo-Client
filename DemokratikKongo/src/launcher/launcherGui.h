#pragma once

#include "injector.h"
#include "logger.h"
#include "process_scanner.h"

#include "imgui.h"

#include <d3d11.h>
#include <string>
#include <vector>

namespace dk {

class LauncherGui {
public:
    void init(ID3D11Device* device);
    void shutdown();
    void render();

    void setStatus(const std::string& text);

private:
    void applyTheme();
    void refreshProcesses();
    void injectSelected();
    void renderLogPanel();
    void copyLogToClipboard();
    ImVec4 launcherBadgeColor(LauncherKind kind) const;

    ProcessScanner scanner_;
    Injector injector_;
    std::vector<McProcess> processes_;
    int selectedIndex_ = -1;
    std::wstring payloadPath_;
    DWORD embeddedPayloadBytes_ = 0;
    std::string status_;
    bool pendingRefresh_ = true;
    bool autoScrollLog_ = true;
};

} // namespace dk
