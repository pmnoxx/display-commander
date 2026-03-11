// Standalone UIs: RunStandaloneSettingsUI (no-ReShade settings), RunStandaloneGamesOnlyUI (Games tab only,
// exe/Launcher). (default "."). Uses a second ImGui build in namespace ImGuiStandalone (via compile define
// ImGui=ImGuiStandalone) and ImDrawList=ImDrawListStandalone to avoid symbol clash with ReShade's ImGui/ImDrawList used
// in-game.

#include "config/display_commander_config.hpp"
#include "display/display_cache.hpp"
#include "globals.hpp"
#include "settings/main_tab_settings.hpp"
#include "standalone_ui_settings_bridge.hpp"
#include "ui/imgui_wrapper_standalone.hpp"
#include "ui/new_ui/addons_tab.hpp"
#include "ui/new_ui/advanced_tab.hpp"
#include "ui/new_ui/experimental_tab.hpp"
#include "ui/new_ui/games_tab.hpp"
#include "ui/new_ui/hotkeys_tab.hpp"
#include "ui/new_ui/main_new_tab_standalone.hpp"
#include "ui/new_ui/performance_tab.hpp"
#include "ui/new_ui/vulkan_tab.hpp"
#include "ui/nvidia_profile_tab_shared.hpp"
#include "utils/general_utils.hpp"
#include "utils/reshade_load_path.hpp"
#include "utils/steam_library.hpp"
#include "utils/version_check.hpp"
#include "version.hpp"
#include "widgets/remapping_widget/remapping_widget.hpp"
#include "widgets/xinput_widget/xinput_widget.hpp"

#include <windows.h>

#include <GL/gl.h>
#include <shellapi.h>
#include <tlhelp32.h>

// reshade
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_win32.h"
#include "imgui.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifndef IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
#define IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
#endif

// opengl32.lib not linked: gl/wgl symbols from opengl32_proxy (forwards to system opengl32)

static HDC g_hDC = nullptr;
static HGLRC g_hGLRC = nullptr;
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;

static bool CreateContextOpenGL(HWND hWnd);
static void CleanupContextOpenGL(HWND hWnd);
static void SaveLauncherWindowPosition(HWND hwnd);
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Build default font + merge Japanese glyphs from a Windows system font so the launcher can display Japanese.
static void BuildStandaloneFontsWithJapanese(display_commander::ui::ImGuiWrapperStandalone& gui) {
    ImGuiIO* io = gui.GetIOForFontSetup();
    if (io == nullptr) return;
    io->Fonts->AddFontDefault();

    wchar_t win_dir[MAX_PATH] = {};
    if (GetWindowsDirectoryW(win_dir, (UINT)std::size(win_dir)) == 0) return;
    std::wstring fonts_dir = std::wstring(win_dir) + L"\\Fonts\\";

    static const wchar_t* jp_font_names[] = {L"meiryo.ttc", L"msgothic.ttc", L"yugothm.ttc"};
    for (const wchar_t* name : jp_font_names) {
        std::wstring path_w = fonts_dir + name;
        if (GetFileAttributesW(path_w.c_str()) == INVALID_FILE_ATTRIBUTES) continue;

        std::string path_utf8;
        int need = WideCharToMultiByte(CP_UTF8, 0, path_w.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (need <= 0) continue;
        path_utf8.resize(need);
        WideCharToMultiByte(CP_UTF8, 0, path_w.c_str(), -1, path_utf8.data(), need, nullptr, nullptr);

        ImFontConfig merge_cfg = {};
        merge_cfg.MergeMode = true;
        merge_cfg.GlyphRanges = io->Fonts->GetGlyphRangesJapanese();
        if (io->Fonts->AddFontFromFileTTF(path_utf8.c_str(), 18.0f, &merge_cfg, nullptr) != nullptr) break;
    }
}

// Central Reshade folder: %LOCALAPPDATA%\Programs\Display_Commander\Reshade
static std::wstring GetCentralReshadeDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, (DWORD)std::size(buf));
    if (n == 0 || n >= (DWORD)std::size(buf)) return {};
    std::wstring path = buf;
    if (!path.empty() && path.back() != L'\\') path += L'\\';
    path += L"Programs\\Display_Commander\\Reshade";
    return path;
}

