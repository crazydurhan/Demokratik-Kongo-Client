#include "launcherApp.h"
#include "launcherGui.h"
#include "stealthNames.h"
#include "ui/uiPalette.h"

#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"

// Font blobs are shared with the core DLL; see include/fonts/.
#include "fonts/inter_medium.h"
#include "fonts/inter_bold.h"
#include "fonts/jetbrainsmono.h"
#include "fonts/fa_solid.h"

#include <d3d11.h>
#include <dwmapi.h>
#include <tchar.h>

#include <cmath>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dwmapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace dk {

namespace {

IDXGISwapChain* g_swapChain = nullptr;
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
bool g_swapChainOccluded = false;
UINT g_resizeWidth = 0;
UINT g_resizeHeight = 0;
LauncherGui* g_gui = nullptr;
HWND g_hwnd = nullptr;

// Set by WM_DPICHANGED; consumed between frames so the font atlas is never
// rebuilt while ImGui is inside a frame.
float g_pendingDpiScale = 0.0f;

bool createDeviceD3D(HWND hWnd);
void cleanupDeviceD3D();
bool createRenderTarget();
void cleanupRenderTarget();
LRESULT WINAPI wndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Builds the whole font set at a given DPI scale. Called once at start-up and
// again after every DPI change (with the DX11 objects invalidated around it).
LauncherFonts buildFonts(float scale)
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    static const ImWchar kLatinExtRanges[] = {
        0x0020, 0x00FF, // Basic Latin + Latin-1 Supplement
        0x0100, 0x017F, // Latin Extended-A
        0,
    };
    static const ImWchar kIconRanges[] = { 0xF000, 0xF8FF, 0 };

    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    cfg.GlyphRanges = kLatinExtRanges;
    cfg.OversampleH = 3;
    cfg.OversampleV = 1;

    auto add = [&](const unsigned char* data, size_t size, float px, ImFontConfig& c) {
        return io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(data), static_cast<int>(size),
                                              std::floor(px * scale + 0.5f), &c);
    };

    LauncherFonts f;
    f.regular = add(inter_medium_font, sizeof(inter_medium_font), 15.0f, cfg);
    f.caption = add(inter_medium_font, sizeof(inter_medium_font), 12.0f, cfg);
    f.bold = add(inter_bold_font, sizeof(inter_bold_font), 15.0f, cfg);
    f.title = add(inter_bold_font, sizeof(inter_bold_font), 22.0f, cfg);
    f.mono = add(jetbrainsmono, sizeof(jetbrainsmono), 13.0f, cfg);

    ImFontConfig iconCfg;
    iconCfg.FontDataOwnedByAtlas = false;
    iconCfg.GlyphRanges = kIconRanges;
    iconCfg.OversampleH = 3;
    iconCfg.OversampleV = 1;
    f.icon = add(fa_solid_font, sizeof(fa_solid_font), 15.0f, iconCfg);

    io.FontDefault = f.regular;
    return f;
}

void enableRoundedCorners(HWND hWnd)
{
    const int DWMWCP_ROUND = 2;
    DwmSetWindowAttribute(hWnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &DWMWCP_ROUND, sizeof(DWMWCP_ROUND));
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hWnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
}

bool createDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL featureLevel;

    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        featureLevels, 2, D3D11_SDK_VERSION,
        &sd, &g_swapChain, &g_device, &featureLevel, &g_context);

    if (res == DXGI_ERROR_UNSUPPORTED) {
        res = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            featureLevels, 2, D3D11_SDK_VERSION,
            &sd, &g_swapChain, &g_device, &featureLevel, &g_context);
    }

    if (res != S_OK)
        return false;

    if (!createRenderTarget())
        return false;
    return true;
}

void cleanupDeviceD3D()
{
    cleanupRenderTarget();
    if (g_swapChain) { g_swapChain->Release(); g_swapChain = nullptr; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
}

bool createRenderTarget()
{
    ID3D11Texture2D* backBuffer = nullptr;
    const HRESULT hrGet = g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hrGet) || !backBuffer)
        return false;
    const HRESULT hrRtv = g_device->CreateRenderTargetView(backBuffer, nullptr, &g_mainRenderTargetView);
    backBuffer->Release();
    if (FAILED(hrRtv)) {
        g_mainRenderTargetView = nullptr;
        return false;
    }
    return true;
}

