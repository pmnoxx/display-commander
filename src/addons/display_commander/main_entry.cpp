// Source Code <Display Commander>
#include "addon.hpp"
#include "audio/audio_management.hpp"
#include "config/display_commander_config.hpp"
#include "display/display_initial_state.hpp"
#include "display/dpi_management.hpp"
#include "exit_handler.hpp"
#include "globals.hpp"
#include "hooks/dbghelp/dbghelp_private_loader.hpp"
#include "hooks/input/xinput_hooks.hpp"
#include "hooks/loadlibrary_hooks.hpp"
#include "hooks/vulkan/nvlowlatencyvk_hooks.hpp"
#include "hooks/vulkan/vulkan_loader_hooks.hpp"
#include "hooks/windows_hooks/api_hooks.hpp"
#include "hooks/windows_hooks/window_proc_hooks.hpp"
#include "hooks/windows_hooks/windows_message_hooks.hpp"
#include "imgui.h"
#include "input_remapping/input_remapping.hpp"
#include "latency/gpu_completion_monitoring.hpp"
#include "latency/reflex_provider.hpp"
#include "latent_sync/refresh_rate_monitor_integration.hpp"
#include "nvapi/nvapi_actual_refresh_rate_monitor.hpp"
#include "nvapi/nvapi_init.hpp"
#include "process_exit_hooks.hpp"
#include "proxy_dll/dxgi_proxy_init.hpp"
#include "settings/advanced_tab_settings.hpp"
#include "settings/experimental_tab_settings.hpp"
#include "settings/hook_suppression_settings.hpp"
#include "settings/main_tab_settings.hpp"
#include "settings/reshade_tab_settings.hpp"
#include "swapchain_events.hpp"
#include "ui/imgui_wrapper_reshade.hpp"
#include "ui/monitor_settings/monitor_settings.hpp"
#include "ui/new_ui/experimental_tab.hpp"
#include "ui/new_ui/main_new_tab.hpp"
#include "ui/new_ui/new_ui_main.hpp"
#include "utils/dc_load_path.hpp"
#include "utils/detour_call_tracker.hpp"
#include "utils/display_commander_logger.hpp"
#include "utils/general_utils.hpp"
#include "utils/helper_exe_filter.hpp"
#include "utils/logging.hpp"
#include "utils/reshade_load_path.hpp"
#include "utils/timing.hpp"
#include "utils/version_check.hpp"
#include "version.hpp"

// Libraries <Windows.h>
#include <windows.h>

// Libraries <Windows>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <psapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <sysinfoapi.h>
#include <winver.h>
#include <wrl/client.h>

// Libraries <standard C++>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <reshade.hpp>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// Forward declarations for ReShade event handlers
void OnInitEffectRuntime(reshade::api::effect_runtime* runtime);
bool OnReShadeOverlayOpen(reshade::api::effect_runtime* runtime, bool open, reshade::api::input_source source);
// Note: OnInitDevice, OnDestroySwapchain, etc. are declared in swapchain_events.hpp
void OnInitCommandList(reshade::api::command_list* cmd_list);
void OnDestroyCommandList(reshade::api::command_list* cmd_list);
void OnInitCommandQueue(reshade::api::command_queue* queue);
void OnDestroyCommandQueue(reshade::api::command_queue* queue);
void OnExecuteCommandList(reshade::api::command_queue* queue, reshade::api::command_list* cmd_list);
void OnFinishPresent(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain);
void OnReShadeBeginEffects(reshade::api::effect_runtime* runtime, reshade::api::command_list* cmd_list,
                           reshade::api::resource_view rtv, reshade::api::resource_view rtv_srgb);
void OnReShadeFinishEffects(reshade::api::effect_runtime* runtime, reshade::api::command_list* cmd_list,
                            reshade::api::resource_view rtv, reshade::api::resource_view rtv_srgb);
void OnReShadePresent(reshade::api::effect_runtime* runtime);

// Forward declaration for version check
bool CheckReShadeVersionCompatibility();

// Forward declaration for multiple ReShade detection
void DetectMultipleReShadeVersions();

// Forward declaration for safemode function
void HandleSafemode();

// Forward declaration for loading addons from Plugins directory
void LoadAddonsFromPluginsDirectory();

// Export for multi-proxy coordination: other DC instances (dxgi, winmm, version.dll) scan this to decide HOOKED vs
// PROXY_DLL_ONLY
extern "C" __declspec(dllexport) int GetDisplayCommanderState() {
    return static_cast<int>(g_display_commander_state.load(std::memory_order_acquire));
}

// TryGetDxgiOutputDeviceNameFromLastSwapchain removed - VRR status is now updated
// from OnPresentUpdateBefore with direct swapchain access to avoid unsafe global pointer usage

// Function to parse version string and check if it's 6.6.2 or above
bool IsVersion662OrAbove(const std::string& version_str) {
    if (version_str.empty()) {
        return false;
    }

    // Parse version string in format "major.minor.build.revision"
    // We need to check if version is >= 6.6.2.0
    int major = 0;
    int minor = 0;
    int build = 0;
    int revision = 0;

    if (sscanf_s(version_str.c_str(), "%d.%d.%d.%d", &major, &minor, &build, &revision) >= 2) {
        // Check if version is 6.6.2 or above
        if (major > 6) {
            return true;  // Major version > 6
        }
        if (major == 6) {
            if (minor > 6) {
                return true;  // 6.x where x > 6
            }
            if (minor == 6) {
                return build >= 2;  // 6.6.x where x >= 2
            }
        }
    }

    return false;
}

// Structure to store ReShade module detection debug information
struct ReShadeModuleInfo {
    std::string path;
    std::string version;
    bool has_imgui_support;
    bool is_version_662_or_above;
    HMODULE handle;
};

struct ReShadeDetectionDebugInfo {
    int total_modules_found = 0;
    std::vector<ReShadeModuleInfo> modules;
    bool detection_completed = false;
    std::string error_message;
};

// Global debug information storage
ReShadeDetectionDebugInfo g_reshade_debug_info;

void OnRegisterOverlayDisplayCommander(reshade::api::effect_runtime* runtime) {
    CALL_GUARD_NO_TS();;
    // Ensure the current overlay runtime is in our list (fallback if init_effect_runtime was missed, e.g. addon load
    // order)
    if (runtime != nullptr) {
        AddReShadeRuntime(runtime);
    }
    const bool show_display_commander_ui = settings::g_mainTabSettings.show_display_commander_ui.GetValue();
    // Avoid displaying UI twice
    if (show_display_commander_ui) {
        settings::g_mainTabSettings.show_display_commander_ui.SetValue(false);
    }
    {
        display_commander::ui::ImGuiWrapperReshade gui_wrapper;
        ui::new_ui::NewUISystem::GetInstance().Draw(runtime, gui_wrapper);
    }

    // Periodically save config to ensure settings are persisted
    static LONGLONG last_save_time = utils::get_now_ns();
    LONGLONG now = utils::get_now_ns();
    if ((now - last_save_time) >= 5 * utils::SEC_TO_NS) {
        display_commander::config::save_config("periodic save (every 5 seconds)");
        last_save_time = now;
    }
}

// ReShade effect runtime event handler for input blocking
void OnInitCommandList(reshade::api::command_list* cmd_list) {
    CALL_GUARD_NO_TS();;
    // Command list initialization tracking
    if (cmd_list == nullptr) {
        return;
    }
    // Add any initialization logic here if needed
}

void OnDestroyCommandList(reshade::api::command_list* cmd_list) {
    CALL_GUARD_NO_TS();;
    // Command list destruction tracking
    if (cmd_list == nullptr) {
        return;
    }
    // Add any cleanup logic here if needed
}

void OnInitCommandQueue(reshade::api::command_queue* queue) {
    CALL_GUARD_NO_TS();;
    // Command queue initialization tracking
    if (queue == nullptr) {
        return;
    }
    // Add any initialization logic here if needed
}

void OnDestroyCommandQueue(reshade::api::command_queue* queue) {
    CALL_GUARD_NO_TS();;
    // Command queue destruction tracking
    if (queue == nullptr) {
        return;
    }
    // Add any cleanup logic here if needed
}

void OnExecuteCommandList(reshade::api::command_queue* queue, reshade::api::command_list* cmd_list) {
    CALL_GUARD_NO_TS();;
    // Command list execution tracking
    if (queue == nullptr || cmd_list == nullptr) {
        return;
    }
    // Add any tracking logic here if needed
}

void OnFinishPresent(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain) {
    CALL_GUARD_NO_TS();;
    // Present completion tracking
    if (queue == nullptr || swapchain == nullptr) {
        return;
    }
    // Add any tracking logic here if needed
}

void OnReShadeBeginEffects(reshade::api::effect_runtime* runtime, reshade::api::command_list* cmd_list,
                           reshade::api::resource_view rtv, reshade::api::resource_view rtv_srgb) {
    CALL_GUARD_NO_TS();;
    if (runtime == nullptr || cmd_list == nullptr) {
        return;
    }
    // Brightness is applied from OnReShadePresent to avoid modifying technique state/uniforms
    // during the effect loop (which can cause crashes).
}

void OnReShadeFinishEffects(reshade::api::effect_runtime* runtime, reshade::api::command_list* cmd_list,
                            reshade::api::resource_view rtv, reshade::api::resource_view rtv_srgb) {
    if (IsDisplayCommanderHookingInstance()) display_commanderhooks::InstallApiHooks();
    CALL_GUARD_NO_TS();;
    // ReShade effects finish tracking
    if (runtime == nullptr || cmd_list == nullptr) {
        return;
    }
    // Add any tracking logic here if needed
}

void OnReShadePresent(reshade::api::effect_runtime* runtime) {
    (void)runtime;
}

namespace {
void OnInitEffectRuntime_StartRefreshRateMonitoringIfNeeded() {
    if (settings::g_mainTabSettings.show_actual_refresh_rate.GetValue()
        || settings::g_mainTabSettings.show_refresh_rate_frame_times.GetValue()) {
        display_commander::nvapi::StartNvapiActualRefreshRateMonitoring();
    }
}

void OnInitEffectRuntime_InitWithHwndOnce(reshade::api::effect_runtime* runtime) {
    static bool initialized_with_hwnd = false;
    if (initialized_with_hwnd) {
        return;
    }
    HWND game_window = static_cast<HWND>(runtime->get_hwnd());
    if (game_window == nullptr) {
        return;
    }
    initialized_with_hwnd = true;
    if (IsWindow(game_window) != 0) {
        LogInfo("[OnInitEffectRuntime] Game window detected - HWND: 0x%p", game_window);
        LogInfo("[OnInitEffectRuntime] calling DoInitializationWithHwnd...");
        DoInitializationWithHwnd(game_window);
        LogInfo("[OnInitEffectRuntime] DoInitializationWithHwnd returned");
    } else {
        LogWarn("[OnInitEffectRuntime] ReShade runtime window is not valid - HWND: 0x%p", game_window);
    }
}
}  // namespace

void OnInitEffectRuntime(reshade::api::effect_runtime* runtime) {
    LogInfo("[OnInitEffectRuntime] entry");
    CALL_GUARD_NO_TS();;
    if (runtime == nullptr) {
        LogInfo("[OnInitEffectRuntime] runtime is null, returning");
        return;
    }
    AddReShadeRuntime(runtime);
    LogInfo("[OnInitEffectRuntime] after AddReShadeRuntime");

    LogInfo("[OnInitEffectRuntime] ReShade effect runtime initialized - Input blocking now available");

    OnInitEffectRuntime_StartRefreshRateMonitoringIfNeeded();
    OnInitEffectRuntime_InitWithHwndOnce(runtime);

    // Re-apply ReShade config overrides for this runtime (window config, paths, tutorial/updates, etc.)
    // so each new runtime (e.g. ReShade2.ini) gets the same DC settings.
    OverrideReShadeSettings(runtime);

    LogInfo("[OnInitEffectRuntime] exit");
}

// ReShade overlay event handler for input blocking
bool OnReShadeOverlayOpen(reshade::api::effect_runtime* runtime, bool open, reshade::api::input_source source) {
    CALL_GUARD_NO_TS();;

    if (open) {
        LogInfo("ReShade overlay opened - Input blocking active");
        // When ReShade overlay opens, we can also use its input blocking
        if (runtime != nullptr) {
            AddReShadeRuntime(runtime);
        }
    } else {
        LogInfo("ReShade overlay closed - Input blocking inactive");
    }

    return false;  // Don't prevent ReShade from opening/closing the overlay
}