// Get file version string (e.g. "1.2.3.4") or empty if not present / no version.
// Prefer 4-part version from VS_FIXEDFILEINFO so build number is always shown when present.
static std::string GetFileVersionStringUtf8(const std::wstring& filePath) {
    DWORD verHandle = 0;
    DWORD size = GetFileVersionInfoSizeW(filePath.c_str(), &verHandle);
    if (size == 0) return {};
    std::vector<char> buf(size);
    if (!GetFileVersionInfoW(filePath.c_str(), 0, size, buf.data())) return {};
    void* pFixed = nullptr;
    UINT fixedLen = 0;
    if (VerQueryValueW(buf.data(), L"\\", &pFixed, &fixedLen) && pFixed && fixedLen >= sizeof(VS_FIXEDFILEINFO)) {
        const VS_FIXEDFILEINFO* pInfo = static_cast<const VS_FIXEDFILEINFO*>(pFixed);
        if (pInfo->dwSignature == 0xFEEF04BD) {
            unsigned major = HIWORD(pInfo->dwFileVersionMS);
            unsigned minor = LOWORD(pInfo->dwFileVersionMS);
            unsigned patch = HIWORD(pInfo->dwFileVersionLS);
            unsigned build = LOWORD(pInfo->dwFileVersionLS);
            char out[32];
            snprintf(out, sizeof(out), "%u.%u.%u.%u", major, minor, patch, build);
            return std::string(out);
        }
    }
    struct LANGANDCODEPAGE {
        WORD wLanguage;
        WORD wCodePage;
    };
    LANGANDCODEPAGE* pTrans = nullptr;
    UINT transLen = 0;
    if (!VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation", (void**)&pTrans, &transLen) || !pTrans
        || transLen < sizeof(LANGANDCODEPAGE))
        return {};
    wchar_t subBlock[64];
    swprintf_s(subBlock, L"\\StringFileInfo\\%04x%04x\\FileVersion", pTrans[0].wLanguage, pTrans[0].wCodePage);
    void* pBlock = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(buf.data(), subBlock, &pBlock, &len) || !pBlock || len == 0) return {};
    const wchar_t* ver = static_cast<const wchar_t*>(pBlock);
    int need = WideCharToMultiByte(CP_UTF8, 0, ver, (int)(len / sizeof(wchar_t)), nullptr, 0, nullptr, nullptr);
    if (need <= 0) return {};
    std::string out(static_cast<size_t>(need), 0);
    WideCharToMultiByte(CP_UTF8, 0, ver, (int)(len / sizeof(wchar_t)), &out[0], need, nullptr, nullptr);
    return out;
}

// ReShade update: download and override DLLs in local + central. Version from GitHub tags or dropdown.
static const char* const kReshadeUpdateUrlLatest = "https://reshade.me/downloads/ReShade_Setup_Addon.exe";

static std::atomic<bool> s_reshadeUpdateInProgress{false};

// Newest available version (ReShade from reshade.me, DC from GitHub releases/tags/latest_debug). Fetched once in background.
static std::atomic<bool> s_latestVersionsFetchStarted{false};
static std::atomic<bool> s_latestVersionsFetchDone{false};
static std::atomic<std::string*> s_latestReShadeVersion{nullptr};
static std::atomic<std::string*> s_latestDcVersion{nullptr};
static std::string s_reshadeUpdateResult;
static std::string s_gameDetailsReshadeResult;  // result of install action from Game Details popup (DC as proxy)

struct ReshadeUpdateParams {
    std::wstring addonDir;
    std::wstring centralDir;
    std::string selectedVersion;  // e.g. "6.7.1"
    bool forGameDetails = false;  // if true, result goes to s_gameDetailsReshadeResult
    // For proxy-only install (game folder): install as dxgi.dll / d3d11.dll / etc., never ReShade64/32 by name.
    std::string proxyName;    // e.g. "dxgi" (empty = legacy: copy both cores to addonDir)
    std::wstring gameDir;     // target game folder when proxyName non-empty
    bool gameIs64bit = true;  // which core to copy as proxy (ReShade64 vs ReShade32)
};

