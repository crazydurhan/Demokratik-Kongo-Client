#include "launcherApp.h"
#include "launcherGui.h"
#include "stealthNames.h"

#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"

#include <d3d11.h>
#include <tchar.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

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

bool createDeviceD3D(HWND hWnd);
void cleanupDeviceD3D();
void createRenderTarget();
void cleanupRenderTarget();
LRESULT WINAPI wndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

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

    createRenderTarget();
    return true;
}

void cleanupDeviceD3D()
{
    cleanupRenderTarget();
    if (g_swapChain) { g_swapChain->Release(); g_swapChain = nullptr; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
}

void createRenderTarget()
{
    ID3D11Texture2D* backBuffer = nullptr;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    g_device->CreateRenderTargetView(backBuffer, nullptr, &g_mainRenderTargetView);
    backBuffer->Release();
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
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = stealth::kWindowClass;
    RegisterClassExW(&wc);

    constexpr int kWidth = 960;
    constexpr int kHeight = 640;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - kWidth) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - kHeight) / 2;

    HWND hwnd = CreateWindowExW(
        0, wc.lpszClassName, stealth::kWindowTitle,
        WS_OVERLAPPEDWINDOW, x, y, kWidth, kHeight,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!createDeviceD3D(hwnd)) {
        cleanupDeviceD3D();
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
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    LauncherGui gui;
    g_gui = &gui;
    gui.init(g_device);

    const ImVec4 clearColor(0.06f, 0.07f, 0.09f, 1.0f);
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

        if (g_resizeWidth != 0 && g_resizeHeight != 0) {
            cleanupRenderTarget();
            g_swapChain->ResizeBuffers(0, g_resizeWidth, g_resizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeWidth = g_resizeHeight = 0;
            createRenderTarget();
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