// Direct overlay draw callback (no settings2 indirection)
namespace {

constexpr size_t kCursorOutlineSize = 3;
constexpr std::array<std::array<float, 2>, kCursorOutlineSize> kCursorOutline = {{
    {0.5f, 0.5f},
    {17.0f, 8.0f},
    // {(17.0f + 4.0f) * 0.4f, (8.0f + 20.0f) * 0.4f},
    {4.0f, 20.0f},
}};

static void DrawCustomCursor(display_commander::ui::IImGuiWrapper& gui_wrapper) {
    const ImVec2 pos = gui_wrapper.GetIO().MousePos;
    const float s = 1.0f;

    display_commander::ui::IImDrawList* draw_list = gui_wrapper.GetForegroundDrawList();
    if (draw_list == nullptr) return;

    const ImU32 col_border = IM_COL32(0, 0, 0, 255);
    const ImU32 col_fill = IM_COL32(255, 255, 255, 255);
    const float thickness = 0.5f;

    // Build screen-space points from coordinate list
    double center_x = 0;
    double center_y = 0;
    ImVec2 pts[kCursorOutlineSize];
    for (size_t i = 0; i < kCursorOutlineSize; ++i) {
        pts[i].x = pos.x + kCursorOutline[i][0] * s;
        pts[i].y = pos.y + kCursorOutline[i][1] * s;
        center_x += pts[i].x;
        center_y += pts[i].y;
    }
    center_x /= kCursorOutlineSize;
    center_y /= kCursorOutlineSize;
    ImVec2 pts2[kCursorOutlineSize];
    for (size_t i = 0; i < kCursorOutlineSize; ++i) {
        pts2[i].x = pts[i].x + (pts[i].x - center_x) * 0.1f;
        pts2[i].y = pts[i].y + (pts[i].y - center_y) * 0.1f;
    }

    // Fill: triangle fan from tip (0) -> (1,2), (2,3), ..., (size-1,1)
    for (size_t i = 1; i < kCursorOutlineSize - 1; ++i) {
        draw_list->AddTriangleFilled(pts[0], pts[i], pts[i + 1], col_fill);
    }
    draw_list->AddTriangleFilled(pts[0], pts[kCursorOutlineSize - 1], pts[1], col_fill);

    // Outline: closed polygon
    for (size_t i = 0; i < kCursorOutlineSize; ++i) {
        const size_t j = (i + 1) % kCursorOutlineSize;
        draw_list->AddLine(pts2[i], pts2[j], col_border, thickness);
    }
}

// Test callback for reshade_overlay event
void OnPerformanceOverlay_DisplayCommanderWindow(reshade::api::effect_runtime* runtime) {
    CALL_GUARD_NO_TS();;
    display_commander::ui::ImGuiWrapperReshade overlay_wrapper;
    const float fixed_width = 1600.0f;
    float saved_x = settings::g_mainTabSettings.display_commander_ui_window_x.GetValue();
    float saved_y = settings::g_mainTabSettings.display_commander_ui_window_y.GetValue();
    static float last_saved_x = 0.0f;
    static float last_saved_y = 0.0f;
    if (saved_x > 0.0f || saved_y > 0.0f) {
        if (saved_x != last_saved_x || saved_y != last_saved_y) {
            overlay_wrapper.SetNextWindowPos(ImVec2(saved_x, saved_y), ImGuiCond_Once, ImVec2(0.f, 0.f));
            last_saved_x = saved_x;
            last_saved_y = saved_y;
        }
    }
    overlay_wrapper.SetNextWindowSize(ImVec2(fixed_width, 2160.0f), ImGuiCond_Always);

    bool window_open = true;
    if (overlay_wrapper.Begin("Display Commander", &window_open,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
        if (runtime != nullptr) {
            runtime->block_input_next_frame();
        }
        ImVec2 current_pos = overlay_wrapper.GetWindowPos();
        if (current_pos.x != saved_x || current_pos.y != saved_y) {
            settings::g_mainTabSettings.display_commander_ui_window_x.SetValue(current_pos.x);
            settings::g_mainTabSettings.display_commander_ui_window_y.SetValue(current_pos.y);
            last_saved_x = current_pos.x;
            last_saved_y = current_pos.y;
        }
        ui::new_ui::NewUISystem::GetInstance().Draw(runtime, overlay_wrapper);
    } else {
        settings::g_mainTabSettings.show_display_commander_ui.SetValue(false);
    }
    overlay_wrapper.End();
    if (!window_open) {
        settings::g_mainTabSettings.show_display_commander_ui.SetValue(false);
    }
    DrawCustomCursor(overlay_wrapper);
}

void OnPerformanceOverlay_TestWindow(reshade::api::effect_runtime* runtime, bool show_tooltips) {
    display_commander::ui::ImGuiWrapperReshade overlay_wrapper;
    float vertical_spacing = settings::g_mainTabSettings.overlay_vertical_spacing.GetValue();
    float horizontal_spacing = settings::g_mainTabSettings.overlay_horizontal_spacing.GetValue();
    overlay_wrapper.SetNextWindowPos(ImVec2(10.0f + horizontal_spacing, 10.0f + vertical_spacing), ImGuiCond_Always,
                                     ImVec2(0.f, 0.f));
    float bg_alpha = settings::g_mainTabSettings.overlay_background_alpha.GetValue();
    overlay_wrapper.SetNextWindowBgAlpha(bg_alpha);
    overlay_wrapper.SetNextWindowSize(ImVec2(450, 65), ImGuiCond_FirstUseEver);
    if (overlay_wrapper.Begin("Test Window", nullptr,
                              ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize
                                  | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
                                  | ImGuiWindowFlags_AlwaysAutoResize)) {
        ui::new_ui::DrawPerformanceOverlayContent(overlay_wrapper, ui::new_ui::GetGraphicsApiFromRuntime(runtime),
                                                  show_tooltips);
    }
    overlay_wrapper.End();
}

void OnPerformanceOverlay(reshade::api::effect_runtime* runtime) {
    CALL_GUARD_NO_TS();;
    const bool show_display_commander_ui = settings::g_mainTabSettings.show_display_commander_ui.GetValue();
    const bool show_tooltips = show_display_commander_ui;

    if (show_display_commander_ui) {
        OnPerformanceOverlay_DisplayCommanderWindow(runtime);
    }

    bool show_actual_refresh_rate = settings::g_mainTabSettings.show_actual_refresh_rate.GetValue();
    bool show_refresh_rate_frame_times = settings::g_mainTabSettings.show_refresh_rate_frame_times.GetValue();
    bool show_performance_overlay = settings::g_mainTabSettings.show_test_overlay.GetValue();
    if (show_performance_overlay && (show_actual_refresh_rate || show_refresh_rate_frame_times)) {
        if (!display_commander::nvapi::IsNvapiActualRefreshRateMonitoringActive()) {
            display_commander::nvapi::StartNvapiActualRefreshRateMonitoring();
        }
    } else {
        if (display_commander::nvapi::IsNvapiActualRefreshRateMonitoringActive()) {
            display_commander::nvapi::StopNvapiActualRefreshRateMonitoring();
        }
    }

    if (!settings::g_mainTabSettings.show_test_overlay.GetValue()) {
        return;
    }
    OnPerformanceOverlay_TestWindow(runtime, show_tooltips);
}
}  // namespace

namespace {
// Helpers for OverrideReShadeSettings - each handles one logical block.
// When runtime is non-null, config is read/written for that runtime's .ini (e.g. ReShade2.ini); otherwise
// global/current.
void OverrideReShadeSettings_WindowConfig(reshade::api::effect_runtime* runtime) {
    std::string window_config;
    size_t value_size = 0;
    if ((g_reshade_module != nullptr)
        && reshade::get_config_value(runtime, "OVERLAY", "Window", nullptr, &value_size)) {
        window_config.resize(value_size);
        if ((g_reshade_module != nullptr)
            && reshade::get_config_value(runtime, "OVERLAY", "Window", window_config.data(), &value_size)) {
            if (!window_config.empty() && window_config.back() == '\0') {
                window_config.pop_back();
            }
        } else {
            window_config.clear();
        }
    }

    bool changed_window_config = false;
    if (window_config.find("[Window][DC]") == std::string::npos) {
        if (!window_config.empty()) {
            window_config.push_back('\0');
        }
        std::string to_add = "[Window][DC],Pos=1017,,20,Size=1344,,1255,Collapsed=0,DockId=0x00000001,,999999,";
        for (size_t i = 0; i < to_add.size(); i++) {
            if (to_add[i] == ',') {
                window_config.push_back('\0');
            } else {
                window_config.push_back(to_add[i]);
            }
        }
        changed_window_config = true;
    }
    if (window_config.find("[Window][RenoDX]") == std::string::npos) {
        if (!window_config.empty()) {
            window_config.push_back('\0');
        }
        std::string to_add = "[Window][RenoDX],Pos=1017,,20,Size=1344,,1255,Collapsed=0,DockId=0x00000001,,9999999,";
        for (size_t i = 0; i < to_add.size(); i++) {
            if (to_add[i] == ',') {
                window_config.push_back('\0');
            } else {
                window_config.push_back(to_add[i]);
            }
        }
        changed_window_config = true;
    }
    if (changed_window_config) {
        reshade::set_config_value(runtime, "OVERLAY", "Window", window_config.c_str(), window_config.size());
        LogInfo("Updated ReShade Window config with Display Commander and RenoDX docking settings");
    }
}

void OverrideReShadeSettings_TutorialAndUpdates(reshade::api::effect_runtime* runtime) {
    reshade::set_config_value(runtime, "OVERLAY", "TutorialProgress", 4);
    reshade::set_config_value(runtime, "GENERAL", "CheckForUpdates", 0);
    reshade::set_config_value(runtime, "OVERLAY", "ShowClock", 0);
}

// ReShade defaults SCREENSHOT SavePath and PostSaveCommandWorkingDirectory to ".\" (game folder).
// Redirect to .\Screenshots so captures and post-save commands do not clutter the exe directory.
void OverrideReShadeSettings_ScreenshotPaths(reshade::api::effect_runtime* runtime) {
    if (g_reshade_module == nullptr) {
        return;
    }
    auto trim = [](std::string& s) {
        while (!s.empty() && static_cast<unsigned char>(s.back()) <= 32) {
            s.pop_back();
        }
        size_t i = 0;
        while (i < s.size() && static_cast<unsigned char>(s[i]) <= 32) {
            ++i;
        }
        if (i > 0) {
            s.erase(0, i);
        }
    };
    auto is_game_root_relative = [&trim](std::string s) -> bool {
        trim(s);
        if (s.empty()) {
            return false;
        }
        if (s == ".") {
            return true;
        }
        if (s.size() == 2 && s[0] == '.' && (s[1] == '\\' || s[1] == '/')) {
            return true;
        }
        return false;
    };
    static constexpr const char kScreenshots[] = ".\\Screenshots";

    char buf[1024];
    size_t sz = sizeof(buf);
    const bool have_save_path =
        reshade::get_config_value(runtime, "SCREENSHOT", "SavePath", buf, &sz);
    std::string save_val = have_save_path ? std::string(buf) : std::string();
    if (!have_save_path || is_game_root_relative(std::move(save_val))) {
        reshade::set_config_value(runtime, "SCREENSHOT", "SavePath", kScreenshots);
        LogInfo("ReShade settings override - SCREENSHOT SavePath -> %s (was game root / default)",
                kScreenshots);
    }

    sz = sizeof(buf);
    const bool have_post_cwd =
        reshade::get_config_value(runtime, "SCREENSHOT", "PostSaveCommandWorkingDirectory", buf, &sz);
    std::string post_val = have_post_cwd ? std::string(buf) : std::string();
    trim(post_val);
    if (have_post_cwd && post_val.empty()) {
        return;  // user cleared working directory; do not override
    }
    if (!have_post_cwd || is_game_root_relative(std::move(post_val))) {
        reshade::set_config_value(runtime, "SCREENSHOT", "PostSaveCommandWorkingDirectory", kScreenshots);
        LogInfo(
            "ReShade settings override - SCREENSHOT PostSaveCommandWorkingDirectory -> %s (was game root / "
            "default)",
            kScreenshots);
    }
}

constexpr const char* kDisplayCommanderSection = "DisplayCommander";
constexpr const char* kConfigKeyGlobalShadersPathsEnabled = "ReShadeGlobalShadersTexturesPathsEnabled";
constexpr const char* kConfigKeyScreenshotPathEnabled = "ReShadeScreenshotPathEnabled";

bool IsGlobalShadersPathsConfigEnabled() {
    bool enabled = false;
    display_commander::config::get_config_value_ensure_exists(kDisplayCommanderSection, kConfigKeyGlobalShadersPathsEnabled,
                                                               enabled, false);
    return enabled;
}

bool IsScreenshotPathConfigEnabled() {
    bool enabled = false;
    display_commander::config::get_config_value_ensure_exists(kDisplayCommanderSection, kConfigKeyScreenshotPathEnabled,
                                                               enabled, false);
    return enabled;
}

void OverrideReShadeSettings_LoadFromDllMainOnce(reshade::api::effect_runtime* runtime) {
    bool load_from_dll_main_set_once = false;
    display_commander::config::get_config_value("DisplayCommander", "LoadFromDllMainSetOnce",
                                                load_from_dll_main_set_once);
    if (!load_from_dll_main_set_once) {
        int32_t current_reshade_value = 0;
        reshade::get_config_value(runtime, "ADDON", "LoadFromDllMain", current_reshade_value);
        LogInfo("ReShade settings override - LoadFromDllMain current ReShade value: %d", current_reshade_value);
        LogInfo("ReShade settings override - LoadFromDllMain set to 0 (first time)");
        display_commander::config::set_config_value("DisplayCommander", "LoadFromDllMainSetOnce", true);
        display_commander::config::save_config("LoadFromDllMainSetOnce flag set");
        LogInfo("ReShade settings override - LoadFromDllMainSetOnce flag saved to DisplayCommander config");
    } else {
        LogInfo("ReShade settings override - LoadFromDllMain already set to 0 previously, skipping");
    }
}

void OverrideReShadeSettings_AddDisplayCommanderPaths(reshade::api::effect_runtime* runtime, bool enabled) {

    std::filesystem::path dc_base_dir = GetDisplayCommanderReshadeRootFolder();
    if (dc_base_dir.empty()) {
        LogWarn("Failed to get DC Reshade root path, skipping ReShade path configuration");
        return;
    }
    std::filesystem::path shaders_dir = dc_base_dir / L"Shaders";
    std::filesystem::path textures_dir = dc_base_dir / L"Textures";

    try {
        std::error_code ec;
        std::filesystem::create_directories(shaders_dir, ec);
        if (ec) {
            LogWarn("Failed to create shaders directory: %ls (error: %s)", shaders_dir.c_str(), ec.message().c_str());
        } else {
            LogInfo("Created/verified shaders directory: %ls", shaders_dir.c_str());
        }
        std::filesystem::create_directories(textures_dir, ec);
        if (ec) {
            LogWarn("Failed to create textures directory: %ls (error: %s)", textures_dir.c_str(), ec.message().c_str());
        } else {
            LogInfo("Created/verified textures directory: %ls", textures_dir.c_str());
        }
    } catch (const std::exception& e) {
        LogWarn("Exception while creating directories: %s", e.what());
        return;
    }

    auto syncPathInSearchPaths = [runtime, enabled](const char* section, const char* key,
                                                    const std::filesystem::path& path_to_sync) -> bool {
        char buffer[4096] = {0};
        size_t buffer_size = sizeof(buffer);
        std::vector<std::string> existing_paths;
        if ((g_reshade_module != nullptr) && reshade::get_config_value(runtime, section, key, buffer, &buffer_size)) {
            const char* ptr = buffer;
            while (*ptr != '\0' && ptr < buffer + buffer_size) {
                std::string path(ptr);
                if (!path.empty()) {
                    existing_paths.push_back(path);
                }
                ptr += path.length() + 1;
            }
        }
        std::string path_str = path_to_sync.string();
        path_str += "\\**";
        auto normalizeForComparison = [](const std::string& path) -> std::string {
            std::string normalized = path;
            if (normalized.length() >= 3 && normalized.substr(normalized.length() - 3) == "\\**") {
                normalized = normalized.substr(0, normalized.length() - 3);
            }
            return normalized;
        };
        std::string normalized_path = normalizeForComparison(path_str);
        bool found_existing = false;
        std::vector<std::string> updated_paths;
        updated_paths.reserve(existing_paths.size());
        for (const auto& existing_path : existing_paths) {
            std::string normalized_existing = normalizeForComparison(existing_path);
            if (normalized_path.length() == normalized_existing.length()
                && std::equal(normalized_path.begin(), normalized_path.end(), normalized_existing.begin(),
                              [](char a, char b) { return std::tolower(a) == std::tolower(b); })) {
                found_existing = true;
                if (enabled) {
                    updated_paths.push_back(existing_path);
                }
                continue;
            }
            updated_paths.push_back(existing_path);
        }

        if (enabled && !found_existing) {
            updated_paths.push_back(path_str);
        }

        if ((enabled && found_existing) || (!enabled && !found_existing)) {
            LogInfo("ReShade %s::%s unchanged for path %s (enabled=%d)", section, key, normalized_path.c_str(),
                    enabled ? 1 : 0);
            return false;
        }

        std::string combined;
        for (const auto& path : updated_paths) {
            combined += path;
            combined += '\0';
        }
        reshade::set_config_value(runtime, section, key, combined.c_str(), combined.size());
        LogInfo("%s path in ReShade %s::%s: %s", enabled ? "Added" : "Removed", section, key, path_str.c_str());
        return true;
    };

    syncPathInSearchPaths("GENERAL", "EffectSearchPaths", shaders_dir);
    syncPathInSearchPaths("GENERAL", "TextureSearchPaths", textures_dir);
}
}  // namespace

// Override ReShade settings to set tutorial as viewed and disable auto updates.
// When runtime is non-null (e.g. from OnInitEffectRuntime), config is applied to that runtime's .ini; otherwise
// global/current.
void OverrideReShadeSettings(reshade::api::effect_runtime* runtime) {
    if (g_reshade_module == nullptr) {
        return;  // No-ReShade mode or ReShade not loaded; skip ReShade config override
    }
    LogInfo("Overriding ReShade settings - Setting tutorial as viewed and disabling auto updates");

    OverrideReShadeSettings_WindowConfig(runtime);
    OverrideReShadeSettings_TutorialAndUpdates(runtime);
    if (IsScreenshotPathConfigEnabled()) {
        OverrideReShadeSettings_ScreenshotPaths(runtime);
    } else {
        // Restore defaults when this local toggle is disabled and the config still points to .\Screenshots.
        static constexpr const char kScreenshots[] = ".\\Screenshots";
        static constexpr const char kGameRoot[] = ".\\";
        char buf[1024];
        size_t sz = sizeof(buf);
        if (reshade::get_config_value(runtime, "SCREENSHOT", "SavePath", buf, &sz) && std::string(buf) == kScreenshots) {
            reshade::set_config_value(runtime, "SCREENSHOT", "SavePath", kGameRoot);
        }
        sz = sizeof(buf);
        if (reshade::get_config_value(runtime, "SCREENSHOT", "PostSaveCommandWorkingDirectory", buf, &sz)
            && std::string(buf) == kScreenshots) {
            reshade::set_config_value(runtime, "SCREENSHOT", "PostSaveCommandWorkingDirectory", kGameRoot);
        }
    }
    OverrideReShadeSettings_LoadFromDllMainOnce(runtime);
    OverrideReShadeSettings_AddDisplayCommanderPaths(runtime, IsGlobalShadersPathsConfigEnabled());

    LogInfo("ReShade settings override completed successfully");
}

// No-ReShade mode: ReShade is not loaded; hooks/UI init run without ReShade overlay.
std::atomic<bool> g_no_reshade_mode(false);

// Helper function to check if an addon is enabled (whitelist approach)
static bool IsAddonEnabledForLoading(const std::string& addon_name, const std::string& addon_file) {
    if (g_reshade_module == nullptr) {
        return false;
    }
    std::vector<std::string> enabled_addons;
    display_commander::config::get_config_value("ADDONS", "EnabledAddons", enabled_addons);

    // Create identifier in format "name@file"
    std::string identifier = addon_name + "@" + addon_file;

    // Check if this addon is in the enabled list
    for (const auto& enabled_entry : enabled_addons) {
        if (enabled_entry == identifier) {
            return true;
        }
    }

    return false;
}

namespace {
// Get Addons dir path and create it. Returns false on failure.
bool LoadAddonsFromPluginsDirectory_EnsureAddonsDir(std::filesystem::path& out_addons_dir) {
    wchar_t localappdata_path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localappdata_path))) {
        LogWarn("Failed to get LocalAppData folder path, skipping addon loading from Addons directory");
        return false;
    }
    out_addons_dir =
        std::filesystem::path(localappdata_path) / L"Programs" / L"Display_Commander" / L"Reshade" / L"Addons";
    try {
        std::error_code ec;
        std::filesystem::create_directories(out_addons_dir, ec);
        if (ec) {
            LogWarn("Failed to create Addons directory: %ls (error: %s)", out_addons_dir.c_str(), ec.message().c_str());
            return false;
        }
        LogInfo("Created/verified Addons directory: %ls", out_addons_dir.c_str());
        return true;
    } catch (const std::exception& e) {
        LogWarn("Exception while creating Addons directory: %s", e.what());
        return false;
    }
}

