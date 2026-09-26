#include "../menu.h"
#include "../../base.h"

#include <gl/GL.h>
#include <mutex>
#include <Shlobj.h>
#include <atomic>
#include <sstream>
#include <cstring>

#include "../../../../ext/minhook/minhook.h"
#include "../../../../ext/imgui/imgui.h"
#include "../../../../ext/imgui/imgui_impl_win32.h"
#include "../../../../ext/imgui/imgui_impl_opengl2.h"
#include "../../util/logger.h"
#include "../../util/trimmer.h"
#include "../../../../ext/fonts/inter_medium.h"
#include "../../../../ext/fonts/inter_bold.h"
#include "../../../../ext/fonts/fa_solid.h"
#include "../../../../ext/fonts/jetbrainsmono.h"

#include "../../base.h"

std::atomic<bool> g_imguiSetupDone{ false };
std::atomic_flag clipCursor = ATOMIC_FLAG_INIT;
RECT originalClip;

struct RenderHookScope
{
	RenderHookScope() { Base::RenderHookDepth.fetch_add(1, std::memory_order_acq_rel); }
	~RenderHookScope() { Base::RenderHookDepth.fetch_sub(1, std::memory_order_acq_rel); }
};

typedef bool(__stdcall* template_wglSwapBuffers) (HDC hdc);
template_wglSwapBuffers original_wglSwapBuffers;
bool __stdcall hook_wglSwapBuffers(_In_ HDC hdc)
{
	// Kill() only sets a flag; ImGui/GL teardown must happen here on the
	// render thread (GL contexts are thread-affine). Capture the trampoline
	// first — teardown may unhook and null it.
	template_wglSwapBuffers origSwap = original_wglSwapBuffers;
	if (Menu::ProcessPendingGLTeardown())
	{
		if (origSwap)
			return origSwap(hdc);
		return FALSE;
	}

	if (Base::ShuttingDown.load(std::memory_order_acquire) || !Base::IsRunning())
	{
		if (originalClip.right > originalClip.left && originalClip.bottom > originalClip.top)
		{
			ClipCursor(&originalClip);
			clipCursor.clear();
		}
		else
		{
			ClipCursor(NULL);
		}
		if (original_wglSwapBuffers)
			return original_wglSwapBuffers(hdc);
		return FALSE;
	}

	RenderHookScope hookScope;

	// handling fullscreen context switching before we set the new hwnd, so we can compare them
	
	// if the cached hwnd isnt equal to the current one
	// info: window handles change when you enter/exit fullscreen
	if (Menu::HandleWindow != WindowFromDC(hdc) && Menu::Initialized)
	{
		// Window handle changed (e.g. fullscreen toggle). The old drawable and
		// its pixel format may no longer be valid, so we must rebuild our
		// dedicated render context for the NEW drawable.
		Menu::HandleWindow = WindowFromDC(hdc);
		Menu::HandleDeviceContext = hdc;
		Menu::OriginalGLContext = wglGetCurrentContext();

		// Tear down the ImGui backends bound to the previous context.
		ImGui_ImplOpenGL2_Shutdown();
		ImGui_ImplWin32_Shutdown();

		// Recreate our overlay context for the new drawable. Delete the old one
		// first so we do not leak a GL context on every window change. (The old
		// code created an anonymous context here and leaked it, AND it
		// re-initialised the ImGui font texture in that throwaway context - so
		// the overlay lost its texture after a resize, producing broken/blank
		// frames. Rebuilding MenuGLContext and initialising ImGui INTO it fixes
		// both issues.)
		if (Menu::MenuGLContext)
		{
			wglDeleteContext(Menu::MenuGLContext);
			Menu::MenuGLContext = nullptr;
		}
		Menu::MenuGLContext = wglCreateContext(hdc);

		if (Menu::MenuGLContext && wglMakeCurrent(hdc, Menu::MenuGLContext))
		{
			ImGui_ImplWin32_Init(Menu::HandleWindow);
			ImGui_ImplOpenGL2_Init();
			wglMakeCurrent(hdc, Menu::OriginalGLContext);
		}

		// set wndproc
		Menu::Hook_wndProc();

		// end detour
		return original_wglSwapBuffers(hdc);
	}

	Menu::HandleDeviceContext = hdc;
	Menu::HandleWindow = WindowFromDC(hdc);
	Menu::OriginalGLContext = wglGetCurrentContext();

	if (!g_imguiSetupDone.exchange(true, std::memory_order_acq_rel))
	{
		Logger::Info("Menu", "First wglSwapBuffers intercept — creating overlay GL context + ImGui");
		Menu::Hook_wndProc();
		Menu::SetupImgui();
	}

	// If we cannot bind our overlay context this frame (transient during
	// resize / focus changes), present the game's frame untouched instead of
	// rendering into an undefined context - this is what produced the brief
	// white/garbage flashes.
	if (!Menu::MenuGLContext || !wglMakeCurrent(Menu::HandleDeviceContext, Menu::MenuGLContext))
	{
		return original_wglSwapBuffers(hdc);
	}

	ImGui_ImplOpenGL2_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	bool pushedFont = false;
	if (Menu::Font && Menu::Font->IsLoaded())
	{
		ImGui::PushFont(Menu::Font);
		pushedFont = true;
	}

	// Always drive Menu::RenderMenu() so the ClickGUI close-animation can play.
	// We only flip cursor clipping based on the *open* flag.
	if (Menu::Open)
	{
		if (clipCursor.test_and_set()) GetClipCursor(&originalClip);
		ClipCursor(NULL);
	}
	else
	{
		if (originalClip.right > originalClip.left && originalClip.bottom > originalClip.top)
		{
			ClipCursor(&originalClip);
			clipCursor.clear();
		}
	}
	Menu::RenderMenu();

	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
	ImGui::Begin("Overlay", nullptr,
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoInputs |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoBackground);

	Base::RenderLoop();

	if (pushedFont)
	{
		ImGui::PopFont();
	}
	ImGui::End();

	ImGui::EndFrame();
	ImGui::Render();
	ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

	wglMakeCurrent(Menu::HandleDeviceContext, Menu::OriginalGLContext);
	return original_wglSwapBuffers(hdc);
}