static DWORD WINAPI ReshadeUpdateWorker(LPVOID param) {
    std::unique_ptr<ReshadeUpdateParams> params(static_cast<ReshadeUpdateParams*>(param));
    const std::wstring& addonDir = params->addonDir;
    const std::wstring& centralDir = params->centralDir;

    auto setResult = [&params](const std::string& text) {
        if (params->forGameDetails)
            s_gameDetailsReshadeResult = text;
        else
            s_reshadeUpdateResult = text;
    };
    std::filesystem::path tempDir;
    {
        wchar_t tempPath[MAX_PATH];
        if (GetTempPathW((DWORD)std::size(tempPath), tempPath) == 0) {
            setResult("Install failed: could not get temp path.");
            s_reshadeUpdateInProgress = false;
            return 1;
        }
        tempDir = std::filesystem::path(tempPath) / L"dc_reshade_update";
        std::error_code ec;
        std::filesystem::create_directories(tempDir, ec);
        if (ec) {
            setResult("Install failed: could not create temp dir.");
            s_reshadeUpdateInProgress = false;
            return 1;
        }
    }

    std::filesystem::path archivePath = tempDir / "ReShade_Setup_Addon.exe";
    bool downloaded = false;
    if (!params->selectedVersion.empty() && params->selectedVersion != "latest") {
        std::string versionedUrl =
            "https://reshade.me/downloads/ReShade_Setup_" + params->selectedVersion + "_Addon.exe";
        downloaded = display_commander::utils::version_check::DownloadBinaryFromUrl(versionedUrl, archivePath);
    }
    if (!downloaded) {
        downloaded =
            display_commander::utils::version_check::DownloadBinaryFromUrl(kReshadeUpdateUrlLatest, archivePath);
    }
    if (!downloaded) {
        setResult("Install failed: download failed (check URL or network).");
        s_reshadeUpdateInProgress = false;
        return 1;
    }

    std::wstring archiveW = archivePath.wstring();
    std::wstring cmdLine;
    cmdLine.reserve(archiveW.size() + 64);
    cmdLine = L"tar.exe -xf \"";
    cmdLine += archiveW;
    cmdLine += L"\" ReShade64.dll ReShade32.dll";

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, &cmdLine[0], nullptr, nullptr, FALSE, 0, nullptr, tempDir.c_str(), &si, &pi)) {
        setResult("Install failed: tar extract failed (need Windows 10+ tar).");
        s_reshadeUpdateInProgress = false;
        return 1;
    }
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, 60000);
    CloseHandle(pi.hProcess);

    std::filesystem::path extracted64 = tempDir / "ReShade64.dll";
    std::filesystem::path extracted32 = tempDir / "ReShade32.dll";
    if (!std::filesystem::exists(extracted64) || !std::filesystem::exists(extracted32)) {
        setResult("Install failed: extraction did not produce DLLs.");
        s_reshadeUpdateInProgress = false;
        return 1;
    }

    auto copyTo = [](const std::filesystem::path& src64, const std::filesystem::path& src32,
                     const std::wstring& destDir) -> bool {
        if (destDir.empty()) return true;
        std::filesystem::path dest(destDir);
        std::error_code ec;
        std::filesystem::copy_file(src64, dest / "ReShade64.dll", std::filesystem::copy_options::overwrite_existing,
                                   ec);
        if (ec) return false;
        std::filesystem::copy_file(src32, dest / "ReShade32.dll", std::filesystem::copy_options::overwrite_existing,
                                   ec);
        return !ec;
    };

    bool proxyOnlyInstall = params->forGameDetails && !params->proxyName.empty() && !params->gameDir.empty();

    if (proxyOnlyInstall) {
        // Install ReShade only as proxy (dxgi.dll / d3d11.dll / etc.) in game folder; cores go to central only.
        if (!centralDir.empty()) {
            if (!copyTo(extracted64, extracted32, centralDir)) {
                setResult("Install failed: could not write to central ReShade folder.");
                s_reshadeUpdateInProgress = false;
                return 1;
            }
        }
        std::wstring proxyDll;
        for (char c : params->proxyName) proxyDll += (wchar_t)(unsigned char)c;
        proxyDll += L".dll";
        const std::filesystem::path& coreSrc = params->gameIs64bit ? extracted64 : extracted32;
        std::filesystem::path gameDest(params->gameDir);
        gameDest /= proxyDll;
        std::error_code ec;
        std::filesystem::copy_file(coreSrc, gameDest, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            setResult("Install failed: could not write proxy to game folder.");
            s_reshadeUpdateInProgress = false;
            return 1;
        }
    } else {
        // Copy ReShade cores to central only; do not copy to game directory.
        if (centralDir.empty()) {
            setResult("Install failed: central ReShade folder not set (LOCALAPPDATA).");
            s_reshadeUpdateInProgress = false;
            return 1;
        }
        if (!copyTo(extracted64, extracted32, centralDir)) {
            setResult("Install failed: could not write to central ReShade folder.");
            s_reshadeUpdateInProgress = false;
            return 1;
        }
    }

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
    std::string msg;
    if (params->forGameDetails) {
        if (proxyOnlyInstall)
            msg = "ReShade installed as " + params->proxyName + ".dll (" + (params->gameIs64bit ? "64-bit" : "32-bit")
                  + ").";
        else
            msg = (params->selectedVersion.empty() || params->selectedVersion == "latest")
                      ? "ReShade installed (latest)."
                      : "ReShade installed: " + params->selectedVersion + ".";
        s_gameDetailsReshadeResult = msg;
    } else {
        s_reshadeUpdateResult = (params->selectedVersion.empty() || params->selectedVersion == "latest")
                                    ? "ReShade updated (latest)."
                                    : "ReShade updated to " + params->selectedVersion + ".";
    }
    s_reshadeUpdateInProgress = false;
    return 0;
}

namespace {
constexpr int kStandaloneSettingsWindowDefaultWidth = 950;
constexpr int kStandaloneSettingsWindowDefaultHeight = 1600;
}  // namespace