// Iterate addons_dir and load enabled .addon64/.addon32. Updates counts and logs.
void LoadAddonsFromPluginsDirectory_IterateAndLoad(const std::filesystem::path& addons_dir) {
    try {
        std::error_code ec;
        int loaded_count = 0;
        int failed_count = 0;
        int skipped_count = 0;

        for (const auto& entry : std::filesystem::directory_iterator(
                 addons_dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (ec) {
                LogWarn("Error accessing Addons directory: %s", ec.message().c_str());
                continue;
            }
            if (!entry.is_regular_file()) continue;

            const auto& path = entry.path();
            const auto extension = path.extension();
            if (extension != L".addon64" && extension != L".addon32") continue;
#ifdef _WIN64
            if (extension != L".addon64") continue;
#else
            if (extension != L".addon32") continue;
#endif
            std::string addon_name = path.stem().string();
            std::string addon_file = path.filename().string();
            if (!IsAddonEnabledForLoading(addon_name, addon_file)) {
                LogInfo("Skipping disabled addon: %ls", path.c_str());
                skipped_count++;
                continue;
            }
            LogInfo("Loading enabled addon from Addons directory: %ls", path.c_str());
            HMODULE module = LoadLibraryExW(path.c_str(), nullptr,
                                            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (module == nullptr) {
                LogError("Failed to load addon from '%ls' (error: %lu)", path.c_str(), GetLastError());
                failed_count++;
            } else {
                LogInfo("Successfully loaded addon from '%ls'", path.c_str());
                loaded_count++;
                // RenoDX addon loaded: disable Swapchain HDR Upgrade and set global flag
                std::string lower = addon_file;
                std::transform(lower.begin(), lower.end(), lower.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (lower.find("renodx") != std::string::npos && lower.find("renodx-devkit") == std::string::npos) {
                    g_is_renodx_loaded.store(true, std::memory_order_relaxed);
                }
            }
        }
        if (loaded_count > 0 || failed_count > 0 || skipped_count > 0) {
            LogInfo("Addon loading from Addons directory completed: %d loaded, %d failed, %d skipped (disabled)",
                    loaded_count, failed_count, skipped_count);
        }
    } catch (const std::exception& e) {
        LogWarn("Exception while loading addons from Addons directory: %s", e.what());
    }
}
}  // namespace

// Function to load enabled .addon64/.addon32 files from %localappdata%\Programs\Display_Commander\Reshade\Addons
void LoadAddonsFromPluginsDirectory() {
    OutputDebugStringA("Loading addons from Addons directory");
    std::filesystem::path addons_dir;
    if (!LoadAddonsFromPluginsDirectory_EnsureAddonsDir(addons_dir)) {
        return;
    }
    if (g_reshade_module == nullptr) {
        LogInfo("ReShade not loaded yet, skipping addon loading from Addons directory");
        return;
    }
    OutputDebugStringA("ReShade loaded, attempting to load addons from Addons directory");
    LoadAddonsFromPluginsDirectory_IterateAndLoad(addons_dir);
}

namespace {
// Enumerate process modules and fill g_reshade_debug_info.modules with ReShade modules. Returns false on enum failure.
bool DetectMultipleReShadeVersions_EnumerateModules() {
    HMODULE modules[1024];
    DWORD num_modules = 0;
    if (K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &num_modules) == 0) {
        DWORD error = GetLastError();
        LogWarn("Failed to enumerate process modules: %lu", error);
        g_reshade_debug_info.error_message = "Failed to enumerate process modules: " + std::to_string(error);
        return false;
    }
    if (num_modules > sizeof(modules)) {
        num_modules = static_cast<DWORD>(sizeof(modules));
    }
    LogInfo("Scanning %lu modules for ReShade...", num_modules / sizeof(HMODULE));
    int reshade_module_count = 0;
    for (DWORD i = 0; i < num_modules / sizeof(HMODULE); ++i) {
        HMODULE module = modules[i];
        if (module == nullptr) continue;
        FARPROC register_func = GetProcAddress(module, "ReShadeRegisterAddon");
        FARPROC unregister_func = GetProcAddress(module, "ReShadeUnregisterAddon");
        if (register_func == nullptr || unregister_func == nullptr) continue;

        reshade_module_count++;
        ReShadeModuleInfo module_info;
        module_info.handle = module;
        wchar_t module_path[MAX_PATH];
        DWORD path_length = GetModuleFileNameW(module, module_path, MAX_PATH);

        if (path_length > 0) {
            char narrow_path[MAX_PATH];
            WideCharToMultiByte(CP_UTF8, 0, module_path, -1, narrow_path, MAX_PATH, nullptr, nullptr);
            module_info.path = narrow_path;
            LogInfo("Found ReShade module #%d: 0x%p - %s", reshade_module_count, module, narrow_path);

            DWORD version_dummy = 0;
            DWORD version_size = GetFileVersionInfoSizeW(module_path, &version_dummy);
            if (version_size > 0) {
                std::vector<uint8_t> version_data(version_size);
                if (GetFileVersionInfoW(module_path, version_dummy, version_size, version_data.data()) != 0) {
                    VS_FIXEDFILEINFO* version_info = nullptr;
                    UINT version_info_size = 0;
                    if (VerQueryValueW(version_data.data(), L"\\", reinterpret_cast<LPVOID*>(&version_info),
                                       &version_info_size)
                            != 0
                        && version_info != nullptr) {
                        char version_str[64];
                        snprintf(version_str, sizeof(version_str), "%hu.%hu.%hu.%hu",
                                 HIWORD(version_info->dwFileVersionMS), LOWORD(version_info->dwFileVersionMS),
                                 HIWORD(version_info->dwFileVersionLS), LOWORD(version_info->dwFileVersionLS));
                        module_info.version = version_str;
                        module_info.is_version_662_or_above = IsVersion662OrAbove(version_str);
                        LogInfo("  Version: %s", version_str);
                        LogInfo("  Version 6.6.2+: %s", module_info.is_version_662_or_above ? "Yes" : "No");
                    }
                }
            }
            FARPROC imgui_func = GetProcAddress(module, "ReShadeGetImGuiFunctionTable");
            module_info.has_imgui_support = (imgui_func != nullptr);
            LogInfo("  ImGui Support: %s", imgui_func != nullptr ? "Yes" : "No");
            if (module_info.version.empty()) {
                module_info.is_version_662_or_above = false;
                LogInfo("  Version 6.6.2+: No (version unknown)");
            }
        } else {
            module_info.path = "(path unavailable)";
            LogInfo("Found ReShade module #%d: 0x%p - (path unavailable)", reshade_module_count, module);
        }
        g_reshade_debug_info.modules.push_back(module_info);
    }
    return true;
}
}  // namespace

// Function to detect multiple ReShade versions by scanning all modules
void DetectMultipleReShadeVersions() {
    LogInfo("=== ReShade Module Detection ===");
    g_reshade_debug_info = ReShadeDetectionDebugInfo();

    if (!DetectMultipleReShadeVersions_EnumerateModules()) {
        g_reshade_debug_info.detection_completed = true;
        return;
    }

    const int reshade_module_count = static_cast<int>(g_reshade_debug_info.modules.size());
    LogInfo("=== ReShade Detection Complete ===");
    LogInfo("Total ReShade modules found: %d", reshade_module_count);

    bool has_compatible_version = false;
    for (const auto& m : g_reshade_debug_info.modules) {
        if (m.is_version_662_or_above) {
            has_compatible_version = true;
            LogInfo("Found compatible ReShade version: %s", m.version.c_str());
            break;
        }
    }
    if (!has_compatible_version && !g_reshade_debug_info.modules.empty()) {
        LogWarn("No ReShade modules found with version 6.6.2 or above");
    }

    g_reshade_debug_info.total_modules_found = reshade_module_count;
    g_reshade_debug_info.detection_completed = true;

    if (reshade_module_count > 1) {
        LogWarn("WARNING: Multiple ReShade versions detected! This may cause conflicts.");
        LogWarn("Found %d ReShade modules - only the first one will be used for registration.", reshade_module_count);
        for (size_t i = 0; i < g_reshade_debug_info.modules.size(); ++i) {
            LogWarn("  ReShade module %zu: 0x%p", i + 1, g_reshade_debug_info.modules[i].handle);
        }
    } else if (reshade_module_count == 1) {
        LogInfo("Single ReShade module detected - proceeding with registration.");
    } else {
        LogError("No ReShade modules found! Registration will likely fail.");
        g_reshade_debug_info.error_message = "No ReShade modules found! Registration will likely fail.";
    }
}

// Version compatibility check function
bool CheckReShadeVersionCompatibility() {
    static bool first_time = true;
    if (!first_time) {
        return false;
    }
    first_time = false;
    // This function will be called after registration fails
    // We'll display a helpful error message to the user
    LogError("ReShade addon registration failed - API version not supported");

    // Build debug information string
    std::string debug_info = "ERROR DETAILS:\n";
    debug_info += "• Required API Version: 17 (ReShade 6.6.2+)\n";

    // Check if we have version information
    bool has_version_info = false;
    bool has_compatible_version = false;
    std::string detected_versions;

    if (g_reshade_debug_info.detection_completed && !g_reshade_debug_info.modules.empty()) {
        for (const auto& module : g_reshade_debug_info.modules) {
            if (!module.version.empty()) {
                has_version_info = true;
                if (!detected_versions.empty()) {
                    detected_versions += ", ";
                }
                detected_versions += module.version;

                if (module.is_version_662_or_above) {
                    has_compatible_version = true;
                }
            }
        }
    }

    if (has_version_info) {
        debug_info += "• Detected ReShade Versions: " + detected_versions + "\n";
        debug_info += "• Version 6.6.2+ Compatible: " + std::string(has_compatible_version ? "Yes" : "No") + "\n";
    } else {
        debug_info += "• Your ReShade Version: Unknown (version detection failed)\n";
    }
    debug_info += "• Status: Incompatible\n\n";

    // Add module detection debug information
    if (g_reshade_debug_info.detection_completed) {
        debug_info += "MODULE DETECTION RESULTS:\n";
        debug_info +=
            "• Total ReShade modules found: " + std::to_string(g_reshade_debug_info.total_modules_found) + "\n";

        if (!g_reshade_debug_info.error_message.empty()) {
            debug_info += "• Error: " + g_reshade_debug_info.error_message + "\n";
        }

        if (!g_reshade_debug_info.modules.empty()) {
            debug_info += "• Detected modules:\n";
            for (size_t i = 0; i < g_reshade_debug_info.modules.size(); ++i) {
                const auto& module = g_reshade_debug_info.modules[i];
                debug_info += "  " + std::to_string(i + 1) + ". " + module.path + "\n";
                if (!module.version.empty()) {
                    debug_info += "     Version: " + module.version + "\n";
                    debug_info +=
                        "     Version 6.6.2+: " + std::string(module.is_version_662_or_above ? "Yes" : "No") + "\n";
                } else {
                    debug_info += "     Version: Unknown\n";
                    debug_info += "     Version 6.6.2+: No (version unknown)\n";
                }
                debug_info += "     ImGui Support: " + std::string(module.has_imgui_support ? "Yes" : "No") + "\n";
                debug_info += "     Handle: 0x" + std::to_string(reinterpret_cast<uintptr_t>(module.handle)) + "\n";
            }
        } else {
            debug_info += "• No ReShade modules detected\n";
        }
        debug_info += "\n";
    } else {
        debug_info += "MODULE DETECTION:\n";
        debug_info += "• Detection not completed or failed\n\n";
    }

    debug_info += "SOLUTION:\n";
    debug_info += "1. Download the latest ReShade from: https://reshade.me/\n";
    debug_info += "2. Install ReShade 6.6.2 or newer\n";
    debug_info += "3. Restart your game to load the updated ReShade\n\n";
    debug_info += "This addon uses advanced features that require the newer ReShade API.";

    // Display detailed error message to user
    MessageBoxA(nullptr, debug_info.c_str(), "ReShade Version Incompatible - Update Required",
                MB_OK | MB_ICONERROR | MB_TOPMOST);

    return false;
}

namespace {
void HandleSafemode_WaitForDlls(const std::string& dlls_to_load) {
    if (dlls_to_load.empty()) {
        return;
    }
    LogInfo("Waiting for DLLs to load before Display Commander: %s", dlls_to_load.c_str());
    std::string dlls(dlls_to_load);
    std::replace(dlls.begin(), dlls.end(), ';', ',');
    std::istringstream iss(dlls);
    std::string dll_name;
    const int max_wait_time_ms = 30000;
    const int check_interval_ms = 100;

    while (std::getline(iss, dll_name, ',')) {
        dll_name.erase(0, dll_name.find_first_not_of(" \t\n\r"));
        dll_name.erase(dll_name.find_last_not_of(" \t\n\r") + 1);
        if (dll_name.empty()) {
            continue;
        }
        std::wstring w_dll_name(dll_name.begin(), dll_name.end());
        LogInfo("Waiting for DLL to load: %s", dll_name.c_str());
        int waited_ms = 0;
        bool dll_loaded = false;
        while (waited_ms < max_wait_time_ms) {
            HMODULE hMod = GetModuleHandleW(w_dll_name.c_str());
            if (hMod != nullptr) {
                LogInfo("DLL loaded successfully: %s (0x%p)", dll_name.c_str(), hMod);
                dll_loaded = true;
                break;
            }
            Sleep(check_interval_ms);
            waited_ms += check_interval_ms;
        }
        if (!dll_loaded) {
            LogWarn("Timeout waiting for DLL to load: %s (waited %d ms)", dll_name.c_str(), waited_ms);
        }
    }
    LogInfo("Finished waiting for DLLs to load");
}

void HandleSafemode_ApplySafemodeSettings() {
    LogInfo(
        "Safemode enabled - disabling auto-apply settings, continue rendering, FPS limiter, XInput hooks, MinHook "
        "initialization, and Streamline loading");
    settings::g_mainTabSettings.window_mode.SetValue(static_cast<int>(WindowMode::kNoChanges));
    settings::g_advancedTabSettings.continue_rendering.SetValue(false);
    settings::g_mainTabSettings.fps_limiter_enabled.SetValue(false);
    s_fps_limiter_enabled.store(false);
    ui::monitor_settings::g_setting_auto_apply_resolution.SetValue(false);
    ui::monitor_settings::g_setting_auto_apply_refresh.SetValue(false);
    ui::monitor_settings::g_setting_apply_display_settings_at_start.SetValue(false);
    settings::g_hook_suppression_settings.suppress_xinput_hooks.SetValue(true);
    settings::g_advancedTabSettings.SaveAll();
    LogInfo(
        "Safemode applied - auto-apply settings disabled, continue rendering disabled, FPS limiter disabled "
        "(checkbox off), XInput hooks disabled, MinHook initialization suppressed, Streamline loading disabled, "
        "_nvngx loading disabled, nvapi64 loading disabled, XInput loading disabled");
}

void HandleSafemode_ApplyNonSafemodeSettings() {
    settings::g_advancedTabSettings.safemode.SetValue(false);
    if (!settings::g_experimentalTabSettings.d3d9_flipex_enabled.GetValue()) {
        settings::g_experimentalTabSettings.d3d9_flipex_enabled.SetValue(false);
    }
    if (!settings::g_experimentalTabSettings.d3d9_flipex_enabled_no_reshade.GetValue()) {
        settings::g_experimentalTabSettings.d3d9_flipex_enabled_no_reshade.SetValue(false);
    }
    settings::g_advancedTabSettings.SaveAll();
    LogInfo("Safemode not enabled - setting to 0 for config visibility");
}
}  // namespace

// Safemode function - handles safemode logic
void HandleSafemode() {
    bool safemode_enabled = settings::g_advancedTabSettings.safemode.GetValue();
    std::string dlls_to_load = settings::g_advancedTabSettings.dlls_to_load_before.GetValue();

    HandleSafemode_WaitForDlls(dlls_to_load);

    int delay_ms = settings::g_advancedTabSettings.dll_loading_delay_ms.GetValue();
    if (delay_ms > 0) {
        LogInfo("DLL loading delay: waiting %d ms before installing LoadLibrary hooks", delay_ms);
        Sleep(delay_ms);
        LogInfo("DLL loading delay complete, proceeding with initialization");
    }
    settings::g_advancedTabSettings.dll_loading_delay_ms.SetValue(
        settings::g_advancedTabSettings.dll_loading_delay_ms.GetValue());

    if (safemode_enabled) {
        HandleSafemode_ApplySafemodeSettings();
    } else {
        HandleSafemode_ApplyNonSafemodeSettings();
    }
}

namespace {
void DoInitializationWithoutHwndSafe_Early(HMODULE h_module) {
    if (!IsDisplayCommanderHookingInstance()) return;
    if (utils::setup_high_resolution_timer()) {
        LogInfo("High-resolution timer setup successful");
    } else {
        LogWarn("Failed to setup high-resolution timer");
    }
    LogInfo("DLLMain (DisplayCommander) %lld h_module: 0x%p", utils::get_now_ns(),
            reinterpret_cast<uintptr_t>(h_module));
    settings::LoadAllSettingsAtStartup();
    display_commanderhooks::InstallLoadLibraryHooks();
    LogCurrentLogLevel();
    if (settings::g_advancedTabSettings.disable_dpi_scaling.GetValue()) {
        display_commander::display::dpi::DisableDPIScaling();
        LogInfo("DPI scaling disabled - process is now DPI-aware");
    }
    HandleSafemode();

    bool suppress_pin_module = true;
    display_commander::config::get_config_value_ensure_exists("DisplayCommander.Safemode", "SuppressPinModule",
                                                              suppress_pin_module, false);
    if (!suppress_pin_module) {
        HMODULE pinned_module = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                               reinterpret_cast<LPCWSTR>(h_module), &pinned_module)
            != 0) {
            LogInfo("Module pinned successfully: 0x%p", pinned_module);
            g_module_pinned.store(true);
        } else {
            DWORD error = GetLastError();
            LogWarn("Failed to pin module: 0x%p, Error: %lu", h_module, error);
            g_module_pinned.store(false);
        }
    } else {
        LogInfo("Module pinning suppressed by config (SuppressPinModule=true)");
        g_module_pinned.store(false);
    }

    process_exit_hooks::Initialize();
    LogInfo("DLL initialization complete - DXGI calls now enabled");
    LogInfo("DLL_THREAD_ATTACH: Installing API hooks...");
    display_commanderhooks::InstallApiHooks();
    InstallRealDXGIMinHookHooks();
    OverrideReShadeSettings(nullptr);
}