static LPVOID g_wglSwapBuffersTarget = nullptr;
static LPVOID g_wglGetProcAddressTarget = nullptr;
static std::atomic<bool> g_wglSwapHookInstalled{ false };

typedef PROC(WINAPI* wglGetProcAddress_fn)(LPCSTR);
static wglGetProcAddress_fn original_wglGetProcAddress = nullptr;

static PROC WINAPI hook_wglGetProcAddress(LPCSTR lpszProc)
{
	PROC proc = original_wglGetProcAddress(lpszProc);
	// LWJGL / Lunar resolve wglSwapBuffers via wglGetProcAddress — hook export alone misses them.
	if (lpszProc && _stricmp(lpszProc, "wglSwapBuffers") == 0)
		return reinterpret_cast<PROC>(&hook_wglSwapBuffers);
	return proc;
}

static bool InstallWglSwapHook()
{
	if (g_wglSwapHookInstalled.load(std::memory_order_acquire))
		return original_wglSwapBuffers != nullptr;

	HMODULE opengl32 = GetModuleHandleA("opengl32.dll");
	if (!opengl32)
		opengl32 = LoadLibraryA("opengl32.dll");
	if (!opengl32)
		return false;

	g_wglSwapBuffersTarget = (LPVOID)GetProcAddress(opengl32, "wglSwapBuffers");
	g_wglGetProcAddressTarget = (LPVOID)GetProcAddress(opengl32, "wglGetProcAddress");
	if (!g_wglSwapBuffersTarget || !g_wglGetProcAddressTarget)
	{
		Logger::Error("Menu", "Render hook: wglSwapBuffers/wglGetProcAddress export missing");
		return false;
	}

	MH_STATUS swapCreate = MH_CreateHook(
		g_wglSwapBuffersTarget,
		(LPVOID)hook_wglSwapBuffers,
		(LPVOID*)&original_wglSwapBuffers);
	if (swapCreate != MH_OK && swapCreate != MH_ERROR_ALREADY_CREATED)
	{
		Logger::Error("Menu", "Render hook: MH_CreateHook(wglSwapBuffers) failed (" + std::to_string((int)swapCreate) + ")");
		return false;
	}
	// ALREADY_CREATED does not fill in the trampoline, so proceeding would leave
	// the detour calling through a null original and hard-crash on present.
	if (!original_wglSwapBuffers)
	{
		Logger::Error("Menu", "Render hook: wglSwapBuffers trampoline unavailable — aborting install");
		MH_RemoveHook(g_wglSwapBuffersTarget);
		return false;
	}

	MH_STATUS gpaCreate = MH_CreateHook(
		g_wglGetProcAddressTarget,
		(LPVOID)hook_wglGetProcAddress,
		(LPVOID*)&original_wglGetProcAddress);
	if (gpaCreate != MH_OK && gpaCreate != MH_ERROR_ALREADY_CREATED)
	{
		Logger::Error("Menu", "Render hook: MH_CreateHook(wglGetProcAddress) failed (" + std::to_string((int)gpaCreate) + ")");
		return false;
	}
	if (!original_wglGetProcAddress)
	{
		Logger::Error("Menu", "Render hook: wglGetProcAddress trampoline unavailable — aborting install");
		MH_RemoveHook(g_wglGetProcAddressTarget);
		MH_RemoveHook(g_wglSwapBuffersTarget);
		return false;
	}

	MH_STATUS swapEnable = MH_EnableHook(g_wglSwapBuffersTarget);
	MH_STATUS gpaEnable = MH_EnableHook(g_wglGetProcAddressTarget);
	if (swapEnable != MH_OK || gpaEnable != MH_OK)
	{
		Logger::Error("Menu", "Render hook: MH_EnableHook failed (swap=" + std::to_string((int)swapEnable)
			+ " gpa=" + std::to_string((int)gpaEnable) + ")");
		return false;
	}

	g_wglSwapHookInstalled.store(true, std::memory_order_release);
	{
		std::ostringstream oss;
		oss << "Render hooks active | wglSwapBuffers=0x" << std::hex << (uintptr_t)g_wglSwapBuffersTarget
		    << " wglGetProcAddress=0x" << (uintptr_t)g_wglGetProcAddressTarget;
		Logger::Info("Menu", oss.str());
	}
	return true;
}