void RunStandaloneSettingsUI(HINSTANCE hInst) {
    std::this_thread::sleep_for(std::chrono::seconds(1));

    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = (HINSTANCE)hInst;
    wc.lpszClassName = L"DisplayCommanderSettingsUI";
    if (!RegisterClassExW(&wc)) return;

    // Title: "Display Commander - <game window title> vX.Y.Z" when g_last_swapchain_hwnd has a title; else "Display Commander - Settings (No ReShade) vX.Y.Z"
    std::wstring titleW = L"Display Commander - ";
    HWND game_hwnd = g_last_swapchain_hwnd.load(std::memory_order_acquire);
    if (game_hwnd != nullptr) {
        wchar_t game_title_buf[256] = {};
        if (GetWindowTextW(game_hwnd, game_title_buf, 256) > 0 && game_title_buf[0] != L'\0') {
            titleW += game_title_buf;
        } else {
            titleW += L"Settings (No ReShade)";
        }
    } else {
        titleW += L"Settings (No ReShade)";
    }
    titleW += L" v";
    std::string ver(DISPLAY_COMMANDER_VERSION_STRING);
    int verLen = MultiByteToWideChar(CP_UTF8, 0, ver.c_str(), (int)ver.size() + 1, nullptr, 0);
    if (verLen > 0) {
        size_t prev = titleW.size();
        titleW.resize(prev + (size_t)verLen, 0);
        MultiByteToWideChar(CP_UTF8, 0, ver.c_str(), (int)ver.size() + 1, &titleW[prev], verLen);
        if (titleW.back() == L'\0') titleW.pop_back();
    }

    HWND hwnd = standalone_ui_settings::CreateWindowW_Direct(
        wc.lpszClassName, titleW.c_str(), WS_OVERLAPPEDWINDOW, 100,
        100, kStandaloneSettingsWindowDefaultWidth, kStandaloneSettingsWindowDefaultHeight, nullptr, nullptr,
        (HINSTANCE)hInst, nullptr);
    if (!hwnd) {
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return;
    }
    standalone_ui_settings::SetStandaloneUiHwnd(reinterpret_cast<uintptr_t>(hwnd));

    if (!CreateContextOpenGL(hwnd)) {
        standalone_ui_settings::SetStandaloneUiHwnd(0);
        CleanupContextOpenGL(hwnd);
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    display_commander::ui::ImGuiWrapperStandalone gui;
    gui.CreateContext();
    gui.SetConfigFlags(static_cast<uint32_t>(ImGuiConfigFlags_NavEnableKeyboard));
    gui.StyleColorsDark();
    BuildStandaloneFontsWithJapanese(gui);
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplOpenGL3_Init();

    static const char* fps_limiter_items[] = {"Default", "NVIDIA Reflex (low latency)", "Disabled",
                                              "Sync to Display Refresh Rate (fraction of monitor refresh rate)"};
    static const int fps_limiter_num = 4;

    std::wstring lastWindowTitle;  // cache so we only SetWindowTextW when title changes

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        // Keep window title in sync with current game window title (g_last_swapchain_hwnd)
        std::wstring currentTitle = L"Display Commander - ";
        HWND game_hwnd = g_last_swapchain_hwnd.load(std::memory_order_acquire);
        if (game_hwnd != nullptr) {
            wchar_t game_title_buf[256] = {};
            if (GetWindowTextW(game_hwnd, game_title_buf, 256) > 0 && game_title_buf[0] != L'\0') {
                currentTitle += game_title_buf;
            } else {
                currentTitle += L"Settings (No ReShade)";
            }
        } else {
            currentTitle += L"Settings (No ReShade)";
        }
        currentTitle += L" v";
        std::string ver(DISPLAY_COMMANDER_VERSION_STRING);
        int verLen = MultiByteToWideChar(CP_UTF8, 0, ver.c_str(), (int)ver.size() + 1, nullptr, 0);
        if (verLen > 0) {
            size_t prev = currentTitle.size();
            currentTitle.resize(prev + (size_t)verLen, 0);
            MultiByteToWideChar(CP_UTF8, 0, ver.c_str(), (int)ver.size() + 1, &currentTitle[prev], verLen);
            if (currentTitle.back() == L'\0') currentTitle.pop_back();
        }
        if (currentTitle != lastWindowTitle) {
            SetWindowTextW(hwnd, currentTitle.c_str());
            lastWindowTitle = std::move(currentTitle);
        }

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            glViewport(0, 0, (GLsizei)g_ResizeWidth, (GLsizei)g_ResizeHeight);
            g_ResizeWidth = g_ResizeHeight = 0;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplWin32_NewFrame();

        gui.NewFrame();
        gui.SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
        gui.SetNextWindowSize(ImVec2(440, 0), ImGuiCond_FirstUseEver);
        if (gui.Begin("Display Commander - Settings (No ReShade)", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (gui.BeginTabBar("NoReshadeSettingsTabs", 0)) {
                if (gui.BeginTabItem("Main", nullptr, 0)) {
                    ui::new_ui::InitMainNewTab();
                    ui::new_ui::DrawMainNewTab(ui::new_ui::GetGraphicsApiFromLastDeviceApi(), gui);
                    gui.EndTabItem();
                }
                if (gui.BeginTabItem("Games", nullptr, 0)) {
                    ui::new_ui::DrawGamesTab(gui);
                    gui.EndTabItem();
                }
                if (gui.BeginTabItem("Advanced", nullptr, 0)) {
                    ui::new_ui::DrawAdvancedTab(ui::new_ui::GetGraphicsApiFromLastDeviceApi(), gui);
                    gui.EndTabItem();
                }
                if (gui.BeginTabItem("Hotkeys", nullptr, 0)) {
                    ui::new_ui::DrawHotkeysTab(gui);
                    gui.EndTabItem();
                }
                if (gui.BeginTabItem("Controller", nullptr, 0)) {
                    display_commander::widgets::xinput_widget::InitializeXInputWidget();
                    display_commander::widgets::remapping_widget::InitializeRemappingWidget();
                    display_commander::widgets::xinput_widget::DrawXInputWidget(gui);
                    gui.Spacing();
                    display_commander::widgets::remapping_widget::DrawRemappingWidget(gui);
                    gui.EndTabItem();
                }
                if (gui.BeginTabItem("Performance", nullptr, 0)) {
                    ui::new_ui::DrawPerformanceTab(gui);
                    gui.EndTabItem();
                }
                if (gui.BeginTabItem("Performance Overlay", nullptr, 0)) {
                    ui::new_ui::DrawPerformanceOverlayContent(gui, ui::new_ui::GetGraphicsApiFromLastDeviceApi(), true);
                    gui.EndTabItem();
                }
                if (gui.BeginTabItem("Vulkan (Experimental)", nullptr, 0)) {
                    ui::new_ui::DrawVulkanTab(gui);
                    gui.EndTabItem();
                }
                if (gui.BeginTabItem("ReShade", nullptr, 0)) {
                    ui::new_ui::DrawAddonsTab(gui);
                    gui.EndTabItem();
                }
                if (gui.BeginTabItem("NVIDIA Profile", nullptr, 0)) {
                    static bool s_noreshadeShowAdvancedProfile = false;
                    display_commander::ui::DrawNvidiaProfileTab(ui::new_ui::GetGraphicsApiFromLastDeviceApi(), gui,
                                                                &s_noreshadeShowAdvancedProfile);
                    gui.EndTabItem();
                }
                if (gui.BeginTabItem("Debug", nullptr, 0)) {
                    ui::new_ui::DrawExperimentalTab(gui, nullptr);
                    gui.EndTabItem();
                }
                gui.EndTabBar();
            }
        }
        gui.End();

        gui.Render();
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(gui.GetDrawData());
        SwapBuffers(g_hDC);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    gui.DestroyContext();
    CleanupContextOpenGL(hwnd);
    standalone_ui_settings::SetStandaloneUiHwnd(0);
    SaveLauncherWindowPosition(hwnd);
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
}

// Background worker: fetch newest ReShade (reshade.me) and Display Commander (GitHub latest) versions once.
static void LatestVersionsFetchWorker() {
    std::string reshade_ver;
    std::string reshade_err;
    if (display_commander::utils::version_check::FetchReShadeLatestFromReshadeMe(&reshade_ver, &reshade_err)) {
        s_latestReShadeVersion.store(new std::string(reshade_ver), std::memory_order_release);
    } else {
        s_latestReShadeVersion.store(new std::string(), std::memory_order_release);
    }
    std::string dc_ver;
    std::string dc_err;
    if (display_commander::utils::version_check::FetchLatestDebugReleaseVersion(&dc_ver, &dc_err)) {
        s_latestDcVersion.store(new std::string(dc_ver), std::memory_order_release);
    } else {
        s_latestDcVersion.store(new std::string(), std::memory_order_release);
    }
    s_latestVersionsFetchDone.store(true, std::memory_order_release);
}

// Launcher Settings tab: font scale, ReShade global status, Display Commander global version.
static void DrawLauncherSettingsTab(display_commander::ui::IImGuiWrapper& imgui) {
    // Start one-time background fetch of newest available versions (ReShade, DC).
    bool expected = false;
    if (s_latestVersionsFetchStarted.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        std::thread t(LatestVersionsFetchWorker);
        t.detach();
    }

    float font_scale = 1.0f;
    display_commander::config::DisplayCommanderConfigManager::GetInstance().GetConfigValue("Launcher", "FontScale",
                                                                                           font_scale);
    if (font_scale <= 0.0f || font_scale > 3.0f) font_scale = 1.0f;

    if (imgui.SliderFloat("Font size", &font_scale, 0.5f, 2.0f, "%.2f")) {
        display_commander::config::DisplayCommanderConfigManager::GetInstance().SetConfigValue("Launcher", "FontScale",
                                                                                               font_scale);
        display_commander::config::DisplayCommanderConfigManager::GetInstance().SaveConfig("Launcher font scale");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Scale the Launcher window text. Takes effect immediately.");
    }

    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();
    imgui.TextUnformatted("ReShade (global install)");
    std::filesystem::path reshade_global = display_commander::utils::GetGlobalReshadeDirectory();
    std::string reshade_ver = display_commander::utils::GetGlobalReshadeVersion();
    if (reshade_global.empty()) {
        imgui.TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Not installed");
    } else {
        if (reshade_ver.empty()) {
            imgui.TextColored(ImVec4(0.7f, 0.7f, 0.5f, 1.0f), "Version: (unknown)");
        } else {
            imgui.Text("Version: %s", reshade_ver.c_str());
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("%s", reshade_global.string().c_str());
        }
    }
    if (s_latestVersionsFetchDone.load(std::memory_order_acquire)) {
        std::string* latest = s_latestReShadeVersion.load(std::memory_order_acquire);
        if (latest && !latest->empty()) {
            imgui.Text("Newest available: %s", latest->c_str());
        } else {
            imgui.TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Newest available: (unavailable)");
        }
    } else {
        imgui.TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Newest available: (checking...)");
    }
    bool reshade_updating = s_reshadeUpdateInProgress.load(std::memory_order_acquire);
    if (reshade_updating) {
        imgui.BeginDisabled();
    }
    if (imgui.Button("Update##reshade_global")) {
        std::wstring central = GetCentralReshadeDir();
        if (!central.empty()) {
            auto* params = new ReshadeUpdateParams;
            params->centralDir = central;
            params->selectedVersion = "latest";
            params->forGameDetails = false;
            s_reshadeUpdateResult.clear();
            s_reshadeUpdateInProgress.store(true, std::memory_order_release);
            HANDLE h = CreateThread(nullptr, 0, ReshadeUpdateWorker, params, 0, nullptr);
            if (h != nullptr) {
                CloseHandle(h);
            } else {
                s_reshadeUpdateInProgress.store(false, std::memory_order_release);
                delete params;
                s_reshadeUpdateResult = "Failed to start update thread.";
            }
        }
    }
    if (reshade_updating) {
        imgui.EndDisabled();
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Download latest ReShade Addon and install to global folder.");
    }
    if (!s_reshadeUpdateResult.empty()) {
        imgui.TextColored(ImVec4(0.7f, 0.85f, 0.7f, 1.0f), "%s", s_reshadeUpdateResult.c_str());
    }

    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();
    imgui.TextUnformatted("Display Commander (global install)");
    std::filesystem::path dc_appdata = GetDisplayCommanderAppDataFolder();
    if (dc_appdata.empty()) {
        imgui.TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Folder not available");
    } else {
#ifdef _WIN64
        std::filesystem::path addon_path = dc_appdata / L"zzz_display_commander.addon64";
#else
        std::filesystem::path addon_path = dc_appdata / L"zzz_display_commander.addon32";
#endif
        std::string dc_ver = GetFileVersionStringUtf8(addon_path.native());
        if (dc_ver.empty()) {
            imgui.TextColored(ImVec4(0.7f, 0.7f, 0.5f, 1.0f), "Version: not installed or not found");
        } else {
            imgui.Text("Version: %s", dc_ver.c_str());
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("%s", dc_appdata.string().c_str());
        }
        imgui.Text("This instance: %s", DISPLAY_COMMANDER_VERSION_STRING);
    }
    if (s_latestVersionsFetchDone.load(std::memory_order_acquire)) {
        std::string* latest = s_latestDcVersion.load(std::memory_order_acquire);
        if (latest && !latest->empty()) {
            imgui.Text("Newest available: %s", latest->c_str());
        } else {
            imgui.TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Newest available: (unavailable)");
        }
    } else {
        imgui.TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Newest available: (checking...)");
    }
    if (imgui.Button("Update##dc_global")) {
        display_commander::utils::version_check::CheckForUpdates();
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Check for Display Commander updates (GitHub). Result in main app or next launch.");
    }
}

// Save Launcher (Games) window position/size to config. No-op if hwnd is not the Launcher window class.
static void SaveLauncherWindowPosition(HWND hwnd) {
    wchar_t buf[64] = {};
    if (GetClassNameW(hwnd, buf, (int)std::size(buf)) == 0) return;
    if (wcscmp(buf, L"DisplayCommanderGamesUI") != 0) return;
    RECT r = {};
    if (!GetWindowRect(hwnd, &r)) return;
    int x = r.left;
    int y = r.top;
    int w = r.right - r.left;
    int h = r.bottom - r.top;
    auto& cfg = display_commander::config::DisplayCommanderConfigManager::GetInstance();
    cfg.SetConfigValue("Launcher", "WindowX", x);
    cfg.SetConfigValue("Launcher", "WindowY", y);
    cfg.SetConfigValue("Launcher", "WindowWidth", w);
    cfg.SetConfigValue("Launcher", "WindowHeight", h);
    cfg.SaveConfig("Launcher window position");
}

void RunStandaloneGamesOnlyUI(HINSTANCE hInst) {
    std::this_thread::sleep_for(std::chrono::seconds(1));

    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = (HINSTANCE)hInst;
    wc.lpszClassName = L"DisplayCommanderGamesUI";
    // Use embedded app icon (resource ID 1) for window and taskbar (exe build only; DLL has no icon resource)
#ifdef DISPLAY_COMMANDER_BUILD_EXE
    wc.hIcon = LoadIcon((HINSTANCE)hInst, MAKEINTRESOURCE(1));
    wc.hIconSm = LoadIcon((HINSTANCE)hInst, MAKEINTRESOURCE(1));
#endif
    if (!RegisterClassExW(&wc)) return;

    std::string titleUtf8 = "Display Commander - Games v";
    titleUtf8 += DISPLAY_COMMANDER_VERSION_STRING;
    int titleLen = MultiByteToWideChar(CP_UTF8, 0, titleUtf8.c_str(), (int)titleUtf8.size() + 1, nullptr, 0);
    std::wstring titleW(titleLen > 0 ? (size_t)titleLen : 0, 0);
    if (titleLen > 0)
        MultiByteToWideChar(CP_UTF8, 0, titleUtf8.c_str(), (int)titleUtf8.size() + 1, &titleW[0], titleLen);

    // Restore Win32 window position/size from config (saved on previous close).
    int launcher_x = 100;
    int launcher_y = 100;
    int launcher_w = 600;
    int launcher_h = 800;
    auto& launcher_cfg = display_commander::config::DisplayCommanderConfigManager::GetInstance();
    const bool has_x = launcher_cfg.GetConfigValue("Launcher", "WindowX", launcher_x);
    const bool has_y = launcher_cfg.GetConfigValue("Launcher", "WindowY", launcher_y);
    const bool has_w = launcher_cfg.GetConfigValue("Launcher", "WindowWidth", launcher_w);
    const bool has_h = launcher_cfg.GetConfigValue("Launcher", "WindowHeight", launcher_h);
    if (!(has_x && has_y && has_w && has_h)) {
        launcher_x = 100;
        launcher_y = 100;
        launcher_w = 600;
        launcher_h = 800;
    }
    if (launcher_w < 400) launcher_w = 400;
    if (launcher_h < 300) launcher_h = 300;
    if (launcher_w > 4096) launcher_w = 4096;
    if (launcher_h > 4096) launcher_h = 4096;

    HWND hwnd = standalone_ui_settings::CreateWindowW_Direct(
        wc.lpszClassName, titleW.empty() ? L"Display Commander - Games" : titleW.c_str(), WS_OVERLAPPEDWINDOW,
        launcher_x, launcher_y, launcher_w, launcher_h, nullptr, nullptr, (HINSTANCE)hInst, nullptr);
    if (!hwnd) {
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return;
    }
    standalone_ui_settings::SetStandaloneUiHwnd(reinterpret_cast<uintptr_t>(hwnd));

    if (!CreateContextOpenGL(hwnd)) {
        standalone_ui_settings::SetStandaloneUiHwnd(0);
        CleanupContextOpenGL(hwnd);
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    display_commander::ui::ImGuiWrapperStandalone gui;
    gui.CreateContext();
    gui.SetConfigFlags(static_cast<uint32_t>(ImGuiConfigFlags_NavEnableKeyboard));
    gui.StyleColorsDark();
    BuildStandaloneFontsWithJapanese(gui);
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplOpenGL3_Init();

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            glViewport(0, 0, (GLsizei)g_ResizeWidth, (GLsizei)g_ResizeHeight);
            gui.SetDisplaySize(ImVec2((float)g_ResizeWidth, (float)g_ResizeHeight));
            g_ResizeWidth = g_ResizeHeight = 0;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplWin32_NewFrame();

        gui.NewFrame();

        // Use current client size every frame so Games window grows and shrinks with the outer window
        RECT rc = {};
        if (GetClientRect(hwnd, &rc)) {
            gui.SetDisplaySize(ImVec2((float)(rc.right - rc.left), (float)(rc.bottom - rc.top)));
        }

        float launcher_font_scale = 1.0f;
        display_commander::config::DisplayCommanderConfigManager::GetInstance().GetConfigValue("Launcher", "FontScale",
                                                                                               launcher_font_scale);
        if (launcher_font_scale <= 0.0f || launcher_font_scale > 3.0f) launcher_font_scale = 1.0f;
        gui.SetFontGlobalScale(launcher_font_scale);
        gui.SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        gui.SetNextWindowSize(gui.GetDisplaySize(), ImGuiCond_Always);
        if (gui.Begin("Games", nullptr,
                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
                          | ImGuiWindowFlags_NoTitleBar)) {
            if (gui.BeginTabBar("##LauncherTabs", 0)) {
                if (gui.BeginTabItem("Games", nullptr, 0)) {
                    ui::new_ui::DrawGamesTab(gui);
                    gui.EndTabItem();
                }
                if (gui.BeginTabItem("Settings", nullptr, 0)) {
                    DrawLauncherSettingsTab(gui);
                    gui.EndTabItem();
                }
                gui.EndTabBar();
            }
        }
        gui.End();

        gui.Render();
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(gui.GetDrawData());
        SwapBuffers(g_hDC);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    gui.DestroyContext();
    CleanupContextOpenGL(hwnd);
    standalone_ui_settings::SetStandaloneUiHwnd(0);
    SaveLauncherWindowPosition(hwnd);
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
}

// WGL 3.0 context for ImGui OpenGL3 backend (Win32-only, no DX9)
static bool CreateContextOpenGL(HWND hWnd) {
    g_hDC = GetDC(hWnd);
    if (!g_hDC) return false;

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 0;
    pfd.cStencilBits = 0;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int pixelFormat = ChoosePixelFormat(g_hDC, &pfd);
    if (pixelFormat == 0) {
        ReleaseDC(hWnd, g_hDC);
        g_hDC = nullptr;
        return false;
    }
    if (!SetPixelFormat(g_hDC, pixelFormat, &pfd)) {
        ReleaseDC(hWnd, g_hDC);
        g_hDC = nullptr;
        return false;
    }

    HGLRC tempCtx = wglCreateContext(g_hDC);
    if (!tempCtx) {
        ReleaseDC(hWnd, g_hDC);
        g_hDC = nullptr;
        return false;
    }
    if (!wglMakeCurrent(g_hDC, tempCtx)) {
        wglDeleteContext(tempCtx);
        ReleaseDC(hWnd, g_hDC);
        g_hDC = nullptr;
        return false;
    }

    typedef HGLRC(WINAPI * PFN_wglCreateContextAttribsARB)(HDC, HGLRC, const int*);
    PFN_wglCreateContextAttribsARB wglCreateContextAttribsARB =
        (PFN_wglCreateContextAttribsARB)wglGetProcAddress("wglCreateContextAttribsARB");

    if (wglCreateContextAttribsARB) {
        int attribs[] = {0x2091, 3,       // WGL_CONTEXT_MAJOR_VERSION_ARB = 3
                         0x2092, 0,       // WGL_CONTEXT_MINOR_VERSION_ARB = 0
                         0x2094, 0x0002,  // WGL_CONTEXT_PROFILE_MASK_ARB = WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB
                         0};
        g_hGLRC = wglCreateContextAttribsARB(g_hDC, nullptr, attribs);
    }
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(tempCtx);

    if (!g_hGLRC && wglCreateContextAttribsARB) {
        ReleaseDC(hWnd, g_hDC);
        g_hDC = nullptr;
        return false;
    }
    if (!g_hGLRC) {
        g_hGLRC = wglCreateContext(g_hDC);
    }
    if (!g_hGLRC) {
        ReleaseDC(hWnd, g_hDC);
        g_hDC = nullptr;
        return false;
    }
    if (!wglMakeCurrent(g_hDC, g_hGLRC)) {
        wglDeleteContext(g_hGLRC);
        g_hGLRC = nullptr;
        ReleaseDC(hWnd, g_hDC);
        g_hDC = nullptr;
        return false;
    }
    return true;
}

static void CleanupContextOpenGL(HWND hWnd) {
    if (g_hGLRC && g_hDC) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(g_hGLRC);
        g_hGLRC = nullptr;
    }
    if (g_hDC && hWnd) {
        ReleaseDC(hWnd, g_hDC);
        g_hDC = nullptr;
    }
}

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return 1;
    switch (msg) {
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) {
                g_ResizeWidth = (UINT)LOWORD(lParam);
                g_ResizeHeight = (UINT)HIWORD(lParam);
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_CLOSE: {
            // When user closes the independent settings window (ReShade), uncheck "Show independent window".
            if (hWnd == standalone_ui_settings::GetStandaloneUiHwnd()) {
                settings::g_mainTabSettings.show_independent_window.SetValue(false);
            }
            break;
        }
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