void DoInitializationWithoutHwndSafe_Late() {
    if (!IsDisplayCommanderHookingInstance()) return;
    // Log all ETW sessions once at addon init for diagnostics (e.g. why DC_ list may be empty in Advanced tab)

    display_commanderhooks::InstallXInputHooks(nullptr);
    display_cache::g_displayCache.Initialize();
    display_initial_state::g_initialDisplayState.CaptureInitialState();
    display_commander::input_remapping::initialize_input_remapping();
    ui::new_ui::InitializeNewUISystem();
    StartContinuousMonitoring();
    StartGPUCompletionMonitoring();
    dxgi::fps_limiter::StartRefreshRateMonitoring();
    std::thread(RunBackgroundAudioMonitor).detach();
    ui::new_ui::InitExperimentalTab();
    display_commanderhooks::keyboard_tracker::Initialize();
    LogInfo("Keyboard tracking system initialized");
}
}  // namespace

void DoInitializationWithoutHwndSafe(HMODULE h_module) {
    DoInitializationWithoutHwndSafe_Early(h_module);
    DoInitializationWithoutHwndSafe_Late();
}

void RegisterReShadeEvents(HMODULE h_module) {
    CALL_GUARD_NO_TS();;
    // Register reshade_overlay event for test code
    reshade::register_event<reshade::addon_event::reshade_overlay>(OnPerformanceOverlay);

    // Register device creation event for D3D9 to D3D9Ex upgrade
    reshade::register_event<reshade::addon_event::create_device>(OnCreateDevice);

    // Capture sync interval on swapchain creation for UI
    reshade::register_event<reshade::addon_event::create_swapchain>(OnCreateSwapchainCapture);

    reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);

    // Register ReShade effect runtime events for input blocking
    reshade::register_event<reshade::addon_event::init_effect_runtime>(OnInitEffectRuntime);
    reshade::register_event<reshade::addon_event::destroy_effect_runtime>(OnDestroyEffectRuntime);
    reshade::register_event<reshade::addon_event::reshade_open_overlay>(OnReShadeOverlayOpen);

    // Defer NVAPI init until after settings are loaded below

    // Register our fullscreen prevention event handler
    // NOTE: Fullscreen prevention is now handled directly in IDXGISwapChain_SetFullscreenState_Detour
    // reshade::register_event<reshade::addon_event::set_fullscreen_state>(OnSetFullscreenState);

    // NVAPI HDR monitor will be started after settings load below if enabled
    // Seed default fps limit snapshot
    // GetFpsLimit removed from proxy, use s_fps_limit directly
    reshade::register_event<reshade::addon_event::present>(OnPresentUpdateBefore);
    reshade::register_event<reshade::addon_event::finish_present>(OnPresentUpdateAfter);

    reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
    reshade::register_event<reshade::addon_event::init_device>(OnInitDevice);

    // Register command list/queue lifecycle events
    reshade::register_event<reshade::addon_event::init_command_list>(OnInitCommandList);
    reshade::register_event<reshade::addon_event::destroy_command_list>(OnDestroyCommandList);
    reshade::register_event<reshade::addon_event::init_command_queue>(OnInitCommandQueue);
    reshade::register_event<reshade::addon_event::destroy_command_queue>(OnDestroyCommandQueue);
    reshade::register_event<reshade::addon_event::execute_command_list>(OnExecuteCommandList);

    // Register swapchain/resource lifecycle events
    reshade::register_event<reshade::addon_event::destroy_swapchain>(OnDestroySwapchain);

    // Register present completion event
    reshade::register_event<reshade::addon_event::finish_present>(OnFinishPresent);

    // Register ReShade effect rendering events
    reshade::register_event<reshade::addon_event::reshade_begin_effects>(OnReShadeBeginEffects);
    reshade::register_event<reshade::addon_event::reshade_finish_effects>(OnReShadeFinishEffects);
    reshade::register_event<reshade::addon_event::reshade_present>(OnReShadePresent);
}