void Menu::Hook_wglSwapBuffers()
{
	InstallWglSwapHook();
}

void Menu::EnsureRenderHooks()
{
	static int s_attempts = 0;
	if (g_wglSwapHookInstalled.load(std::memory_order_acquire))
		return;

	if (InstallWglSwapHook())
		return;

	if (++s_attempts == 1 || (s_attempts % 200) == 0)
		Logger::Warn("Menu", "Waiting for opengl32.dll to load render hooks (attempt " + std::to_string(s_attempts) + ")");
}

bool Menu::RenderHooksInstalled()
{
	return g_wglSwapHookInstalled.load(std::memory_order_acquire);
}

void Menu::Unhook_wglSwapBuffers()
{	
	if (g_wglGetProcAddressTarget)
	{
		const MH_STATUS disable = MH_DisableHook(g_wglGetProcAddressTarget);
		const MH_STATUS remove = MH_RemoveHook(g_wglGetProcAddressTarget);
		if (disable != MH_OK || remove != MH_OK)
			Logger::Warn("Menu", "Unhook wglGetProcAddress: disable=" + std::to_string((int)disable)
				+ " remove=" + std::to_string((int)remove));
		g_wglGetProcAddressTarget = nullptr;
	}
	if (g_wglSwapBuffersTarget)
	{
		const MH_STATUS disable = MH_DisableHook(g_wglSwapBuffersTarget);
		const MH_STATUS remove = MH_RemoveHook(g_wglSwapBuffersTarget);
		if (disable != MH_OK || remove != MH_OK)
			Logger::Warn("Menu", "Unhook wglSwapBuffers: disable=" + std::to_string((int)disable)
				+ " remove=" + std::to_string((int)remove));
		g_wglSwapBuffersTarget = nullptr;
	}
	original_wglSwapBuffers = nullptr;
	original_wglGetProcAddress = nullptr;
	g_wglSwapHookInstalled.store(false, std::memory_order_release);
}

void Menu::ResetHookSetupState()
{
	g_imguiSetupDone.store(false, std::memory_order_release);
}