void cleanupRenderTarget()
{
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

LRESULT WINAPI wndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_resizeWidth = LOWORD(lParam);
        g_resizeHeight = HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_NCHITTEST:
    {
        // Borderless window: make the titlebar strip draggable, but keep the
        // right-hand caption buttons (drawn by ImGui) clickable.
        POINT pt{ (short)LOWORD(lParam), (short)HIWORD(lParam) };
        ScreenToClient(hWnd, &pt);
        RECT rc{};
        GetClientRect(hWnd, &rc);
        const int titleH = g_gui ? static_cast<int>(g_gui->titlebarHeight()) : 44;
        const int buttonsW = g_gui ? static_cast<int>(g_gui->captionButtonsWidth()) : 110;
        if (pt.y >= 0 && pt.y <= titleH && pt.x >= 0 && pt.x < rc.right - buttonsW)
            return HTCAPTION;
        return HTCLIENT;
    }
    case WM_DPICHANGED:
    {
        // Windows hands us the rect it wants the window to occupy on the new
        // monitor; take it, and rebuild fonts at the new scale between frames.
        g_pendingDpiScale = static_cast<float>(HIWORD(wParam)) / 96.0f;
        const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
        if (suggested) {
            SetWindowPos(hWnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

} // namespace

int LauncherApp::run(HINSTANCE hInstance)
{
    // The manifest already declares PerMonitorV2; this covers the case where
    // the binary is launched through a shim that strips it.
    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = stealth::kWindowClass;
    if (!RegisterClassExW(&wc)) {
        return 1;
    }

    // Size the window for the DPI of the monitor it will appear on.
    const POINT screenCenter{ GetSystemMetrics(SM_CXSCREEN) / 2, GetSystemMetrics(SM_CYSCREEN) / 2 };
    float dpiScale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(screenCenter, MONITOR_DEFAULTTOPRIMARY));
    if (dpiScale <= 0.0f)
        dpiScale = 1.0f;

    const int width = static_cast<int>(LauncherGui::kBaseWidth * dpiScale + 0.5f);
    const int height = static_cast<int>(LauncherGui::kBaseHeight * dpiScale + 0.5f);
    const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;

    HWND hwnd = CreateWindowExW(
        0, wc.lpszClassName, stealth::kWindowTitle,
        WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y, width, height,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }
    g_hwnd = hwnd;
    enableRoundedCorners(hwnd);

    // The window may have landed on a monitor with a different DPI than the
    // one we guessed from the primary screen centre.
    const float hwndScale = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
    if (hwndScale > 0.0f && std::fabs(hwndScale - dpiScale) > 0.01f) {
        dpiScale = hwndScale;
        const int w2 = static_cast<int>(LauncherGui::kBaseWidth * dpiScale + 0.5f);
        const int h2 = static_cast<int>(LauncherGui::kBaseHeight * dpiScale + 0.5f);
        SetWindowPos(hwnd, nullptr, 0, 0, w2, h2, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
    }

    if (!createDeviceD3D(hwnd)) {
        cleanupDeviceD3D();
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // Disable imgui.ini; launcher layout does not need disk persistence.
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    LauncherFonts fonts = buildFonts(dpiScale);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    LauncherGui gui;
    g_gui = &gui;
    gui.init(hwnd, fonts, dpiScale);

    // Clear to the same colour the GUI paints so DWM's rounded corners never
    // reveal a differently coloured edge.
    const ImVec4 clearColor = dk::ui::palette::ToVec4(dk::ui::palette::kBackground);
    bool done = false;

    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        if (g_swapChainOccluded && g_swapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            Sleep(10);
            continue;
        }
        g_swapChainOccluded = false;

        if (g_pendingDpiScale > 0.0f) {
            // Outside NewFrame/Render: safe to throw the atlas away and rebuild.
            dpiScale = g_pendingDpiScale;
            g_pendingDpiScale = 0.0f;
            ImGui_ImplDX11_InvalidateDeviceObjects();
            fonts = buildFonts(dpiScale);
            ImGui_ImplDX11_CreateDeviceObjects();
            gui.onDpiChanged(fonts, dpiScale);
        }

        // Animates the OS window size (log panel) before the swap chain resize
        // below, so the frame we are about to draw already matches it.
        gui.preFrame();

        if (g_resizeWidth != 0 && g_resizeHeight != 0) {
            cleanupRenderTarget();
            g_swapChain->ResizeBuffers(0, g_resizeWidth, g_resizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeWidth = g_resizeHeight = 0;
            if (!createRenderTarget())
                break; // render target unusable — exit the loop cleanly instead of null-deref
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        gui.render();

        ImGui::Render();
        const float clear[4] = { clearColor.x, clearColor.y, clearColor.z, clearColor.w };
        g_context->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_context->ClearRenderTargetView(g_mainRenderTargetView, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        const HRESULT hr = g_swapChain->Present(1, 0);
        g_swapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    gui.shutdown();
    g_gui = nullptr;

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    cleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}

} // namespace dk