namespace {
enum class ProcessAttachEarlyResult { Continue, RefuseLoad, EarlySuccess };

// Chooses config path (and sets RESHADE_BASE_PATH_OVERRIDE and g_dc_config_directory) in order of priority:
// 1) If .DC_CONFIG_GLOBAL exists next to the addon OR in %LocalAppData%\\Programs\\Display_Commander: use
//    %LocalAppData%\\Programs\\Display_Commander\\Games\\<game_name>
// 2) Else if .DC_CONFIG_IN_DLL exists next to the addon: use addon folder
// 3) Else: use game exe directory
static void ChooseAndSetDcConfigPath(HMODULE h_module) {
    std::wstring config_path_w;
    WCHAR module_path[MAX_PATH] = {};
    if (GetModuleFileNameW(h_module, module_path, MAX_PATH) > 0) {
        std::filesystem::path dll_dir = std::filesystem::path(module_path).parent_path();
        std::error_code ec;

        bool use_global_config = false;
        if (std::filesystem::is_regular_file(dll_dir / L".DC_CONFIG_GLOBAL", ec) && !ec) {
            use_global_config = true;
        } else {
            ec.clear();
            std::filesystem::path dc_root = GetDisplayCommanderAppDataRootPathNoCreate();
            if (!dc_root.empty() && std::filesystem::is_regular_file(dc_root / L".DC_CONFIG_GLOBAL", ec) && !ec) {
                use_global_config = true;
            }
        }
        if (use_global_config) {
            std::filesystem::path base = GetDisplayCommanderAppDataFolder();
            if (!base.empty()) {
                std::string game_name = GetGameNameFromProcess();
                if (game_name.empty()) game_name = "Game";
                config_path_w = (base / L"Games" / std::filesystem::path(game_name)).wstring();
            }
        }
        if (config_path_w.empty() &&
            std::filesystem::is_regular_file(dll_dir / L".DC_CONFIG_IN_DLL", ec) && !ec) {
            config_path_w = dll_dir.wstring();
        }
    }
    if (config_path_w.empty()) {
        WCHAR exe_path[MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) == 0) return;
        WCHAR* last_slash = wcsrchr(exe_path, L'\\');
        if (last_slash == nullptr || last_slash <= exe_path) return;
        *last_slash = L'\0';
        config_path_w = exe_path;
    }
    if (config_path_w.empty()) return;
    SetEnvironmentVariableW(L"RESHADE_BASE_PATH_OVERRIDE", config_path_w.c_str());
    g_dc_config_directory.store(std::make_shared<std::wstring>(config_path_w));
}

std::wstring ProcessAttach_GetConfigDirectoryW() {
    auto dir = g_dc_config_directory.load(std::memory_order_acquire);
    if (dir != nullptr && !dir->empty()) return *dir;
    wchar_t exe_path[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) == 0) return L"";
    return std::filesystem::path(exe_path).parent_path().wstring();
}

void ProcessAttach_DetectReShadeInModules() {
    HMODULE modules[1024];
    DWORD num_modules_bytes = 0;
    if (K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &num_modules_bytes) == 0) return;
    DWORD num_modules =
        (std::min<DWORD>)(num_modules_bytes / sizeof(HMODULE), static_cast<DWORD>(sizeof(modules) / sizeof(HMODULE)));
    for (DWORD i = 0; i < num_modules; i++) {
        if (modules[i] == nullptr) continue;
        FARPROC register_func = GetProcAddress(modules[i], "ReShadeRegisterAddon");
        if (register_func != nullptr) {
            HMODULE expected = nullptr;
            if (g_reshade_module.compare_exchange_strong(expected, modules[i]))
                OutputDebugStringA("ReShadeRegisterAddon found");
            break;
        }
    }
}

// Returns the file's version resource ProductName (wide), or empty if absent. Used to avoid
// loading ReShade again when already loaded, or loading Display Commander (ourselves) again.
// Note: We intentionally ignore the length reported by VerQueryValueW for strings and instead
// rely on the null terminator, since some version blocks report too-small lengths that would
// truncate names like "Display Commander".
std::wstring GetFileProductNameW(const std::wstring& path_w) {
    DWORD ver_handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path_w.c_str(), &ver_handle);
    if (size == 0) return {};
    std::vector<char> buf(size);
    if (!GetFileVersionInfoW(path_w.c_str(), 0, size, buf.data())) return {};
    struct LANGANDCODEPAGE {
        WORD wLanguage;
        WORD wCodePage;
    };
    LANGANDCODEPAGE* p_trans = nullptr;
    UINT trans_len = 0;
    if (!VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&p_trans), &trans_len)
        || !p_trans || trans_len < sizeof(LANGANDCODEPAGE))
        return {};

    auto read_product = [&buf](const wchar_t* sub_block) -> std::wstring {
        void* p_block = nullptr;
        UINT len_ignored = 0;
        if (!VerQueryValueW(buf.data(), sub_block, &p_block, &len_ignored) || !p_block) return {};
        const wchar_t* product = static_cast<const wchar_t*>(p_block);
        // Read until null terminator, with a hard safety cap.
        constexpr size_t kMaxChars = 512;
        size_t str_len = 0;
        while (str_len < kMaxChars && product[str_len] != L'\0') ++str_len;
        if (str_len == 0) return {};
        return std::wstring(product, str_len);
    };

    wchar_t sub_block[64];
    swprintf_s(sub_block, L"\\StringFileInfo\\%04x%04x\\ProductName", p_trans[0].wLanguage, p_trans[0].wCodePage);
    std::wstring result = read_product(sub_block);

    // If the first translation gave a very short string, always try English-US (040904b0) as a fallback;
    // this is what Explorer typically uses for ProductName.
    if (result.size() < 17) {
        const wchar_t* en_us_block = L"\\StringFileInfo\\040904b0\\ProductName";
        std::wstring en_result = read_product(en_us_block);
        if (en_result.size() > result.size()) result = std::move(en_result);
    }

    return result;
}

// When loaded as a .dll proxy (e.g. dxgi.dll, d3d11.dll), detect other Display Commander proxy DLLs
// in the module directory, log the list (unused DLLs detection), then rename them to *.dll.unused.
void RenameUnusedDcProxyDlls(HMODULE h_module) {
    if (h_module == nullptr) return;

    WCHAR module_path_w[MAX_PATH] = {};
    if (GetModuleFileNameW(h_module, module_path_w, MAX_PATH) == 0) return;

    std::filesystem::path self_path(module_path_w);
    if (!self_path.has_filename()) return;

    const std::wstring self_product = GetFileProductNameW(self_path.wstring());
    char self_product_narrow[256] = {};
    if (!self_product.empty()) {
        WideCharToMultiByte(CP_ACP, 0, self_product.c_str(), -1, self_product_narrow,
                            static_cast<int>(sizeof(self_product_narrow)), nullptr, nullptr);
    }
    LogInfo("[main_entry] RenameUnusedDcProxyDlls: self_product: %s", self_product_narrow);
    if (self_product.empty() || _wcsicmp(self_product.c_str(), L"Display Commander") != 0) {
        return;  // Only operate when our own module is identified as Display Commander.
    }
    char self_path_narrow[MAX_PATH] = {};
    WideCharToMultiByte(CP_ACP, 0, self_path.wstring().c_str(), -1, self_path_narrow, MAX_PATH, nullptr, nullptr);
    LogInfo("[main_entry] RenameUnusedDcProxyDlls: self_path: %s", self_path_narrow);

    std::filesystem::path dir = self_path.parent_path();
    if (dir.empty()) return;

    // Detection phase: collect paths of unused DC proxy DLLs.
    std::vector<std::filesystem::path> unused_paths;
    std::error_code ec;
    for (const auto& entry :
         std::filesystem::directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;

        std::filesystem::path path = entry.path();
        if (!path.has_filename()) continue;
        if (std::filesystem::equivalent(path, self_path, ec)) {
            ec.clear();
            continue;  // Never rename the currently loaded proxy DLL.
        }

        std::wstring ext = path.extension().wstring();
        for (auto& c : ext) {
            if (c >= L'A' && c <= L'Z') c += (L'a' - L'A');
        }
        if (ext != L".dll") continue;

        const std::wstring product = GetFileProductNameW(path.wstring());
        char path_narrow[MAX_PATH] = {};
        char product_narrow[256] = {};
        WideCharToMultiByte(CP_ACP, 0, path.wstring().c_str(), -1, path_narrow, MAX_PATH, nullptr, nullptr);
        if (!product.empty()) {
            WideCharToMultiByte(CP_ACP, 0, product.c_str(), -1, product_narrow,
                                static_cast<int>(sizeof(product_narrow)), nullptr, nullptr);
        }
        LogInfo("[main_entry] RenameUnusedDcProxyDlls: path: %s, product: %s", path_narrow, product_narrow);
        if (product.empty() || _wcsicmp(product.c_str(), L"Display Commander") != 0) continue;

        std::filesystem::path new_path = path;
        new_path += L".unused";  // e.g. dxgi.dll.unused
        if (std::filesystem::exists(new_path, ec)) {
            ec.clear();
            continue;  // Don't overwrite existing *.dll.unused files.
        }
        unused_paths.push_back(path);
    }

    // Log detection result (unused DLLs detection).
    const size_t n = unused_paths.size();
    if (n == 0) {
        LogInfo("[main_entry] RenameUnusedDcProxyDlls: no unused DC proxy DLLs detected");
        return;
    }
    LogInfo("[main_entry] RenameUnusedDcProxyDlls: detected %zu unused DC proxy DLL(s)", n);
    for (size_t i = 0; i < n; i++) {
        char path_narrow[MAX_PATH] = {};
        WideCharToMultiByte(CP_ACP, 0, unused_paths[i].wstring().c_str(), -1, path_narrow, MAX_PATH, nullptr, nullptr);
        LogInfo("[main_entry] RenameUnusedDcProxyDlls: unused[%zu]: %s", i, path_narrow);
    }

    // Rename each detected unused DLL.
    for (const std::filesystem::path& path : unused_paths) {
        std::filesystem::path new_path = path;
        new_path += L".unused";
        std::error_code rename_ec;
        std::filesystem::rename(path, new_path, rename_ec);
        if (!rename_ec) {
            char old_narrow[MAX_PATH] = {};
            char new_narrow[MAX_PATH] = {};
            WideCharToMultiByte(CP_ACP, 0, path.wstring().c_str(), -1, old_narrow, MAX_PATH, nullptr, nullptr);
            WideCharToMultiByte(CP_ACP, 0, new_path.wstring().c_str(), -1, new_narrow, MAX_PATH, nullptr, nullptr);
            LogInfo("[main_entry] Renamed unused DC proxy DLL: %s -> %s", old_narrow, new_narrow);
        }
    }
}