void Menu::SetupImgui()
{
	Menu::MenuGLContext = wglCreateContext(Menu::HandleDeviceContext);
	if (!Menu::MenuGLContext)
	{
		Logger::Error("Menu", "SetupImgui: wglCreateContext failed — overlay disabled this frame");
		return;
	}
	if (!wglMakeCurrent(Menu::HandleDeviceContext, Menu::MenuGLContext))
	{
		Logger::Error("Menu", "SetupImgui: wglMakeCurrent failed — overlay disabled this frame");
		wglDeleteContext(Menu::MenuGLContext);
		Menu::MenuGLContext = nullptr;
		// Allow the hook to retry setup on a later frame.
		g_imguiSetupDone.store(false, std::memory_order_release);
		return;
	}

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();

	GLint m_viewport[4];
	glGetIntegerv(GL_VIEWPORT, m_viewport);

	glOrtho(0, m_viewport[2], m_viewport[3], 0, 1, -1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glClearColor(0, 0, 0, 0);

	Menu::CurrentImGuiContext = ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	// Disable imgui.ini; window/module layout is persisted via ProfileManager.
	io.IniFilename = nullptr;
	io.Fonts->AddFontDefault();

	// Latin + Latin Extended-A so Turkish glyphs (ğüşıöç ĞÜŞİÖÇ) render in ImGui panels.
	static const ImWchar kLatinExtRanges[] = {
		0x0020, 0x00FF, // Basic Latin + Latin-1 Supplement
		0x0100, 0x017F, // Latin Extended-A
		0,
	};

	// Font Awesome 6 solid glyphs live in the Private Use Area.
	static const ImWchar kIconRanges[] = {
		0xF000, 0xF8FF,
		0,
	};

	ImFontConfig fontCfg;
	fontCfg.FontDataOwnedByAtlas = false;
	fontCfg.GlyphRanges = kLatinExtRanges;
	fontCfg.OversampleH = 3;
	fontCfg.OversampleV = 1;
	Menu::Font = io.Fonts->AddFontFromMemoryTTF((void*)inter_medium_font, sizeof(inter_medium_font), 16.0f, &fontCfg);
	Menu::FontBold = io.Fonts->AddFontFromMemoryTTF((void*)inter_bold_font, sizeof(inter_bold_font), 16.0f, &fontCfg);
	Menu::FontMono = io.Fonts->AddFontFromMemoryTTF((void*)jetbrainsmono, sizeof(jetbrainsmono), 16.0f, &fontCfg, kLatinExtRanges);

	// Icon font: crisp, tintable vector glyphs for the whole GUI.
	ImFontConfig iconCfg;
	iconCfg.FontDataOwnedByAtlas = false;
	iconCfg.GlyphRanges = kIconRanges;
	iconCfg.OversampleH = 3;
	iconCfg.OversampleV = 1;
	Menu::FontIcon = io.Fonts->AddFontFromMemoryTTF((void*)fa_solid_font, sizeof(fa_solid_font), 16.0f, &iconCfg);

	ImVec4* colors = ImGui::GetStyle().Colors;
	colors[ImGuiCol_Text] = ImVec4(1.f, 1.f, 1.f, 1.00f);
	colors[ImGuiCol_TextDisabled] = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
	colors[ImGuiCol_WindowBg] = ImVec4(0.045f, 0.045f, 0.045f, 0.90f);
	colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
	colors[ImGuiCol_Border] = ImVec4(0.00f, 0.00f, 0.00f, 0.50f);
	colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_FrameBg] = ImVec4(0.045f, 0.045f, 0.045f, 0.90f);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.045f, 0.045f, 0.045f, 0.90f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.045f, 0.045f, 0.045f, 0.90f);
	colors[ImGuiCol_TitleBg] = ImVec4(0.065f, 0.065f, 0.065f, 0.90f);
	colors[ImGuiCol_TitleBgActive] = ImVec4(0.065f, 0.065f, 0.065f, 0.90f);
	colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.065f, 0.065f, 0.065f, 0.90f);
	colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
	colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.00f);
	colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
	colors[ImGuiCol_CheckMark] = ImVec4(0.56f, 0.10f, 0.10f, 1.00f);
	colors[ImGuiCol_SliderGrab] = ImVec4(0.4f, 0.7f, 0.7f, 1.0f);
	colors[ImGuiCol_SliderGrabActive] = ImVec4(0.5f, 0.89f, 0.89f, 1.00f);
	colors[ImGuiCol_Button] = ImVec4(0.19f, 0.19f, 0.19f, 1.f);
	colors[ImGuiCol_ButtonHovered] = ImVec4(0.17f, 0.17f, 0.17f, 1.00f);
	colors[ImGuiCol_ButtonActive] = ImVec4(0.3f, 0.3f, 0.3f, 1.00f);
	colors[ImGuiCol_Header] = ImVec4(0.33f, 0.35f, 0.36f, 0.53f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.f, 0.44f, 0.44f, 0.67f);
	colors[ImGuiCol_HeaderActive] = ImVec4(0.47f, 0.47f, 0.47f, 0.67f);
	colors[ImGuiCol_Separator] = ImVec4(0.32f, 0.32f, 0.32f, 0.3f);
	colors[ImGuiCol_SeparatorHovered] = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
	colors[ImGuiCol_SeparatorActive] = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
	colors[ImGuiCol_ResizeGrip] = ImVec4(1.00f, 1.00f, 1.00f, 0.85f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.60f);
	colors[ImGuiCol_ResizeGripActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.90f);
	colors[ImGuiCol_Tab] = ImVec4(0.07f, 0.07f, 0.07f, 0.51f);
	colors[ImGuiCol_TabHovered] = ImVec4(0, 0.23f, 0.23f, 0.67f);
	colors[ImGuiCol_TabActive] = ImVec4(0.19f, 0.19f, 0.19f, 0.57f);
	colors[ImGuiCol_TabUnfocused] = ImVec4(0.05f, 0.05f, 0.05f, 0.90f);
	colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.13f, 0.13f, 0.13f, 0.74f);
	colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
	colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
	colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
	colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
	colors[ImGuiCol_TableHeaderBg] = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
	colors[ImGuiCol_TableBorderStrong] = ImVec4(0.31f, 0.31f, 0.35f, 1.00f);
	colors[ImGuiCol_TableBorderLight] = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
	colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.07f);
	colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
	colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
	colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
	colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);

	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 10.0f;
	style.WindowBorderSize = 0;
	style.WindowPadding = ImVec2(0,0);

	ImGui_ImplWin32_Init(Menu::HandleWindow);
	ImGui_ImplOpenGL2_Init();


	Menu::Initialized = true;
}