void ProcessAttach_LoadLocalAddonDlls(HMODULE h_module) {
    WCHAR addon_path[MAX_PATH];
    if (GetModuleFileNameW(h_module, addon_path, MAX_PATH) <= 0) return;
    std::filesystem::path addon_dir = std::filesystem::path(addon_path).parent_path();
#ifdef _WIN64
    const std::wstring ext_list[] = {L".dc64", L".dc", L".asi"};
#else
    const std::wstring ext_list[] = {L".dc32", L".dc", L".asi"};
#endif
    const std::set<std::wstring> ext_match(ext_list, ext_list + 3);
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(
             addon_dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        std::wstring ext = entry.path().extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        if (!ext_match.contains(ext)) continue;
        const std::wstring path_w = entry.path().wstring();
        const std::wstring product = GetFileProductNameW(path_w);
        if (!product.empty() && _wcsicmp(product.c_str(), L"ReShade") == 0 && g_reshade_module != nullptr) continue;
        if (!product.empty() && _wcsicmp(product.c_str(), L"Display Commander") == 0) continue;
        HMODULE mod = LoadLibraryW(path_w.c_str());
        if (mod != nullptr) {
            std::string name = entry.path().filename().string();
            char msg[384];
            snprintf(msg, sizeof(msg), "[DisplayCommander] Loaded .dc64/.dc32/.dc/.asi DLL: %s\n", name.c_str());
            OutputDebugStringA(msg);
        }
    }
}
// Post-ReShade addon dir: .dc64r / .dc32r / .dcr. Load directly from the addon directory so these behave like
// before-ReShade addons, but with ReShade APIs available.
void ProcessAttach_LoadLocalAddonDllsAfterReShade(HMODULE h_module) {
    WCHAR addon_path[MAX_PATH];
    if (GetModuleFileNameW(h_module, addon_path, MAX_PATH) <= 0) return;
    std::filesystem::path addon_dir = std::filesystem::path(addon_path).parent_path();
#ifdef _WIN64
    const std::wstring ext_list[] = {L".dc64r", L".dcr"};
#else
    const std::wstring ext_list[] = {L".dc32r", L".dcr"};
#endif
    const std::set<std::wstring> ext_match(ext_list, ext_list + 2);
    std::error_code ec;
    std::vector<std::filesystem::path> to_load;
    for (const auto& entry : std::filesystem::directory_iterator(
             addon_dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        std::wstring ext = entry.path().extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        if (!ext_match.contains(ext)) continue;
        to_load.push_back(entry.path());
    }
    if (ec || to_load.empty()) {
        return;
    }
    std::sort(to_load.begin(), to_load.end(),
              [](const std::filesystem::path& a, const std::filesystem::path& b) {
                  return a.filename().wstring() < b.filename().wstring();
              });
    for (const auto& path : to_load) {
        const std::wstring path_w = path.wstring();
        const std::wstring product = GetFileProductNameW(path_w);
        if (!product.empty() && _wcsicmp(product.c_str(), L"ReShade") == 0 && g_reshade_module != nullptr) continue;
        if (!product.empty() && _wcsicmp(product.c_str(), L"Display Commander") == 0) continue;
        HMODULE mod = LoadLibraryW(path_w.c_str());
        if (mod != nullptr) {
            std::string name = path.filename().string();
            char msg[384];
            snprintf(msg, sizeof(msg), "[DisplayCommander] Loaded .dc64r/.dc32r/.dcr DLL (after ReShade): %s\n",
                     name.c_str());
            OutputDebugStringA(msg);
        }
    }
}

void ProcessAttach_DetectEntryPoint(HMODULE h_module, std::wstring& entry_point) {
    entry_point = L"addon";
    WCHAR module_path[MAX_PATH];
    if (GetModuleFileNameW(h_module, module_path, MAX_PATH) <= 0) {
        OutputDebugStringA("[DisplayCommander] Entry point detection: Failed to get module filename\n");
        return;
    }
    std::filesystem::path module_file_path(module_path);
    std::wstring module_name = module_file_path.stem().wstring();
    std::wstring module_name_full = module_file_path.filename().wstring();
    std::transform(module_name.begin(), module_name.end(), module_name.begin(), ::towlower);
    std::transform(module_name_full.begin(), module_name_full.end(), module_name_full.begin(), ::towlower);
    int module_utf8_size = WideCharToMultiByte(CP_UTF8, 0, module_name_full.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (module_utf8_size > 0) {
        std::string module_name_utf8(module_utf8_size - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, module_name_full.c_str(), -1, module_name_utf8.data(), module_utf8_size,
                            nullptr, nullptr);
        char debug_msg[512];
        snprintf(debug_msg, sizeof(debug_msg),
                 "[DisplayCommander] DEBUG: module_name_full='%s', module_name (stem)='%ls'\n",
                 module_name_utf8.c_str(), module_name.c_str());
        OutputDebugStringA(debug_msg);
    }
    struct ProxyDllInfo {
        const wchar_t* name;
        const wchar_t* entry_point_val;
        const char* debug_msg;
        const char* log_msg;
    };
    const ProxyDllInfo proxy_dlls[] = {
        {L"dxgi", L"dxgi.dll", "[DisplayCommander] Entry point detected: dxgi.dll (proxy mode)\n",
         "Display Commander loaded as dxgi.dll proxy - DXGI functions will be forwarded to system dxgi.dll"},
        {L"d3d11", L"d3d11.dll", "[DisplayCommander] Entry point detected: d3d11.dll (proxy mode)\n",
         "Display Commander loaded as d3d11.dll proxy - D3D11 functions will be forwarded to system d3d11.dll"},
        {L"d3d12", L"d3d12.dll", "[DisplayCommander] Entry point detected: d3d12.dll (proxy mode)\n",
         "Display Commander loaded as d3d12.dll proxy - D3D12 functions will be forwarded to system d3d12.dll"},
        {L"version", L"version.dll", "[DisplayCommander] Entry point detected: version.dll (proxy mode)\n",
         "Display Commander loaded as version.dll proxy - Version functions will be forwarded to system version.dll"},
        {L"opengl32", L"opengl32.dll", "[DisplayCommander] Entry point detected: opengl32.dll (proxy mode)\n",
         "Display Commander loaded as opengl32.dll proxy - OpenGL/WGL functions will be forwarded to system "
         "opengl32.dll"},
        {L"dbghelp", L"dbghelp.dll", "[DisplayCommander] Entry point detected: dbghelp.dll (proxy mode)\n",
         "Display Commander loaded as dbghelp.dll proxy - DbgHelp functions will be forwarded to system dbghelp.dll"},
        {L"vulkan-1", L"vulkan-1.dll", "[DisplayCommander] Entry point detected: vulkan-1.dll (proxy mode)\n",
         "Display Commander loaded as vulkan-1.dll proxy - Vulkan functions will be forwarded to system vulkan-1.dll"}};
    for (const auto& proxy : proxy_dlls) {
        if (_wcsicmp(module_name.c_str(), proxy.name) == 0) {
            entry_point = proxy.entry_point_val;
            OutputDebugStringA(proxy.debug_msg);
            return;
        }
    }
    int module_utf8_size2 = WideCharToMultiByte(CP_UTF8, 0, module_name.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (module_utf8_size2 > 0) {
        std::string module_name_utf8(module_utf8_size2 - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, module_name.c_str(), -1, module_name_utf8.data(), module_utf8_size2, nullptr,
                            nullptr);
        char debug_msg[512];
        snprintf(debug_msg, sizeof(debug_msg), "[DisplayCommander] Entry point detected: addon (module: %s)\n",
                 module_name_utf8.c_str());
        OutputDebugStringA(debug_msg);
    } else {
        OutputDebugStringA("[DisplayCommander] Entry point detected: addon\n");
    }
}

bool ProcessAttach_TryLoadReShadeWhenNotLoaded(HMODULE /*h_module*/) {
    OutputDebugStringA("ReShade not loaded");
    // Ensure suppression flags are loaded before any early hook install attempt.
    // This path can run before normal startup loading in some attach flows.
    settings::g_hook_suppression_settings.LoadAll();
    WCHAR executable_path[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, executable_path, MAX_PATH);
    std::filesystem::path game_directory = std::filesystem::path(executable_path).parent_path();
    std::filesystem::path dc_reshade_dir = display_commander::utils::GetReshadeDirectoryForLoading(game_directory);
#ifdef _WIN64
    std::filesystem::path reshade_path = dc_reshade_dir / L"Reshade64.dll";
    const char* dll_name = "Reshade64.dll";
#else
    std::filesystem::path reshade_path = dc_reshade_dir / L"Reshade32.dll";
    const char* dll_name = "Reshade32.dll";
#endif
    auto path_exists = std::filesystem::exists(reshade_path);
    LogInfo("[reshade] path_exists = %d path = %s", path_exists, reshade_path.string().c_str());
    if (!path_exists) {
        return true;
    }
    HMODULE already_loaded = GetModuleHandleW(reshade_path.c_str());
    if (already_loaded != nullptr) {
        HMODULE expected = nullptr;
        if (g_reshade_module.compare_exchange_strong(expected, already_loaded)) {
            char path_narrow[MAX_PATH];
            WideCharToMultiByte(CP_ACP, 0, reshade_path.c_str(), -1, path_narrow, MAX_PATH, nullptr, nullptr);
            char msg[512];
            snprintf(msg, sizeof(msg), "%s already loaded from Documents folder: %s", dll_name, path_narrow);
            OutputDebugStringA(msg);
            return true;
        }
    }
    display_commanderhooks::InstallLoadLibraryHooks();

    SetEnvironmentVariableW(L"RESHADE_DISABLE_LOADING_CHECK", L"1");
    display_commanderhooks::InstallLoadLibraryHooks();
    display_commanderhooks::g_hooked_before_reshade.store(true);
    HMODULE reshade_module = display_commanderhooks::LoadLibraryW_Direct(reshade_path.c_str());
    if (reshade_module != nullptr) {
        HMODULE expected = nullptr;
        if (g_reshade_module.compare_exchange_strong(expected, reshade_module) || expected != nullptr) {
            char path_narrow[MAX_PATH];
            WideCharToMultiByte(CP_ACP, 0, reshade_path.c_str(), -1, path_narrow, MAX_PATH, nullptr, nullptr);
            char msg[512];
            snprintf(msg, sizeof(msg), "%s loaded successfully from Documents folder: %s", dll_name, path_narrow);
            OutputDebugStringA(msg);
            return true;
        }
    }
    DWORD error = GetLastError();
    wchar_t error_msg[512] = {0};
    DWORD msg_len = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error,
                                   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), error_msg,
                                   sizeof(error_msg) / sizeof(wchar_t), nullptr);
    char path_narrow[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, reshade_path.c_str(), -1, path_narrow, MAX_PATH, nullptr, nullptr);
    char msg[1024];
    if (msg_len > 0) {
        while (msg_len > 0 && (error_msg[msg_len - 1] == L'\n' || error_msg[msg_len - 1] == L'\r'))
            error_msg[--msg_len] = L'\0';
        char error_msg_narrow[512];
        WideCharToMultiByte(CP_ACP, 0, error_msg, -1, error_msg_narrow, sizeof(error_msg_narrow), nullptr, nullptr);
        snprintf(msg, sizeof(msg), "Failed to load %s from Documents folder (error %lu: %s): %s %p", dll_name, error,
                 error_msg_narrow, path_narrow, reshade_module);
    } else {
        snprintf(msg, sizeof(msg), "Failed to load %s from Documents folder (error: %lu): %s", dll_name, error,
                 path_narrow);
    }
    OutputDebugStringA(msg);
    MessageBoxA(nullptr, msg, msg, MB_OK | MB_ICONWARNING | MB_TOPMOST);
    return false;
}

void ProcessAttach_NoReShadeModeInit(HMODULE h_module) {
    g_hmodule = h_module;
    auto dc_dir = g_dc_config_directory.load(std::memory_order_acquire);
    display_commander::config::DisplayCommanderConfigManager::GetInstance().Initialize(
        (dc_dir && !dc_dir->empty()) ? std::optional<std::wstring_view>(*dc_dir) : std::nullopt);
    //display_commander::config::DisplayCommanderConfigManager::GetInstance().SetAutoFlushLogs(true);
    utils::initialize_qpc_timing_constants();
    DoInitializationWithoutHwndSafe(h_module);
}

void ProcessAttach_RegisterAndPostInit(HMODULE h_module, const std::wstring& entry_point) {
    DetectMultipleReShadeVersions();
    wchar_t exe_path_buf[MAX_PATH] = {};
    const wchar_t* exe_name_display = L"";
    if (GetModuleFileNameW(nullptr, exe_path_buf, MAX_PATH) > 0) {
        const wchar_t* last_slash = wcsrchr(exe_path_buf, L'\\');
        exe_name_display = (last_slash != nullptr) ? (last_slash + 1) : exe_path_buf;
    }
    char exe_name_utf8[MAX_PATH] = {};
    if (exe_name_display[0] != L'\0') {
        WideCharToMultiByte(CP_UTF8, 0, exe_name_display, -1, exe_name_utf8, static_cast<int>(std::size(exe_name_utf8)),
                            nullptr, nullptr);
    }
    LogInfoDirect(
        "Display Commander v%s - ReShade addon registration successful (API version 17 supported) g_hmodule: "
        "0x%p current module: 0x%p exe: %s",
        DISPLAY_COMMANDER_VERSION_STRING, g_hmodule, GetModuleHandleA(nullptr),
        (exe_name_utf8[0] != '\0') ? exe_name_utf8 : "(unknown)");
    reshade::register_overlay("Display Commander", OnRegisterOverlayDisplayCommander);
    LogInfoDirect("Display Commander overlay registered");
    OutputDebugStringA("[DisplayCommander] DllMain: DLL_PROCESS_ATTACH - Starting entry point detection\n");
    std::string entry_point_utf8;
    int utf8_size = WideCharToMultiByte(CP_UTF8, 0, entry_point.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8_size > 0) {
        entry_point_utf8.resize(utf8_size - 1);
        WideCharToMultiByte(CP_UTF8, 0, entry_point.c_str(), -1, entry_point_utf8.data(), utf8_size, nullptr, nullptr);
    } else {
        entry_point_utf8 = std::string(entry_point.begin(), entry_point.end());
    }
    char debug_msg[512];
    snprintf(debug_msg, sizeof(debug_msg), "[DisplayCommander] Entry point detected: %s\n", entry_point_utf8.c_str());
    LogInfoDirect("Entry point detected: %s", entry_point_utf8.c_str());
    utils::initialize_qpc_timing_constants();
    DoInitializationWithoutHwndSafe(h_module);
    ProcessAttach_LoadLocalAddonDllsAfterReShade(h_module);
    LoadAddonsFromPluginsDirectory();
    if (IsDisplayCommanderHookingInstance()) display_commanderhooks::InstallApiHooks();
}

// Minimum Display Commander version allowed to load (below this we refuse).
static constexpr const char* kDisplayCommanderMinLoadVersion = "0.12.194";

ProcessAttachEarlyResult ProcessAttach_EarlyChecksAndInit(HMODULE h_module) {
    g_hmodule = h_module;
    g_dll_load_time_ns.store(utils::get_now_ns(), std::memory_order_release);
    g_display_commander_state.store(DisplayCommanderState::DC_STATE_UNDECIDED, std::memory_order_release);

    if (display_commander::utils::version_check::CompareVersions(DISPLAY_COMMANDER_VERSION_STRING,
                                                                 kDisplayCommanderMinLoadVersion)
        < 0) {
        char msg[384];
        snprintf(msg, sizeof(msg), "[DisplayCommander] Version %s is below minimum allowed %s - refusing to load.\n",
                 DISPLAY_COMMANDER_VERSION_STRING, kDisplayCommanderMinLoadVersion);
        OutputDebugStringA(msg);
        return ProcessAttachEarlyResult::RefuseLoad;
    }

    // Don't load DC into helper/crash-handler processes (e.g. UnityCrashHandler, PlatformProcess.exe)
    {
        WCHAR exe_path[MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) > 0) {
            const wchar_t* last_slash = wcsrchr(exe_path, L'\\');
            const wchar_t* exe_name = (last_slash != nullptr) ? (last_slash + 1) : exe_path;
            if (is_helper_or_crash_handler_exe(exe_name)) {
                OutputDebugStringA("[DisplayCommander] Refusing to load in helper/crash-handler process.\n");
                return ProcessAttachEarlyResult::RefuseLoad;
            }
        }
    }

    g_display_commander_state.store(DisplayCommanderState::DC_STATE_HOOKED, std::memory_order_release);
    LPSTR command_line = GetCommandLineA();
    if (command_line != nullptr && command_line[0] != '\0') {
        OutputDebugStringA("[DisplayCommander] Command line: ");
        OutputDebugStringA(command_line);
        OutputDebugStringA("\n");
        if (strstr(command_line, "rundll32") != nullptr) {
            OutputDebugStringA("Run32DLL command line detected");
            return ProcessAttachEarlyResult::EarlySuccess;
        }
    } else {
        OutputDebugStringA("[DisplayCommander] Command line: (empty)\n");
    }
    g_shutdown.store(false);
    return ProcessAttachEarlyResult::Continue;
}
}  // namespace

// Returns true if the module path is a known loader. No allocation.
static bool IsLoaderModule(const wchar_t* path) {
    if (!path || !path[0]) return false;
    const wchar_t* name = path;
    const wchar_t* last = wcsrchr(path, L'\\');
    if (last) name = last + 1;
    const wchar_t* last_slash = wcsrchr(path, L'/');
    if (last_slash && last_slash > name) name = last_slash + 1;
    return _wcsicmp(name, L"ntdll.dll") == 0 || _wcsicmp(name, L"kernel32.dll") == 0
           || _wcsicmp(name, L"kernelbase.dll") == 0 || _wcsicmp(name, L"wow64.dll") == 0
           || _wcsicmp(name, L"wow64win.dll") == 0 || _wcsicmp(name, L"wow64cpu.dll") == 0;
}

// Captures the path of the module that requested this DLL to load: first stack frame outside our DLL
// and outside loader modules. Also builds g_dll_load_call_stack_list (all modules seen, outer first, consecutive
// dedup). Safe to call from DllMain; sets g_dll_load_caller_path and g_dll_load_call_stack_list.
static void CaptureDllLoadCallerPath(HMODULE h_our_module) {
    try {
        void* backtrace[256] = {};
        const USHORT n =
            CaptureStackBackTrace(0, static_cast<ULONG>(sizeof(backtrace) / sizeof(backtrace[0])), backtrace, nullptr);
        g_dll_main_backtrace_count =
            (n <= static_cast<USHORT>(kDllMainBacktraceMax)) ? n : static_cast<USHORT>(kDllMainBacktraceMax);
        for (USHORT i = 0; i < g_dll_main_backtrace_count; ++i) g_dll_main_backtrace[i] = backtrace[i];
        wchar_t path_buf[MAX_PATH] = {};
        std::string fallback;
        bool fallback_is_loader = false;
        std::string list_buf;
        std::string last_path;
        for (USHORT i = 0; i < n; ++i) {
            HMODULE hmod = nullptr;
            if (!GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    static_cast<LPCWSTR>(backtrace[i]), &hmod)
                || hmod == nullptr) {
                continue;
            }
            if (hmod == h_our_module) continue;
            if (GetModuleFileNameW(hmod, path_buf, MAX_PATH) == 0) continue;
            const std::string path_str = std::filesystem::path(path_buf).string();
            if (path_str != last_path) {
                last_path = path_str;
                if (!list_buf.empty()) list_buf += '\n';
                list_buf += path_str;
            }
            if (fallback.empty()) {
                fallback = path_str;
                fallback_is_loader = IsLoaderModule(path_buf);
            }
            if (IsLoaderModule(path_buf)) continue;
            g_dll_load_caller_path = path_str;
            g_dll_load_call_stack_list = std::move(list_buf);
            return;
        }
        g_dll_load_call_stack_list = std::move(list_buf);
        if (!fallback_is_loader && !fallback.empty()) g_dll_load_caller_path = std::move(fallback);
    } catch (...) {
        // avoid crashing DllMain
    }
}

// Append current module path (and caller if known) to DisplayCommander.log next to our DLL on every load.
// No-throw; safe to call from DllMain. Does not create directories; only writes if the DLL's dir exists (it always
// does).
static void LogBoot(const std::string& text) {
    try {
        if (g_dll_main_log_path.empty()) return;
        std::ofstream f(g_dll_main_log_path, std::ios::app);
        if (!f) return;
        f << text << "\n";
        f.flush();
    } catch (...) {
        // avoid crashing DllMain
    }
}

static void EnsureDisplayCommanderLogWithModulePath(HMODULE h_module) {
    wchar_t module_path_buf[MAX_PATH] = {};
    if (GetModuleFileNameW(h_module, module_path_buf, MAX_PATH) == 0) return;
    char module_path_narrow[MAX_PATH] = {};
    if (WideCharToMultiByte(CP_ACP, 0, module_path_buf, -1, module_path_narrow,
                            static_cast<int>(sizeof(module_path_narrow)), nullptr, nullptr)
        == 0) {
        return;
    }
    // Always emit to DebugView so the message is visible even when the file cannot be written
    char dbg_buf[MAX_PATH + 128];
    int dbg_len =
        snprintf(dbg_buf, sizeof(dbg_buf), "[DisplayCommander] DisplayCommander module path: %s", module_path_narrow);
    if (!g_dll_load_caller_path.empty() && dbg_len >= 0 && static_cast<size_t>(dbg_len) < sizeof(dbg_buf) - 32) {
        dbg_len += snprintf(dbg_buf + dbg_len, sizeof(dbg_buf) - static_cast<size_t>(dbg_len), " Caller: %s",
                            g_dll_load_caller_path.c_str());
    }
    if (dbg_len >= 0 && static_cast<size_t>(dbg_len) < sizeof(dbg_buf)) {
        snprintf(dbg_buf + dbg_len, sizeof(dbg_buf) - static_cast<size_t>(dbg_len), "\n");
        OutputDebugStringA(dbg_buf);
    }
    std::filesystem::path log_path = std::filesystem::path(module_path_buf).parent_path() / "DisplayCommander.log";
    g_dll_main_log_path = log_path.string();

    std::string module_path_line = std::string("DisplayCommander module path: ") + module_path_narrow;
    if (!g_dll_load_caller_path.empty()) {
        module_path_line += " Caller: " + g_dll_load_caller_path;
    }

    LogBoot(module_path_line);
    LogBoot("(DLL load function call stack will follow after init)");
}

#ifndef IMAGE_DIRECTORY_ENTRY_IMPORT
#define IMAGE_DIRECTORY_ENTRY_IMPORT 1
#endif

// Reads IMAGE_IMPORT_DESCRIPTOR[] from the game executable (main module) in memory and logs each imported DLL name.
// Uses manual PE parsing only (no DbgHelp). Safe to call when we log the PROCESS_ATTACH stack.
static void LogGameExeStaticImports(void (*emit_line)(const std::string&, std::ofstream*), std::ofstream* f) {
    const bool supressed = true;
    if (supressed) return;
    HMODULE const base = GetModuleHandleW(nullptr);
    if (!base) return;
    const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    const auto* const nt =
        reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const BYTE*>(base) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;

    DWORD import_rva = 0;
    DWORD size_of_image = 0;
    if (nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64) {
        const auto* const oh = &nt->OptionalHeader;
        import_rva = oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
        size_of_image = oh->SizeOfImage;
    } else {
        const auto* const oh = reinterpret_cast<const IMAGE_OPTIONAL_HEADER32*>(&nt->OptionalHeader);
        import_rva = oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
        size_of_image = oh->SizeOfImage;
    }
    if (import_rva == 0 || size_of_image == 0) return;

    emit_line("Game executable static imports (DLLs):", f);
    const auto* desc =
        reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(reinterpret_cast<const BYTE*>(base) + import_rva);
    constexpr DWORD kMaxImports = 1024;
    for (DWORD i = 0; i < kMaxImports && desc->Name != 0; ++i, ++desc) {
        DWORD const name_rva = desc->Name;
        if (name_rva >= size_of_image) continue;
        const char* const name = reinterpret_cast<const char*>(reinterpret_cast<const BYTE*>(base) + name_rva);
        size_t len = 0;
        while (len < 260 && name[len]) ++len;
        if (len == 0) continue;
        emit_line(std::string(name, len), f);
    }
}

// Resolves g_dll_main_backtrace to function names via DbgHelp and appends to log + DebugView. Call after init (no
// loader lock).
void ResolveAndLogDllMainFunctionStack() {
    if (g_dll_main_backtrace_count == 0) return;
    const USHORT count = g_dll_main_backtrace_count;
    g_dll_main_backtrace_count = 0;

    auto emit_line = [](const std::string& line, std::ofstream* f) {
        OutputDebugStringA("[DisplayCommander]   ");
        OutputDebugStringA(line.c_str());
        OutputDebugStringA("\n");
        if (f && f->is_open()) *f << "  " << line << "\n";
    };

    OutputDebugStringA("[DisplayCommander] DLL load call stack (functions, outer first):\n");
    std::ofstream f;
    if (!g_dll_main_log_path.empty()) {
        f.open(g_dll_main_log_path, std::ios::app);
        if (f) f << "DLL load call stack (functions, outer first):\n";
    }

    if (!dbghelp_loader::LoadDbgHelp() || !dbghelp_loader::IsDbgHelpAvailable()) {
        for (USHORT i = 0; i < count; ++i) {
            HMODULE hmod = nullptr;
            wchar_t path_buf[MAX_PATH] = {};
            if (GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    static_cast<LPCWSTR>(g_dll_main_backtrace[i]), &hmod)
                && hmod != nullptr && GetModuleFileNameW(hmod, path_buf, MAX_PATH) != 0) {
                char narrow[MAX_PATH] = {};
                if (WideCharToMultiByte(CP_UTF8, 0, path_buf, -1, narrow, static_cast<int>(sizeof(narrow)), nullptr,
                                        nullptr)
                    > 0)
                    emit_line(narrow, &f);
            } else {
                char addr_buf[32];
                snprintf(addr_buf, sizeof(addr_buf), "0x%p", g_dll_main_backtrace[i]);
                emit_line(std::string(addr_buf), &f);
            }
        }
        LogGameExeStaticImports(emit_line, &f);
        return;
    }

    HANDLE process = GetCurrentProcess();
    dbghelp_loader::EnsureSymbolsInitialized(process);

    constexpr size_t kSymBufSize = 512;
    char symbol_buffer[sizeof(SYMBOL_INFO) + kSymBufSize] = {};
    IMAGEHLP_MODULE64 mod_info = {};
    mod_info.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);

    for (USHORT i = 0; i < count; ++i) {
        const DWORD64 addr = reinterpret_cast<DWORD64>(g_dll_main_backtrace[i]);
        PSYMBOL_INFO sym_info = reinterpret_cast<PSYMBOL_INFO>(symbol_buffer);
        sym_info->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym_info->MaxNameLen = kSymBufSize;
        DWORD64 displacement = 0;

        std::string module_name = "?";
        if (dbghelp_loader::SymGetModuleInfo64(process, addr, &mod_info) != FALSE) module_name = mod_info.ModuleName;

        std::string line;
        if (dbghelp_loader::SymFromAddr(process, addr, &displacement, sym_info) != FALSE) {
            line = module_name + "!" + sym_info->Name;
            if (displacement != 0) {
                char disp_buf[24];
                snprintf(disp_buf, sizeof(disp_buf), "+0x%llx", static_cast<unsigned long long>(displacement));
                line += disp_buf;
            }
        } else {
            line = module_name + "!0x";
            char addr_hex[20];
            snprintf(addr_hex, sizeof(addr_hex), "%llx", static_cast<unsigned long long>(addr));
            line += addr_hex;
        }
        emit_line(line, &f);
    }
    LogGameExeStaticImports(emit_line, &f);
}

#if !defined(DISPLAY_COMMANDER_BUILD_EXE)
BOOL APIENTRY DllMain(HMODULE h_module, DWORD fdw_reason, LPVOID lpv_reserved) {
    switch (fdw_reason) {
        case DLL_PROCESS_ATTACH: {
            ChooseAndSetDcConfigPath(h_module);
            CaptureDllLoadCallerPath(h_module);
            EnsureDisplayCommanderLogWithModulePath(h_module);
            static const char* reason = "";
            auto set_process_attached_on_exit = [h_module]() {
                ResolveAndLogDllMainFunctionStack();
                // log current
                WCHAR current_module_path[MAX_PATH] = {0};
                if (GetModuleFileNameW(h_module, current_module_path, MAX_PATH) > 0) {
                    char current_module_path_narrow[MAX_PATH];
                    WideCharToMultiByte(CP_ACP, 0, current_module_path, -1, current_module_path_narrow, MAX_PATH,
                                        nullptr, nullptr);
                    LogInfo("[main_entry] DLL_PROCESS_ATTACH: current module path: %s", current_module_path_narrow);
                }
                LogInfo("[main_entry] DLL_PROCESS_ATTACH: DLL process attach reason: %s, state: %d", reason,
                        static_cast<int>(g_display_commander_state.load(std::memory_order_acquire)));
            };
            struct ScopeGuard {
                std::function<void()> run_;
                explicit ScopeGuard(std::function<void()> fn) : run_(std::move(fn)) {}
                ~ScopeGuard() {
                    if (run_) run_();
                }
            } guard(set_process_attached_on_exit);

            auto dc_dir = g_dc_config_directory.load(std::memory_order_acquire);
            display_commander::config::DisplayCommanderConfigManager::GetInstance().Initialize(
                (dc_dir && !dc_dir->empty()) ? std::optional<std::wstring_view>(*dc_dir) : std::nullopt);
            //display_commander::config::DisplayCommanderConfigManager::GetInstance().SetAutoFlushLogs(true);

            // If loaded as .dll proxy, detect and rename unused DC proxy DLLs in the same directory.
            if (display_commander::utils::IsLoadedWithDLLExtension(static_cast<void*>(h_module))) {
                LogInfo("[main_entry] DLL_PROCESS_ATTACH: RenameUnusedDcProxyDlls");
                RenameUnusedDcProxyDlls(h_module);
            }
            ProcessAttachEarlyResult early = ProcessAttach_EarlyChecksAndInit(h_module);
            if (early == ProcessAttachEarlyResult::RefuseLoad) {
                reason = "RefuseLoad";
                return TRUE;
            }
            if (early == ProcessAttachEarlyResult::EarlySuccess) {
                reason = "EarlySuccess";
                return TRUE;
            }
            ProcessAttach_DetectReShadeInModules();
            ProcessAttach_LoadLocalAddonDlls(h_module);

            std::wstring entry_point;
            ProcessAttach_DetectEntryPoint(h_module, entry_point);

            if ((g_reshade_module == nullptr) && !g_no_reshade_mode.load()) {
                ProcessAttach_TryLoadReShadeWhenNotLoaded(h_module);
            }
            // If ReShade still not loaded, treat as no-ReShade mode (hooks without ReShade overlay)
            if (g_reshade_module == nullptr) {
                const bool was_no_reshade = g_no_reshade_mode.load();
                g_no_reshade_mode.store(true);
                if (!was_no_reshade) {
                    OutputDebugStringA("[main_entry] ReShade not found - entering no-ReShade mode.\n");
                }
            }

            if (g_no_reshade_mode.load()) {
                LogInfo("[main_entry] DLL_PROCESS_ATTACH: No ReShade mode");
                ProcessAttach_NoReShadeModeInit(h_module);
                g_dll_initialization_complete.store(true);
                reason = "NoReShadeMode: ReShade not loaded";
                break;
            }

            if (!FinishAddonRegistration(h_module, nullptr, false)) {
                {
                    char msg[512];
                    snprintf(msg, sizeof(msg), "g_module handle: 0x%p", g_hmodule);
                    reshade::log::message(reshade::log::level::info, msg);
                }
                HMODULE modules[1024];
                DWORD num_modules_bytes = 0;
                if (K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &num_modules_bytes) != 0) {
                    DWORD num_modules = (std::min<DWORD>)(num_modules_bytes / sizeof(HMODULE),
                                                          static_cast<DWORD>(sizeof(modules) / sizeof(HMODULE)));
                    for (DWORD i = 0; i < num_modules; i++) {
                        char msg[512];
                        wchar_t module_name[MAX_PATH];
                        if (GetModuleFileNameW(modules[i], module_name, MAX_PATH) > 0) {
                            snprintf(msg, sizeof(msg), "Module %lu: 0x%p %ls", i, modules[i], module_name);
                        } else {
                            snprintf(msg, sizeof(msg), "Module %lu: 0x%p (failed to get name)", i, modules[i]);
                        }
                        reshade::log::message(reshade::log::level::info, msg);
                    }
                }
                reason = "ReShade register addon failed";
                return TRUE;
            }
            LogInfo("[main_entry] DLL_PROCESS_ATTACH: RegisterAndPostInit");

            ProcessAttach_RegisterAndPostInit(h_module, entry_point);
            LogInfo("[main_entry] DLL_PROCESS_ATTACH: RegisterAndPostInit complete");
            g_dll_initialization_complete.store(true);
            reason = "RegisterAndPostInit complete";

            // RegisterReShadeEvents(h_module);
            break;
        }
        case DLL_THREAD_ATTACH: {
            bool done_initialization = false;
            if (done_initialization) {
                break;
            }
            break;
        }
        case DLL_THREAD_DETACH: {
            // Log exit detection
            // exit_handler::OnHandleExit(exit_handler::ExitSource::DLL_THREAD_DETACH_EVENT, "DLL thread detach");
            break;
        }

        case DLL_PROCESS_DETACH:
            if (g_reshade_module == nullptr) {
                return TRUE;
            }
            LogInfo("DLL_PROCESS_DETACH: DLL process detach");
            g_shutdown.store(true);

            // Log exit detection
            exit_handler::OnHandleExit(exit_handler::ExitSource::DLL_PROCESS_DETACH_EVENT, "DLL process detach");

            // Clean up input blocking system
            // Input blocking cleanup is now handled by Windows message hooks

            // Clean up window procedure hooks
            display_commanderhooks::UninstallWindowProcHooks();

            // Clean up API hooks
            display_commanderhooks::UninstallApiHooks();

            // Clean up Vulkan loader hooks
            UninstallVulkanLoaderHooks();

            // Clean up NvLowLatencyVk hooks
            UninstallNvLowLatencyVkHooks();

            // Clean up continuous monitoring if it's running
            StopContinuousMonitoring();
            StopGPUCompletionMonitoring();

            // Clean up refresh rate monitoring
            dxgi::fps_limiter::StopRefreshRateMonitoring();

            // Clean up NVAPI actual refresh rate monitoring
            display_commander::nvapi::StopNvapiActualRefreshRateMonitoring();

            // Clean up experimental tab threads
            ui::new_ui::CleanupExperimentalTab();

            // Clean up NVAPI instances before shutdown
            if (g_reflexProvider) {
                g_reflexProvider->Shutdown();
            }

            // Clean up PresentMon (must stop ETW session to prevent system-wide resource leaks)
            // ETW sessions are system-wide and persist until explicitly stopped
            // If not stopped, they can interfere with future processes

            // Note: reshade::unregister_addon() will automatically unregister all events and overlays
            // registered by this add-on, so manual unregistration is not needed and can cause issues
            // display_restore::RestoreAllIfEnabled(); // restore display settings on exit

            // Unpin the module before unregistration (only if we actually pinned it)
            if (g_module_pinned.load() && g_hmodule != nullptr) {
                if (FreeLibrary(g_hmodule) != 0) {
                    LogInfo("Module unpinned successfully: 0x%p", g_hmodule);
                } else {
                    DWORD error = GetLastError();
                    LogWarn("Failed to unpin module: 0x%p, Error: %lu", g_hmodule, error);
                }
                g_hmodule = nullptr;
                g_module_pinned.store(false);
            }

            reshade::unregister_addon(h_module);
            // Shutdown DisplayCommander logger (must be last to capture all cleanup messages)
            display_commander::logger::Shutdown();

            break;
    }

    return TRUE;
}
#endif  // !DISPLAY_COMMANDER_BUILD_EXE

// CONTINUOUS RENDERING FUNCTIONS REMOVED - Focus spoofing is now handled by Win32 hooks

// CONTINUOUS RENDERING THREAD REMOVED - Focus spoofing is now handled by Win32 hooks
// This provides a much cleaner and more effective solution
