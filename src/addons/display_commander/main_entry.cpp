// Source Code <Display Commander>
#include "addon.hpp"
#include "audio/audio_management.hpp"
#include "autoclick/autoclick_manager.hpp"
#include "config/display_commander_config.hpp"
#include "display/display_initial_state.hpp"
#include "display/dpi_management.hpp"
#include "exit_handler.hpp"
#include "globals.hpp"
#include "hooks/api_hooks.hpp"
#include "hooks/hid_suppression_hooks.hpp"
#include "hooks/loadlibrary_hooks.hpp"
#include "hooks/window_proc_hooks.hpp"
#include "hooks/windows_hooks/windows_message_hooks.hpp"
#include "hooks/xinput_hooks.hpp"
#include "input_remapping/input_remapping.hpp"
#include "latency/gpu_completion_monitoring.hpp"
#include "latency/reflex_provider.hpp"
#include "latent_sync/refresh_rate_monitor_integration.hpp"
#include "nvapi/nvapi_actual_refresh_rate_monitor.hpp"
#include "nvapi/nvapi_init.hpp"
#include "nvapi/nvidia_profile_search.hpp"
#include "nvapi/run_nvapi_setdword_as_admin.hpp"
#include "process_exit_hooks.hpp"
#include "proxy_dll/dxgi_proxy_init.hpp"
#include "settings/advanced_tab_settings.hpp"
#include "settings/experimental_tab_settings.hpp"
#include "settings/hook_suppression_settings.hpp"
#include "settings/main_tab_settings.hpp"
#include "settings/reshade_tab_settings.hpp"
#include "swapchain_events.hpp"
#include "swapchain_events_power_saving.hpp"
#include "ui/imgui_wrapper_reshade.hpp"
#include "ui/monitor_settings/monitor_settings.hpp"
#include "ui/new_ui/experimental_tab.hpp"
#include "ui/new_ui/main_new_tab.hpp"
#include "ui/new_ui/new_ui_main.hpp"
#include "utils/dc_load_path.hpp"
#include "utils/dc_service_status.hpp"
#include "utils/detour_call_tracker.hpp"
#include "utils/display_commander_logger.hpp"
#include "utils/general_utils.hpp"
#include "utils/helper_exe_filter.hpp"
#include "utils/logging.hpp"
#include "utils/no_inject_windows.hpp"
#include "utils/platform_api_detector.hpp"
#include "utils/process_window_enumerator.hpp"
#include "utils/reshade_load_path.hpp"
#include "utils/steam_achievement_cache.hpp"
#include "utils/timing.hpp"
#include "utils/version_check.hpp"
#include "version.hpp"
#include "widgets/dualsense_widget/dualsense_widget.hpp"

// Libraries <Windows.h>
#include <windows.h>

// Libraries <Windows>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <psapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <sysinfoapi.h>
#include <tlhelp32.h>
#include <winver.h>
#include <wrl/client.h>

// Libraries <standard C++>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <reshade.hpp>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// Forward declarations for ReShade event handlers
void OnInitEffectRuntime(reshade::api::effect_runtime* runtime);
bool OnReShadeOverlayOpen(reshade::api::effect_runtime* runtime, bool open, reshade::api::input_source source);
// Note: OnInitDevice, OnDestroySwapchain, OnDestroyResource are declared in swapchain_events.hpp
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

// Forward declaration for multiple Display Commander detection
// Returns true if multiple versions detected (should refuse to load), false otherwise
bool DetectMultipleDisplayCommanderVersions();

// Forward declaration for safemode function
void HandleSafemode();

// Forward declaration for loading addons from Plugins directory
void LoadAddonsFromPluginsDirectory();

// Standalone settings UI when .NO_RESHADE (no ReShade loaded); implemented in ui/cli_standalone_ui.cpp
void RunStandaloneSettingsUI(HINSTANCE hInst);
void RunStandaloneGamesOnlyUI(HINSTANCE hInst);

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
    CALL_GUARD(utils::get_now_ns());
    if (runtime != nullptr && should_skip_addon_injection_for_window(static_cast<HWND>(runtime->get_hwnd()))) {
        return;  // Don't draw DC UI for no-inject windows (e.g. independent standalone UI)
    }
    const bool show_display_commander_ui = settings::g_mainTabSettings.show_display_commander_ui.GetValue();
    // Avoid displaying UI twice
    if (show_display_commander_ui) {
        settings::g_mainTabSettings.show_display_commander_ui.SetValue(false);
    }
    // Update UI draw time for auto-click optimization
    if (enabled_experimental_features) {
        autoclick::UpdateLastUIDrawTime();
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
    CALL_GUARD(utils::get_now_ns());
    // Command list initialization tracking
    if (cmd_list == nullptr) {
        return;
    }
    // Add any initialization logic here if needed
}

void OnDestroyCommandList(reshade::api::command_list* cmd_list) {
    CALL_GUARD(utils::get_now_ns());
    // Command list destruction tracking
    if (cmd_list == nullptr) {
        return;
    }
    // Add any cleanup logic here if needed
}

void OnInitCommandQueue(reshade::api::command_queue* queue) {
    CALL_GUARD(utils::get_now_ns());
    // Command queue initialization tracking
    if (queue == nullptr) {
        return;
    }
    // Add any initialization logic here if needed
}

void OnDestroyCommandQueue(reshade::api::command_queue* queue) {
    CALL_GUARD(utils::get_now_ns());
    // Command queue destruction tracking
    if (queue == nullptr) {
        return;
    }
    // Add any cleanup logic here if needed
}

void OnExecuteCommandList(reshade::api::command_queue* queue, reshade::api::command_list* cmd_list) {
    CALL_GUARD(utils::get_now_ns());
    // Command list execution tracking
    if (queue == nullptr || cmd_list == nullptr) {
        return;
    }
    // Add any tracking logic here if needed
}

void OnFinishPresent(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain) {
    CALL_GUARD(utils::get_now_ns());
    // Present completion tracking
    if (queue == nullptr || swapchain == nullptr) {
        return;
    }
    if (should_skip_addon_injection_for_window(static_cast<HWND>(swapchain->get_hwnd()))) {
        return;
    }
    // Add any tracking logic here if needed
}

namespace {
// Apply Display Commander brightness (0-500%) via ReShade effect DisplayCommander_Control.fx.
// No-op if the effect is not loaded (e.g. user has not added DC effect path or reloaded effects).
void ApplyDisplayCommanderBrightness(reshade::api::effect_runtime* runtime) {
    if (runtime == nullptr) {
        return;
    }
    if (!settings::g_mainTabSettings.brightness_autohdr_section_enabled.GetValue()) {
        return;
    }
    const float percent = settings::g_mainTabSettings.brightness_percent.GetValue();
    const float multiplier = percent / 100.0f;
    const reshade::api::effect_technique tech = runtime->find_technique("DisplayCommander_Control.fx", "Brightness");
    if (tech == 0) {
        return;  // Effect not loaded
    }
    const reshade::api::effect_uniform_variable var =
        runtime->find_uniform_variable("DisplayCommander_Control.fx", "Brightness");
    if (var == 0) {
        return;
    }
    // Decode = swapchain colorspace (default 1 = scRGB); Encode = brightness color space
    const int32_t decode_colorspace = static_cast<int32_t>(settings::g_mainTabSettings.swapchain_colorspace.GetValue());
    const int32_t encode_colorspace =
        static_cast<int32_t>(settings::g_mainTabSettings.brightness_colorspace.GetValue());
    const reshade::api::effect_uniform_variable var_decode =
        runtime->find_uniform_variable("DisplayCommander_Control.fx", "DECODE_METHOD");
    if (var_decode != 0) {
        runtime->set_uniform_value_int(var_decode, decode_colorspace);
    }
    const reshade::api::effect_uniform_variable var_encode =
        runtime->find_uniform_variable("DisplayCommander_Control.fx", "ENCODE_METHOD");
    if (var_encode != 0) {
        runtime->set_uniform_value_int(var_encode, encode_colorspace);
    }
    runtime->set_uniform_value_float(var, multiplier);
    const float gamma_val = settings::g_mainTabSettings.gamma_value.GetValue();
    const reshade::api::effect_uniform_variable var_gamma =
        runtime->find_uniform_variable("DisplayCommander_Control.fx", "Gamma");
    if (var_gamma != 0) {
        runtime->set_uniform_value_float(var_gamma, gamma_val);
    }
    const float contrast_val = settings::g_mainTabSettings.contrast_value.GetValue();
    const reshade::api::effect_uniform_variable var_contrast =
        runtime->find_uniform_variable("DisplayCommander_Control.fx", "Contrast");
    if (var_contrast != 0) {
        runtime->set_uniform_value_float(var_contrast, contrast_val);
    }
    const float saturation_val = settings::g_mainTabSettings.saturation_value.GetValue();
    const reshade::api::effect_uniform_variable var_saturation =
        runtime->find_uniform_variable("DisplayCommander_Control.fx", "Saturation");
    if (var_saturation != 0) {
        runtime->set_uniform_value_float(var_saturation, saturation_val);
    }
    const float hue_val = settings::g_mainTabSettings.hue_degrees.GetValue();
    const reshade::api::effect_uniform_variable var_hue =
        runtime->find_uniform_variable("DisplayCommander_Control.fx", "HueDegrees");
    if (var_hue != 0) {
        runtime->set_uniform_value_float(var_hue, hue_val);
    }
    const reshade::api::effect_uniform_variable var_extra_gamma22 =
        runtime->find_uniform_variable("DisplayCommander_Control.fx", "ExtraGamma22Decode");
    if (var_extra_gamma22 != 0) {
        runtime->set_uniform_value_int(var_extra_gamma22, 0);
    }
    // Enable technique when any display tweak is non-neutral, or when decode/encode is not Auto (effect must run
    // so that decode -> process -> encode is applied even at brightness 100%).
    const bool need_decode_encode_pass = (decode_colorspace != 0 || encode_colorspace != 0);
    runtime->set_technique_state(tech, multiplier != 1.0f || gamma_val != 1.0f || contrast_val != 1.0f
                                           || saturation_val != 1.0f || hue_val != 0.0f || need_decode_encode_pass);
}

// Apply AutoHDR: when enabled, run DisplayCommander_PerceptualBoost.fx (SpecialK_PerceptualBoost). Uses
// Color Space (brightness_colorspace) for both DECODE_METHOD and ENCODE_METHOD. Requires Generic RenoDX to
// upgrade SDR->HDR.
void ApplyDisplayCommanderAutoHdr(reshade::api::effect_runtime* runtime) {
    if (runtime == nullptr) {
        return;
    }
    if (!settings::g_mainTabSettings.brightness_autohdr_section_enabled.GetValue()) {
        return;
    }
    const bool auto_hdr = settings::g_mainTabSettings.auto_hdr.GetValue();
    const reshade::api::effect_technique tech =
        runtime->find_technique("DisplayCommander_PerceptualBoost.fx", "SpecialK_PerceptualBoost");
    if (tech == 0) {
        return;  // Effect not loaded
    }
    if (auto_hdr) {
        const int32_t colorspace = static_cast<int32_t>(settings::g_mainTabSettings.brightness_colorspace.GetValue());
        const reshade::api::effect_uniform_variable var_decode =
            runtime->find_uniform_variable("DisplayCommander_PerceptualBoost.fx", "DECODE_METHOD");
        if (var_decode != 0) {
            runtime->set_uniform_value_int(var_decode, colorspace);
        }
        const reshade::api::effect_uniform_variable var_encode =
            runtime->find_uniform_variable("DisplayCommander_PerceptualBoost.fx", "ENCODE_METHOD");
        if (var_encode != 0) {
            runtime->set_uniform_value_int(var_encode, colorspace);
        }
        const reshade::api::effect_uniform_variable var_strength =
            runtime->find_uniform_variable("DisplayCommander_PerceptualBoost.fx", "EffectStrength_P3");
        if (var_strength != 0) {
            const float strength = settings::g_mainTabSettings.auto_hdr_strength.GetValue();
            runtime->set_uniform_value_float(var_strength, strength);
        }
    }
    runtime->set_technique_state(tech, auto_hdr);
}
}  // namespace

void OnReShadeBeginEffects(reshade::api::effect_runtime* runtime, reshade::api::command_list* cmd_list,
                           reshade::api::resource_view rtv, reshade::api::resource_view rtv_srgb) {
    CALL_GUARD(utils::get_now_ns());
    if (runtime == nullptr || cmd_list == nullptr) {
        return;
    }
    if (should_skip_addon_injection_for_window(static_cast<HWND>(runtime->get_hwnd()))) {
        return;
    }
    // Brightness is applied from OnReShadePresent to avoid modifying technique state/uniforms
    // during the effect loop (which can cause crashes).
}

void OnReShadeFinishEffects(reshade::api::effect_runtime* runtime, reshade::api::command_list* cmd_list,
                            reshade::api::resource_view rtv, reshade::api::resource_view rtv_srgb) {
    if (IsDisplayCommanderHookingInstance()) display_commanderhooks::InstallApiHooks();
    CALL_GUARD(utils::get_now_ns());
    // ReShade effects finish tracking
    if (runtime == nullptr || cmd_list == nullptr) {
        return;
    }
    if (should_skip_addon_injection_for_window(static_cast<HWND>(runtime->get_hwnd()))) {
        return;
    }
    // Add any tracking logic here if needed
}

void OnReShadePresent(reshade::api::effect_runtime* runtime) {
    if (runtime == nullptr) {
        return;
    }
    if (should_skip_addon_injection_for_window(static_cast<HWND>(runtime->get_hwnd()))) {
        return;
    }
    // Apply brightness and AutoHDR for the next frame. Safe to set technique state and uniforms here
    // (after effects have been rendered this frame, before the next frame).
    ApplyDisplayCommanderBrightness(runtime);
    ApplyDisplayCommanderAutoHdr(runtime);
}

namespace {
void OnInitEffectRuntime_ExtractShadersOnce() {
    CALL_GUARD(utils::get_now_ns());
    static std::atomic<bool> shader_extract_done{false};
    if (shader_extract_done.exchange(true)) {
        return;
    }
    constexpr int IDR_CONTROL_FX = 300;
    constexpr int IDR_COLOR_FXH = 301;
    constexpr int IDR_RESHADE_FXH = 302;
    constexpr int IDR_PERCEPTUALBOOST_FX = 303;

    auto extract_resource = [](int res_id, const wchar_t* filename_wide, const char* filename_utf8) -> bool {
        HRSRC hRes = FindResourceA(g_hmodule, MAKEINTRESOURCE(res_id), RT_RCDATA);
        if (hRes == nullptr) return false;
        HGLOBAL hLoaded = LoadResource(g_hmodule, hRes);
        if (hLoaded == nullptr) return false;
        const void* pData = LockResource(hLoaded);
        const DWORD size = SizeofResource(g_hmodule, hRes);
        if (pData == nullptr || size == 0) return false;

        auto write_to = [&pData, size](const std::filesystem::path& dest_path) -> bool {
            std::error_code ec2;
            std::filesystem::create_directories(dest_path.parent_path(), ec2);
            if (ec2) return false;
            std::ofstream of(dest_path, std::ios::binary);
            if (!of) return false;
            of.write(static_cast<const char*>(pData), static_cast<std::streamsize>(size));
            return of.good();
        };

        wchar_t addon_path[MAX_PATH] = {};
        if (GetModuleFileNameW(g_hmodule, addon_path, MAX_PATH) == 0) return false;
        std::filesystem::path addon_dir = std::filesystem::path(addon_path).parent_path();

        bool any_written = false;
        wchar_t localappdata_path[MAX_PATH] = {};
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localappdata_path))) {
            std::filesystem::path la_dest = std::filesystem::path(localappdata_path) / L"Programs"
                                            / L"Display_Commander" / L"Reshade" / L"Shaders" / L"DisplayCommander"
                                            / filename_wide;
            if (write_to(la_dest)) any_written = true;
        }
        std::filesystem::path dc_dest =
            addon_dir / L"Display_Commander" / L"Reshade" / L"Shaders" / L"DisplayCommander" / filename_wide;
        if (write_to(dc_dest)) any_written = true;

        char base_path[512] = {};
        size_t base_size = sizeof(base_path);
        reshade::get_reshade_base_path(base_path, &base_size);
        std::filesystem::path reshade_base(base_path);
        std::filesystem::path reshade_dest = reshade_base / "Shaders" / "DisplayCommander" / filename_utf8;
        if (std::filesystem::exists(reshade_base / "Shaders") && std::filesystem::is_directory(reshade_base / "Shaders")
            && write_to(reshade_dest)) {
            any_written = true;
        }
        return any_written;
    };

    if (extract_resource(IDR_CONTROL_FX, L"DisplayCommander_Control.fx", "DisplayCommander_Control.fx")) {
        LogInfo(
            "DisplayCommander shaders extracted to Reshade\\Shaders\\DisplayCommander (e.g. "
            "%%LOCALAPPDATA%%\\Programs\\Display_Commander\\Reshade\\Shaders\\DisplayCommander).");
    }
    extract_resource(IDR_COLOR_FXH, L"color.fxh", "color.fxh");
    extract_resource(IDR_RESHADE_FXH, L"ReShade.fxh", "ReShade.fxh");
    extract_resource(IDR_PERCEPTUALBOOST_FX, L"DisplayCommander_PerceptualBoost.fx",
                     "DisplayCommander_PerceptualBoost.fx");
}

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
    if (game_window == nullptr || should_skip_addon_injection_for_window(game_window)) {
        return;  // Skip init for no-inject windows (e.g. independent UI)
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
    LogInfo("[OnInitEffectRuntime] before autoclick start (enabled_experimental_features=%d)",
            enabled_experimental_features ? 1 : 0);
    if (enabled_experimental_features) {
        autoclick::StartAutoClickThread();
        autoclick::StartUpDownKeyPressThread();
        autoclick::StartButtonOnlyPressThread();
    }
}
}  // namespace

void OnInitEffectRuntime(reshade::api::effect_runtime* runtime) {
    LogInfo("[OnInitEffectRuntime] entry");
    CALL_GUARD(utils::get_now_ns());
    if (runtime == nullptr) {
        LogInfo("[OnInitEffectRuntime] runtime is null, returning");
        return;
    }
    AddReShadeRuntime(runtime);
    LogInfo("[OnInitEffectRuntime] after AddReShadeRuntime");

    OnInitEffectRuntime_ExtractShadersOnce();
    LogInfo("[OnInitEffectRuntime] after shader extract block");
    LogInfo("[OnInitEffectRuntime] ReShade effect runtime initialized - Input blocking now available");

    OnInitEffectRuntime_StartRefreshRateMonitoringIfNeeded();
    OnInitEffectRuntime_InitWithHwndOnce(runtime);

    LogInfo("[OnInitEffectRuntime] exit");
}

// ReShade overlay event handler for input blocking
bool OnReShadeOverlayOpen(reshade::api::effect_runtime* runtime, bool open, reshade::api::input_source source) {
    CALL_GUARD(utils::get_now_ns());

    if (runtime != nullptr && should_skip_addon_injection_for_window(static_cast<HWND>(runtime->get_hwnd()))) {
        return false;  // Don't run addon logic for this window (e.g. independent UI)
    }

    if (open) {
        LogInfo("ReShade overlay opened - Input blocking active");
        // When ReShade overlay opens, we can also use its input blocking
        if (runtime != nullptr) {
            AddReShadeRuntime(runtime);
        }
    } else {
        LogInfo("ReShade overlay closed - Input blocking inactive");
    }

    // Update auto-click UI state for optimization
    if (enabled_experimental_features) {
        autoclick::UpdateUIOverlayState(open);
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
    CALL_GUARD(utils::get_now_ns());
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
    overlay_wrapper.SetNextWindowSize(ImVec2(fixed_width, 0.0f), ImGuiCond_Always);
    bool window_open = true;
    if (overlay_wrapper.Begin("Display Commander", &window_open,
                              ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize)) {
        if (enabled_experimental_features) {
            autoclick::UpdateLastUIDrawTime();
        }
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

void OnSteamAchievementOverlay(reshade::api::effect_runtime* /*runtime*/) {
    // Bump state is updated only by the continuous-monitoring thread (RefreshSteamAchievementCacheFromBackground).
    // Main thread only reads cached state and draws the overlay; no Steam API or blocking work here.
    const int64_t now_ns = utils::get_now_ns();
    if (!display_commander::utils::IsSteamAchievementBumpActiveNonBlocking(now_ns)) {
        return;
    }
    int bump_unlocked = 0;
    int bump_total = 0;
    display_commander::utils::GetSteamAchievementBumpDisplayNonBlocking(&bump_unlocked, &bump_total);
    char bump_display_name[256] = {};
    char bump_description[512] = {};
    char bump_debug[1024] = {};
    display_commander::utils::GetSteamAchievementBumpTextNonBlocking(bump_display_name, sizeof(bump_display_name),
                                                                     bump_description, sizeof(bump_description),
                                                                     bump_debug, sizeof(bump_debug));
    float vertical_spacing = settings::g_mainTabSettings.overlay_vertical_spacing.GetValue();
    float horizontal_spacing = settings::g_mainTabSettings.overlay_horizontal_spacing.GetValue();
    const bool performance_overlay_shown = settings::g_mainTabSettings.show_test_overlay.GetValue();
    float steam_y = 10.0f + vertical_spacing;
    if (performance_overlay_shown) {
        steam_y += 75.0f;
    }
    float bg_alpha = settings::g_mainTabSettings.overlay_background_alpha.GetValue();
    display_commander::ui::ImGuiWrapperReshade overlay_wrapper;
    overlay_wrapper.SetNextWindowPos(ImVec2(10.0f + horizontal_spacing, steam_y), ImGuiCond_Always, ImVec2(0.f, 0.f));
    overlay_wrapper.SetNextWindowBgAlpha(bg_alpha);
    overlay_wrapper.SetNextWindowSize(ImVec2(800, 0), ImGuiCond_Always);
    if (overlay_wrapper.Begin("Steam Achievements", nullptr,
                              ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize
                                  | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
                                  | ImGuiWindowFlags_AlwaysAutoResize)) {
        if (bump_display_name[0] != '\0') {
            overlay_wrapper.TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Achievement unlocked: %s", bump_display_name);
        } else {
            overlay_wrapper.TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Achievement unlocked!");
        }
        if (bump_description[0] != '\0') {
            overlay_wrapper.TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "%s", bump_description);
        }
        overlay_wrapper.TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%d / %d achievements", bump_unlocked, bump_total);
        if (bump_debug[0] != '\0') {
            std::string overlay_debug;
            overlay_debug.reserve(static_cast<size_t>(std::strlen(bump_debug)));
            for (const char* line = bump_debug; line != nullptr && *line != '\0';) {
                const char* end = std::strchr(line, '\n');
                const size_t len = end ? static_cast<size_t>(end - line) : std::strlen(line);
                const std::string line_str(line, len);
                if (!line_str.empty() && line_str.find("SteamUserStats export not found") == std::string::npos) {
                    if (!overlay_debug.empty()) overlay_debug += '\n';
                    overlay_debug += line_str;
                }
                line = end ? end + 1 : line + len;
            }
            if (!overlay_debug.empty()) {
                overlay_wrapper.TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", overlay_debug.c_str());
            }
        }
    }
    overlay_wrapper.End();
}

void OnPerformanceOverlay(reshade::api::effect_runtime* runtime) {
    CALL_GUARD(utils::get_now_ns());
    if (runtime != nullptr) {
        HWND window = static_cast<HWND>(runtime->get_hwnd());
        if (should_skip_addon_injection_for_window(window)) {
            return;
        }
    }
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

    if (settings::g_advancedTabSettings.show_steam_achievement_notifications.GetValue()) {
        OnSteamAchievementOverlay(runtime);
    }
    if (!settings::g_mainTabSettings.show_test_overlay.GetValue()) {
        return;
    }
    OnPerformanceOverlay_TestWindow(runtime, show_tooltips);
}
}  // namespace

namespace {
// Helpers for OverrideReShadeSettings - each handles one logical block.
void OverrideReShadeSettings_WindowConfig() {
    std::string window_config;
    size_t value_size = 0;
    if ((g_reshade_module != nullptr)
        && reshade::get_config_value(nullptr, "OVERLAY", "Window", nullptr, &value_size)) {
        window_config.resize(value_size);
        if ((g_reshade_module != nullptr)
            && reshade::get_config_value(nullptr, "OVERLAY", "Window", window_config.data(), &value_size)) {
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
        reshade::set_config_value(nullptr, "OVERLAY", "Window", window_config.c_str(), window_config.size());
        LogInfo("Updated ReShade Window config with Display Commander and RenoDX docking settings");
    }
}

void OverrideReShadeSettings_TutorialAndUpdates() {
    reshade::set_config_value(nullptr, "OVERLAY", "TutorialProgress", 4);
    reshade::set_config_value(nullptr, "GENERAL", "CheckForUpdates", 0);
    LogInfo("ReShade settings override - CheckForUpdates set to 0 (disabled)");
    if (settings::g_reshadeTabSettings.suppress_reshade_clock.GetValue()) {
        reshade::set_config_value(nullptr, "OVERLAY", "ShowClock", 0);
        LogInfo("ReShade settings override - ShowClock set to 0 (disabled)");
    }
}

void OverrideReShadeSettings_LoadFromDllMainOnce() {
    bool load_from_dll_main_set_once = false;
    display_commander::config::get_config_value("DisplayCommander", "LoadFromDllMainSetOnce",
                                                load_from_dll_main_set_once);
    if (!load_from_dll_main_set_once) {
        int32_t current_reshade_value = 0;
        reshade::get_config_value(nullptr, "ADDON", "LoadFromDllMain", current_reshade_value);
        LogInfo("ReShade settings override - LoadFromDllMain current ReShade value: %d", current_reshade_value);
        LogInfo("ReShade settings override - LoadFromDllMain set to 0 (first time)");
        display_commander::config::set_config_value("DisplayCommander", "LoadFromDllMainSetOnce", true);
        display_commander::config::save_config("LoadFromDllMainSetOnce flag set");
        LogInfo("ReShade settings override - LoadFromDllMainSetOnce flag saved to DisplayCommander config");
    } else {
        LogInfo("ReShade settings override - LoadFromDllMain already set to 0 previously, skipping");
    }
}

void OverrideReShadeSettings_AddDisplayCommanderPaths() {
    if (!settings::g_mainTabSettings.brightness_autohdr_section_enabled.GetValue()) {
        return;
    }
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

    auto addPathToSearchPaths = [](const char* section, const char* key,
                                   const std::filesystem::path& path_to_add) -> bool {
        char buffer[4096] = {0};
        size_t buffer_size = sizeof(buffer);
        std::vector<std::string> existing_paths;
        if ((g_reshade_module != nullptr) && reshade::get_config_value(nullptr, section, key, buffer, &buffer_size)) {
            const char* ptr = buffer;
            while (*ptr != '\0' && ptr < buffer + buffer_size) {
                std::string path(ptr);
                if (!path.empty()) {
                    existing_paths.push_back(path);
                }
                ptr += path.length() + 1;
            }
        }
        std::string path_str = path_to_add.string();
        path_str += "\\**";
        auto normalizeForComparison = [](const std::string& path) -> std::string {
            std::string normalized = path;
            if (normalized.length() >= 3 && normalized.substr(normalized.length() - 3) == "\\**") {
                normalized = normalized.substr(0, normalized.length() - 3);
            }
            return normalized;
        };
        std::string normalized_path = normalizeForComparison(path_str);
        for (const auto& existing_path : existing_paths) {
            std::string normalized_existing = normalizeForComparison(existing_path);
            if (normalized_path.length() == normalized_existing.length()
                && std::equal(normalized_path.begin(), normalized_path.end(), normalized_existing.begin(),
                              [](char a, char b) { return std::tolower(a) == std::tolower(b); })) {
                LogInfo("Path already exists in ReShade %s::%s: %s", section, key, normalized_path.c_str());
                return false;
            }
        }
        existing_paths.push_back(path_str);
        std::string combined;
        for (const auto& path : existing_paths) {
            combined += path;
            combined += '\0';
        }
        reshade::set_config_value(nullptr, section, key, combined.c_str(), combined.size());
        LogInfo("Added path to ReShade %s::%s: %s", section, key, path_str.c_str());
        return true;
    };

    addPathToSearchPaths("GENERAL", "EffectSearchPaths", shaders_dir);
    addPathToSearchPaths("GENERAL", "TextureSearchPaths", textures_dir);
}

void OverrideReShadeSettings_RemoveDisplayCommanderPaths() {
    std::filesystem::path dc_base_dir = GetDisplayCommanderReshadeRootFolder();
    if (dc_base_dir.empty()) {
        LogWarn("Failed to get DC Reshade root path, skipping ReShade path removal");
        return;
    }
    std::filesystem::path shaders_dir = dc_base_dir / L"Shaders";
    std::filesystem::path textures_dir = dc_base_dir / L"Textures";

    auto normalizeForComparison = [](const std::string& path) -> std::string {
        std::string normalized = path;
        if (normalized.length() >= 3 && normalized.substr(normalized.length() - 3) == "\\**") {
            normalized = normalized.substr(0, normalized.length() - 3);
        }
        return normalized;
    };
    std::string shaders_norm = normalizeForComparison(shaders_dir.string());
    std::string textures_norm = normalizeForComparison(textures_dir.string());

    auto pathMatches = [](const std::string& normalized_existing, const std::string& normalized_target) -> bool {
        return normalized_existing.length() == normalized_target.length()
               && std::equal(normalized_existing.begin(), normalized_existing.end(), normalized_target.begin(),
                             [](char a, char b) {
                                 return std::tolower(static_cast<unsigned char>(a))
                                        == std::tolower(static_cast<unsigned char>(b));
                             });
    };

    auto removePathFromSearchPaths = [&](const char* section, const char* key,
                                         const std::string& normalized_target) -> bool {
        char buffer[4096] = {0};
        size_t buffer_size = sizeof(buffer);
        std::vector<std::string> existing_paths;
        if ((g_reshade_module == nullptr) || !reshade::get_config_value(nullptr, section, key, buffer, &buffer_size)) {
            return false;
        }
        const char* ptr = buffer;
        while (*ptr != '\0' && ptr < buffer + buffer_size) {
            std::string path(ptr);
            if (!path.empty()) {
                std::string normalized = normalizeForComparison(path);
                if (!pathMatches(normalized, normalized_target)) {
                    existing_paths.push_back(path);
                }
            }
            ptr += path.length() + 1;
        }
        std::string combined;
        for (const auto& path : existing_paths) {
            combined += path;
            combined += '\0';
        }
        reshade::set_config_value(nullptr, section, key, combined.c_str(), combined.size());
        LogInfo("Removed DC path from ReShade %s::%s (target was: %s)", section, key, normalized_target.c_str());
        return true;
    };

    removePathFromSearchPaths("GENERAL", "EffectSearchPaths", shaders_norm);
    removePathFromSearchPaths("GENERAL", "TextureSearchPaths", textures_norm);
}
}  // namespace

// Override ReShade settings to set tutorial as viewed and disable auto updates
void OverrideReShadeSettings() {
    if (g_reshade_module == nullptr) {
        return;  // No-ReShade mode or ReShade not loaded; skip ReShade config override
    }
    LogInfo("Overriding ReShade settings - Setting tutorial as viewed and disabling auto updates");

    OverrideReShadeSettings_WindowConfig();
    OverrideReShadeSettings_TutorialAndUpdates();
    OverrideReShadeSettings_LoadFromDllMainOnce();
    if (settings::g_mainTabSettings.brightness_autohdr_section_enabled.GetValue()) {
        OverrideReShadeSettings_AddDisplayCommanderPaths();
    } else {
        OverrideReShadeSettings_RemoveDisplayCommanderPaths();
    }

    LogInfo("ReShade settings override completed successfully");
}

// ReShade loaded status (declared here so it's available to LoadAddonsFromPluginsDirectory)
std::atomic<bool> g_wait_and_inject_stop(false);

// No-ReShade mode and standalone UI (see globals.hpp)
std::atomic<bool> g_no_reshade_mode(false);
std::atomic<bool> g_standalone_ui_pending(false);
// No-DC mode: .NODC present - load ReShade only, do not register as addon (proxy-only)
std::atomic<bool> g_no_dc_mode(false);
// .UI file: open independent UI at start (cleared after first use in continuous_monitoring)
std::atomic<bool> g_start_with_independent_ui(false);
// .NO_EXIT: block exit and open independent UI when game tries to exit (debugging)
std::atomic<bool> g_no_exit_mode(false);

void TryStartStandaloneUIFromSafeContext() {
    if (!g_no_reshade_mode.load() || !g_standalone_ui_pending.load()) {
        return;
    }
    g_standalone_ui_pending.store(false);
    HMODULE hmod = g_hmodule;
    if (!hmod) {
        return;
    }
    std::thread t([hmod]() { RunStandaloneSettingsUI(static_cast<HINSTANCE>(hmod)); });
    t.detach();
}

// Show the independent (standalone) settings window from ReShade overlay. Only when running in ReShade.
void RequestShowIndependentWindow() {
    if (g_no_reshade_mode.load()) {
        return;
    }
    if (g_standalone_ui_hwnd.load(std::memory_order_acquire) != nullptr) {
        return;  // already open
    }
    HMODULE hmod = g_hmodule;
    if (!hmod) {
        return;
    }
    std::thread t([hmod]() { RunStandaloneSettingsUI(static_cast<HINSTANCE>(hmod)); });
    t.detach();
}

// Request close of the independent settings window (posts WM_CLOSE).
void CloseIndependentWindow() {
    HWND h = g_standalone_ui_hwnd.load(std::memory_order_acquire);
    if (h != nullptr) {
        PostMessageW(h, WM_CLOSE, 0, 0);
    }
}

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
                    settings::g_mainTabSettings.swapchain_hdr_upgrade.SetValue(false);
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

namespace {
struct OtherDcModuleInfo {
    HMODULE module = nullptr;
    std::string path;
    std::string version;
    LONGLONG load_time_ns = 0;
    bool has_load_time = false;
    int state = -1;  // GetDisplayCommanderState() from that module; -1 if not available
};

// Enumerate modules and collect other Display Commander instances. Returns false on enum failure.
bool DetectMultipleDisplayCommanderVersions_Collect(std::vector<OtherDcModuleInfo>& out) {
    typedef const char* (*GetDisplayCommanderVersionFunc)();
    typedef LONGLONG (*GetLoadedNsFunc)();

    HMODULE modules[1024];
    DWORD num_modules = 0;
    if (K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &num_modules) == 0) {
        DWORD error = GetLastError();
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
                 "[DisplayCommander] Failed to enumerate process modules for Display Commander detection: %lu\n",
                 error);
        OutputDebugStringA(error_msg);
        return false;
    }
    if (num_modules > sizeof(modules)) {
        num_modules = static_cast<DWORD>(sizeof(modules));
    }
    HMODULE current_module = g_hmodule;
    out.clear();

    char scan_msg[256];
    snprintf(scan_msg, sizeof(scan_msg), "[DisplayCommander] === Display Commander Module Detection ===\n");
    OutputDebugStringA(scan_msg);
    snprintf(scan_msg, sizeof(scan_msg), "[DisplayCommander] Scanning %u modules for Display Commander...\n",
             static_cast<unsigned int>(num_modules / sizeof(HMODULE)));
    OutputDebugStringA(scan_msg);

    for (DWORD i = 0; i < num_modules / sizeof(HMODULE); ++i) {
        HMODULE module = modules[i];
        if (module == nullptr || module == current_module) continue;

        GetDisplayCommanderVersionFunc version_func =
            reinterpret_cast<GetDisplayCommanderVersionFunc>(GetProcAddress(module, "GetDisplayCommanderVersion"));
        if (version_func == nullptr) continue;

        OtherDcModuleInfo info;
        info.module = module;
        wchar_t module_path[MAX_PATH];
        DWORD path_length = GetModuleFileNameW(module, module_path, MAX_PATH);
        if (path_length > 0) {
            char narrow_path[MAX_PATH];
            WideCharToMultiByte(CP_UTF8, 0, module_path, -1, narrow_path, MAX_PATH, nullptr, nullptr);
            info.path = narrow_path;
        } else {
            info.path = "(path unavailable)";
        }
        const char* other_version = version_func();
        info.version = other_version ? other_version : "(unknown)";

        GetLoadedNsFunc loaded_ns_func = reinterpret_cast<GetLoadedNsFunc>(GetProcAddress(module, "LoadedNs"));
        if (loaded_ns_func != nullptr) {
            info.load_time_ns = loaded_ns_func();
            info.has_load_time = (info.load_time_ns > 0);
        }
        typedef int (*GetDisplayCommanderStateFunc)();
        GetDisplayCommanderStateFunc state_func =
            reinterpret_cast<GetDisplayCommanderStateFunc>(GetProcAddress(module, "GetDisplayCommanderState"));
        if (state_func != nullptr) {
            info.state = state_func();
        }
        out.push_back(info);
    }
    return true;
}

// Log, notify other instances, set g_other_dc_version_detected and should_refuse_load.
void DetectMultipleDisplayCommanderVersions_Resolve(const std::vector<OtherDcModuleInfo>& others,
                                                    LONGLONG current_load_time_ns, bool& should_refuse_load) {
    const char* current_version = DISPLAY_COMMANDER_VERSION_STRING;
    typedef void (*NotifyMultipleVersionsFunc)(const char*);

    for (size_t idx = 0; idx < others.size(); ++idx) {
        const OtherDcModuleInfo& info = others[idx];
        int one_based = static_cast<int>(idx) + 1;
        char found_msg[512];
        snprintf(found_msg, sizeof(found_msg), "[DisplayCommander] Found Display Commander module #%d: 0x%p - %s\n",
                 one_based, info.module, info.path.c_str());
        OutputDebugStringA(found_msg);
        snprintf(found_msg, sizeof(found_msg), "[DisplayCommander]   Other version: %s\n", info.version.c_str());
        OutputDebugStringA(found_msg);
        snprintf(found_msg, sizeof(found_msg), "[DisplayCommander]   Current version: %s\n", current_version);
        OutputDebugStringA(found_msg);

        const bool other_is_loader = (info.state == static_cast<int>(DisplayCommanderState::DC_STATE_DLL_LOADER));
        if (other_is_loader) {
            OutputDebugStringA("[DisplayCommander]   Other instance is loader (DLL_LOADER) - not a conflict.\n");
        }
        if (info.has_load_time) {
            snprintf(found_msg, sizeof(found_msg), "[DisplayCommander]   Other load time: %lld ns\n",
                     info.load_time_ns);
            OutputDebugStringA(found_msg);
            snprintf(found_msg, sizeof(found_msg), "[DisplayCommander]   Current load time: %lld ns\n",
                     current_load_time_ns);
            OutputDebugStringA(found_msg);
            if (!other_is_loader) {
                if (info.load_time_ns < current_load_time_ns) {
                    snprintf(
                        found_msg, sizeof(found_msg),
                        "[DisplayCommander]   Conflict resolution: Other instance loaded first (difference: %lld ns). "
                        "Refusing to load current instance.\n",
                        current_load_time_ns - info.load_time_ns);
                    OutputDebugStringA(found_msg);
                } else {
                    snprintf(
                        found_msg, sizeof(found_msg),
                        "[DisplayCommander]   Conflict resolution: Current instance loaded first (difference: %lld "
                        "ns). Allowing current instance to load.\n",
                        info.load_time_ns - current_load_time_ns);
                    OutputDebugStringA(found_msg);
                }
            }
        } else {
            OutputDebugStringA("[DisplayCommander]   Load timestamp not available from other instance.\n");
        }

        if (!info.version.empty() && info.version != "(unknown)" && !other_is_loader) {
            ::g_other_dc_version_detected.store(std::make_shared<const std::string>(info.version));

            NotifyMultipleVersionsFunc notify_func = reinterpret_cast<NotifyMultipleVersionsFunc>(
                GetProcAddress(info.module, "NotifyDisplayCommanderMultipleVersions"));
            if (notify_func != nullptr) {
                notify_func(current_version);
                OutputDebugStringA("[DisplayCommander] Notified other instance of multiple versions.\n");
            }
        }

        if (!info.version.empty() && info.version != "(unknown)" && !other_is_loader) {
            bool instance_should_refuse = false;
            if (info.has_load_time && info.load_time_ns < current_load_time_ns) {
                instance_should_refuse = true;
                should_refuse_load = true;
            }
            if (instance_should_refuse) {
                char error_msg[512];
                snprintf(error_msg, sizeof(error_msg),
                         "[Display Commander] ERROR: Multiple Display Commander instances detected! "
                         "Other instance: v%s at %s (loaded at %lld ns), Current instance: v%s (loaded at %lld ns). "
                         "Other instance was loaded first - refusing to load current instance to prevent conflicts.",
                         info.version.c_str(), info.path.c_str(), info.load_time_ns, current_version,
                         current_load_time_ns);
                OutputDebugStringA("[DisplayCommander] ERROR: Multiple Display Commander instances detected!\n");
                snprintf(found_msg, sizeof(found_msg), "[DisplayCommander]   Other instance: v%s at %s\n",
                         info.version.c_str(), info.path.c_str());
                OutputDebugStringA(found_msg);
                snprintf(found_msg, sizeof(found_msg), "[DisplayCommander]   Current instance: v%s\n", current_version);
                OutputDebugStringA(found_msg);
                OutputDebugStringA("[DisplayCommander] Refusing to load to prevent conflicts.\n");
            }
        }
    }
}
}  // namespace

// Function to detect multiple Display Commander versions by scanning all modules
// Returns true if multiple versions detected (should refuse to load), false otherwise
bool DetectMultipleDisplayCommanderVersions() {
    std::vector<OtherDcModuleInfo> others;
    if (!DetectMultipleDisplayCommanderVersions_Collect(others)) {
        return false;
    }

    LONGLONG current_load_time_ns = g_dll_load_time_ns.load(std::memory_order_acquire);
    char scan_msg[256];
    snprintf(scan_msg, sizeof(scan_msg), "[DisplayCommander] Current instance load time: %lld ns\n",
             current_load_time_ns);
    OutputDebugStringA(scan_msg);

    bool should_refuse_load = false;
    DetectMultipleDisplayCommanderVersions_Resolve(others, current_load_time_ns, should_refuse_load);

    const int dc_module_count = static_cast<int>(others.size());
    char complete_msg[256];
    snprintf(complete_msg, sizeof(complete_msg), "[DisplayCommander] === Display Commander Detection Complete ===\n");
    OutputDebugStringA(complete_msg);
    snprintf(complete_msg, sizeof(complete_msg),
             "[DisplayCommander] Total Display Commander modules found: %d (excluding current)\n", dc_module_count);
    OutputDebugStringA(complete_msg);

    if (should_refuse_load) {
        OutputDebugStringA(
            "[DisplayCommander] WARNING: Multiple Display Commander versions detected! Refusing to load.\n");
        return true;
    }
    if (dc_module_count > 0) {
        OutputDebugStringA(
            "[DisplayCommander] INFO: Multiple Display Commander versions detected, but current instance was loaded "
            "first. Allowing load.\n");
    } else {
        OutputDebugStringA("[DisplayCommander] Single Display Commander instance detected - no conflicts.\n");
    }
    return false;
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
    display_commander::utils::RegisterCurrentProcessWithDisplayCommanderMutex();
    settings::LoadAllSettingsAtStartup();
    display_commanderhooks::InstallLoadLibraryHooks();
    LogCurrentLogLevel();
    if (settings::g_advancedTabSettings.disable_dpi_scaling.GetValue()) {
        display_commander::display::dpi::DisableDPIScaling();
        LogInfo("DPI scaling disabled - process is now DPI-aware");
    }
    HandleSafemode();

    bool suppress_pin_module = false;
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
    OverrideReShadeSettings();
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
    if (enabled_experimental_features) {
        display_commander::widgets::dualsense_widget::InitializeDualSenseWidget();
    }
    display_commanderhooks::keyboard_tracker::Initialize();
    LogInfo("Keyboard tracking system initialized");
}
}  // namespace

void DoInitializationWithoutHwndSafe(HMODULE h_module) {
    DoInitializationWithoutHwndSafe_Early(h_module);
    DoInitializationWithoutHwndSafe_Late();
}

void DoInitializationWithoutHwnd(HMODULE h_module) {
    CALL_GUARD(utils::get_now_ns());
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

    // Register draw event handlers for render timing
    reshade::register_event<reshade::addon_event::draw>(OnDraw);
    reshade::register_event<reshade::addon_event::draw_indexed>(OnDrawIndexed);
    reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(OnDrawOrDispatchIndirect);

    // Register power saving event handlers for additional GPU operations
    reshade::register_event<reshade::addon_event::dispatch>(OnDispatch);
    reshade::register_event<reshade::addon_event::dispatch_mesh>(OnDispatchMesh);
    reshade::register_event<reshade::addon_event::dispatch_rays>(OnDispatchRays);
    reshade::register_event<reshade::addon_event::copy_resource>(OnCopyResource);
    reshade::register_event<reshade::addon_event::update_buffer_region>(OnUpdateBufferRegion);
    // reshade::register_event<reshade::addon_event::update_buffer_region_command>(OnUpdateBufferRegionCommand);

    // Register buffer resolution upgrade event handlers
    reshade::register_event<reshade::addon_event::create_resource>(OnCreateResource);
    reshade::register_event<reshade::addon_event::create_resource_view>(OnCreateResourceView);
    reshade::register_event<reshade::addon_event::create_sampler>(OnCreateSampler);
    reshade::register_event<reshade::addon_event::bind_viewports>(OnSetViewport);
    reshade::register_event<reshade::addon_event::bind_scissor_rects>(OnSetScissorRects);
    // Note: bind_resource, map_resource, unmap_resource events don't exist in ReShade API
    // These operations are handled differently in ReShade
    // Register device destroy event for restore-on-exit
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
    reshade::register_event<reshade::addon_event::destroy_resource>(OnDestroyResource);

    // Register present completion event
    reshade::register_event<reshade::addon_event::finish_present>(OnFinishPresent);

    // Register ReShade effect rendering events
    reshade::register_event<reshade::addon_event::reshade_begin_effects>(OnReShadeBeginEffects);
    reshade::register_event<reshade::addon_event::reshade_finish_effects>(OnReShadeFinishEffects);
    reshade::register_event<reshade::addon_event::reshade_present>(OnReShadePresent);
    if (IsDisplayCommanderHookingInstance()) display_commanderhooks::InstallApiHooks();
}

// Named event name for injection tracking (shared across processes)
// Defined here so it's available in DllMain
constexpr const wchar_t* INJECTION_ACTIVE_EVENT_NAME = L"Local\\DisplayCommander_InjectionActive";
constexpr const wchar_t* INJECTION_STOP_EVENT_NAME = L"Local\\DisplayCommander_InjectionStop";

namespace {
enum class ProcessAttachEarlyResult { Continue, RefuseLoad, EarlySuccess, LoaderOnly };

std::wstring ProcessAttach_GetConfigDirectoryW() {
    wchar_t exe_path[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) == 0) return L"";
    return std::filesystem::path(exe_path).parent_path().wstring();
}

// Returns true if any file in dir has a name that split by '.' has parts[1] equal to one of segment_names
// (case-insensitive). E.g. ".NORESHADE", ".NORESHADE.off" match segment "NORESHADE".
static bool DirectoryHasFileWithSegment(const std::wstring& dir, std::initializer_list<const wchar_t*> segment_names) {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec || !entry.is_regular_file(ec)) continue;
        std::wstring name = entry.path().filename().wstring();
        if (name.empty() || name[0] != L'.') continue;
        std::vector<std::wstring> parts;
        for (size_t pos = 0; pos <= name.size();) {
            size_t next = name.find(L'.', pos);
            if (next == std::wstring::npos) {
                parts.push_back(name.substr(pos));
                break;
            }
            parts.push_back(name.substr(pos, next - pos));
            pos = next + 1;
        }
        if (parts.size() < 2) continue;
        for (const wchar_t* seg : segment_names) {
            if (_wcsicmp(parts[1].c_str(), seg) == 0) return true;
        }
    }
    return false;
}

void ProcessAttach_CheckNoReShadeMode() {
    const std::wstring dc_config_dir = ProcessAttach_GetConfigDirectoryW();
    if (dc_config_dir.empty()) return;
    if (DirectoryHasFileWithSegment(dc_config_dir, {L"NO_RESHADE", L"NORESHADE"})) {
        g_no_reshade_mode.store(true);
        OutputDebugStringA(
            "[DisplayCommander] .NO_RESHADE/.NORESHADE (or .* variant) found - ReShade will not be loaded; standalone "
            "settings UI will start.\n");
    }
    if (DirectoryHasFileWithSegment(dc_config_dir, {L"NODC"})) {
        g_no_dc_mode.store(true);
        OutputDebugStringA(
            "[DisplayCommander] .NODC (or .NODC.*) found - ReShade will be loaded; Display Commander will not "
            "register as addon (proxy-only).\n");
    }
    if (DirectoryHasFileWithSegment(dc_config_dir, {L"UI"})) {
        g_start_with_independent_ui.store(true);
        OutputDebugStringA(
            "[DisplayCommander] .UI (or .UI.*) found - independent settings window will open at start (ReShade "
            "path).\n");
    }
    if (DirectoryHasFileWithSegment(dc_config_dir, {L"NO_EXIT", L"NOEXIT"})) {
        g_no_exit_mode.store(true);
        OutputDebugStringA(
            "[DisplayCommander] .NO_EXIT/.NOEXIT (or .* variant) found - game exit will be blocked; independent UI "
            "opens when exit is attempted (debugging).\n");
    }
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

using GetDisplayCommanderStateFn = int (*)();

// Scan loaded modules for another Display Commander that already reports HOOKED.
// If found, set our state to PROXY_DLL_ONLY; otherwise set to HOOKED.
// Optional: set *another_undecided if any other DC returned UNDECIDED (caller may log later).
void ProcessAttach_ScanDisplayCommanderState(bool* another_undecided = nullptr) {
    if (another_undecided) *another_undecided = false;
    HMODULE self = g_hmodule;
    if (!self) return;
    HMODULE modules[1024];
    DWORD num_modules_bytes = 0;
    if (K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &num_modules_bytes) == 0) return;
    DWORD num_modules =
        (std::min<DWORD>)(num_modules_bytes / sizeof(HMODULE), static_cast<DWORD>(sizeof(modules) / sizeof(HMODULE)));
    for (DWORD i = 0; i < num_modules; i++) {
        if (modules[i] == nullptr || modules[i] == self) continue;
        FARPROC proc = GetProcAddress(modules[i], "GetDisplayCommanderState");
        if (!proc) continue;
        int other = reinterpret_cast<GetDisplayCommanderStateFn>(proc)();
        if (other == static_cast<int>(DisplayCommanderState::DC_STATE_HOOKED)) {
            g_display_commander_state.store(DisplayCommanderState::DC_STATE_PROXY_DLL_ONLY, std::memory_order_release);
            return;
        }
        if (other == static_cast<int>(DisplayCommanderState::DC_STATE_DLL_LOADER)) {
            // We are the addon loaded by the loader; we become the hooking instance.
            g_display_commander_state.store(DisplayCommanderState::DC_STATE_HOOKED, std::memory_order_release);
            return;
        }
        if (another_undecided && other == static_cast<int>(DisplayCommanderState::DC_STATE_UNDECIDED))
            *another_undecided = true;
    }
    g_display_commander_state.store(DisplayCommanderState::DC_STATE_HOOKED, std::memory_order_release);
}

// Returns the file's version resource ProductName (wide), or empty if absent. Used to avoid
// loading ReShade again when already loaded, or loading Display Commander (ourselves) again.
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
    wchar_t sub_block[64];
    swprintf_s(sub_block, L"\\StringFileInfo\\%04x%04x\\ProductName", p_trans[0].wLanguage, p_trans[0].wCodePage);
    void* p_block = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(buf.data(), sub_block, &p_block, &len) || !p_block || len < sizeof(wchar_t)) return {};
    const wchar_t* product = static_cast<const wchar_t*>(p_block);
    size_t max_chars = len / sizeof(wchar_t);
    size_t str_len = 0;
    while (str_len < max_chars && product[str_len] != L'\0') ++str_len;
    return std::wstring(product, str_len);
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

// Post-ReShade addon dir: .dc64r / .dc32r / .dcr. We hard-link into global Display_Commander\tmp\<pid>;
// when cross-volume (hard link not possible), we create a local tmp\<pid> in the addon dir and copy there
// so the copy stays on the same volume. LoadLibrary from the chosen path so originals can be updated while running.
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
    const DWORD pid = GetCurrentProcessId();
    wchar_t pid_buf[32];
    swprintf_s(pid_buf, L"%lu", static_cast<unsigned long>(pid));
    std::filesystem::path global_temp_dir = display_commander::utils::GetLocalDcDirectory() / L"tmp" / pid_buf;
    std::error_code ec;
    if (!std::filesystem::exists(global_temp_dir, ec)) {
        std::filesystem::create_directories(global_temp_dir, ec);
    }
    if (ec) {
        char msg[384];
        snprintf(msg, sizeof(msg), "[DisplayCommander] Failed to create global temp dir for post-ReShade addons: %s\n",
                 ec.message().c_str());
        OutputDebugStringA(msg);
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(global_temp_dir, ec)) {
        if (!ec) std::filesystem::remove(entry.path(), ec);
    }
    ec.clear();
    std::filesystem::path local_temp_dir;  // created only when cross-volume forces a copy
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
        std::filesystem::path dest_path = global_temp_dir / entry.path().filename();
        bool loaded = false;
        if (CreateHardLinkW(dest_path.c_str(), path_w.c_str(), nullptr)) {
            loaded = true;
        } else if (GetLastError() == ERROR_INVALID_PARAMETER) {
            // Cross-volume: use local tmp in addon dir (same volume), create only when needed
            if (local_temp_dir.empty()) {
                local_temp_dir = addon_dir / L"tmp" / pid_buf;
                std::filesystem::create_directories(local_temp_dir, ec);
                if (ec) {
                    char msg[384];
                    snprintf(msg, sizeof(msg),
                             "[DisplayCommander] Failed to create local temp dir for post-ReShade addons: %s\n",
                             ec.message().c_str());
                    OutputDebugStringA(msg);
                    local_temp_dir.clear();
                    continue;
                }
                for (const auto& e : std::filesystem::directory_iterator(local_temp_dir, ec)) {
                    if (!ec) std::filesystem::remove(e.path(), ec);
                }
                ec.clear();
            }
            dest_path = local_temp_dir / entry.path().filename();
            if (CopyFileW(path_w.c_str(), dest_path.c_str(), FALSE)) {
                loaded = true;
            }
        }
        if (!loaded) {
            char msg[384];
            snprintf(msg, sizeof(msg), "[DisplayCommander] CreateHardLink/CopyFile failed for %s (error %lu)\n",
                     entry.path().filename().string().c_str(), GetLastError());
            OutputDebugStringA(msg);
            continue;
        }
        HMODULE mod = LoadLibraryW(dest_path.c_str());
        if (mod != nullptr) {
            std::string name = entry.path().filename().string();
            char msg[384];
            snprintf(msg, sizeof(msg), "[DisplayCommander] Loaded .dc64r/.dc32r/.dcr DLL (after ReShade): %s\n",
                     name.c_str());
            OutputDebugStringA(msg);
        }
    }
}

void CleanupPostReShadeAddonTempDir() {
    const DWORD pid = GetCurrentProcessId();
    const std::wstring pid_str = std::to_wstring(pid);
    std::error_code ec;
    auto try_remove_dir = [&ec](const std::filesystem::path& dir) {
        if (!std::filesystem::exists(dir, ec)) return;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec) break;
            std::filesystem::remove(entry.path(), ec);
        }
        std::filesystem::remove(dir, ec);
    };
    try_remove_dir(display_commander::utils::GetLocalDcDirectory() / L"tmp" / pid_str);
    if (g_hmodule != nullptr) {
        WCHAR module_path[MAX_PATH];
        if (GetModuleFileNameW(g_hmodule, module_path, MAX_PATH) > 0) {
            try_remove_dir(std::filesystem::path(module_path).parent_path() / L"tmp" / pid_str);
        }
    }
}

void ProcessAttach_DetectEntryPoint(HMODULE h_module, std::wstring& entry_point, bool& found_proxy) {
    entry_point = L"addon";
    found_proxy = false;
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
         "Display Commander loaded as dbghelp.dll proxy - DbgHelp functions will be forwarded to system dbghelp.dll"}};
    for (const auto& proxy : proxy_dlls) {
        if (_wcsicmp(module_name.c_str(), proxy.name) == 0) {
            entry_point = proxy.entry_point_val;
            OutputDebugStringA(proxy.debug_msg);
            found_proxy = true;
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

bool ProcessAttach_TryLoadReShadeWhenNotLoaded(HMODULE /*h_module*/, bool found_proxy) {
    OutputDebugStringA("ReShade not loaded");
    display_commander::utils::DetectAndLogPlatformAPIs();
    std::vector<display_commander::utils::PlatformAPI> detected_platforms =
        display_commander::utils::GetDetectedPlatformAPIs();
    OutputDebugStringA("Detected platforms: ");
    for (size_t i = 0; i < detected_platforms.size(); ++i) {
        if (i > 0) OutputDebugStringA(", ");
        OutputDebugStringA(display_commander::utils::GetPlatformAPIName(detected_platforms[i]));
    }
    OutputDebugStringA("\n");
    WCHAR executable_path[MAX_PATH] = {0};
    bool whitelist = false;
    if (GetModuleFileNameW(nullptr, executable_path, MAX_PATH) > 0) {
        OutputDebugStringA("Executable path: ");
        char executable_path_narrow[MAX_PATH];
        WideCharToMultiByte(CP_ACP, 0, executable_path, -1, executable_path_narrow, MAX_PATH, nullptr, nullptr);
        OutputDebugStringA(executable_path_narrow);
        OutputDebugStringA("\n");
        whitelist = display_commander::utils::TestWhitelist(executable_path);
    }
    if (detected_platforms.empty() && !found_proxy && !whitelist) {
        LogInfo("[reshade] No platforms detected and not found in whitelist, refusing to load found_proxy: %d",
                found_proxy);
        return false;
    }
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

    const std::wstring dc_config_dir = ProcessAttach_GetConfigDirectoryW();
    if (!dc_config_dir.empty()) SetEnvironmentVariableW(L"RESHADE_BASE_PATH_OVERRIDE", dc_config_dir.c_str());
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
    display_commander::config::DisplayCommanderConfigManager::GetInstance().Initialize();
    display_commander::config::DisplayCommanderConfigManager::GetInstance().SetAutoFlushLogs(true);
    utils::initialize_qpc_timing_constants();
    DoInitializationWithoutHwndSafe(h_module);
    g_standalone_ui_pending.store(true);
}

void ProcessAttach_RegisterAndPostInit(HMODULE h_module, const std::wstring& entry_point) {
    display_commander::config::DisplayCommanderConfigManager::GetInstance().Initialize();
    display_commander::config::DisplayCommanderConfigManager::GetInstance().SetAutoFlushLogs(true);
    OutputDebugStringA("ReShade 111111");
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
    DoInitializationWithoutHwnd(h_module);
    ProcessAttach_LoadLocalAddonDllsAfterReShade(h_module);
    LoadAddonsFromPluginsDirectory();
}

// Minimum Display Commander version allowed to load (below this we refuse).
static constexpr const char* kDisplayCommanderMinLoadVersion = "0.12.194";

ProcessAttachEarlyResult ProcessAttach_EarlyChecksAndInit(HMODULE h_module) {
    g_hmodule = h_module;
    g_dll_load_time_ns.store(utils::get_now_ns(), std::memory_order_release);
    g_display_commander_state.store(DisplayCommanderState::DC_STATE_UNDECIDED, std::memory_order_release);
    display_commander::config::DisplayCommanderConfigManager::GetInstance().Initialize();
    display_commander::config::DisplayCommanderConfigManager::GetInstance().SetAutoFlushLogs(true);

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

    // Use global version is set but we're loaded from the game folder (same as exe): act as loader only and load global DC.
    if (display_commander::utils::GetUseGlobalDcVersionFromConfig()) {
        WCHAR mod_buf[MAX_PATH];
        WCHAR exe_buf[MAX_PATH];
        if (GetModuleFileNameW(h_module, mod_buf, MAX_PATH) > 0 && GetModuleFileNameW(nullptr, exe_buf, MAX_PATH) > 0) {
            std::filesystem::path mod_dir = std::filesystem::path(mod_buf).parent_path();
            std::filesystem::path exe_dir = std::filesystem::path(exe_buf).parent_path();
            std::error_code ec;
            std::filesystem::path mod_canon = std::filesystem::canonical(mod_dir, ec);
            std::filesystem::path exe_canon = std::filesystem::canonical(exe_dir, ec);
            if (!ec && mod_canon == exe_canon) {
                std::filesystem::path load_path = display_commander::utils::GetDcDirectoryForLoading(nullptr);
                std::filesystem::path addon_path = display_commander::utils::GetDcAddonPathInDirectory(load_path);
                if (!addon_path.empty()) {
                    std::filesystem::path our_module_path(mod_buf);
                    std::filesystem::path addon_canon = std::filesystem::canonical(addon_path, ec);
                    std::filesystem::path our_canon = std::filesystem::canonical(our_module_path, ec);
                    if (ec || addon_canon != our_canon) {
                        g_display_commander_state.store(DisplayCommanderState::DC_STATE_DLL_LOADER,
                                                        std::memory_order_release);
                        if (LoadLibraryW(addon_path.c_str()) != nullptr) {
                            OutputDebugStringA("[DisplayCommander] Use global version is set; loaded global DC from game-folder instance, returning loader only.\n");
                            return ProcessAttachEarlyResult::LoaderOnly;
                        }
                        g_display_commander_state.store(DisplayCommanderState::DC_STATE_UNDECIDED,
                                                        std::memory_order_release);
                    }
                }
            }
        }
    }

    // Loader mode: when we're not under stable\ or Debug\, act as loader. GetDcDirectoryForLoading(h_module) returns
    // the directory to use (local addon > global > local proxy when mode is "local"; else global/debug/stable path).
    // If that path != base and contains the addon, we load it and stay quiet.
    if (display_commander::utils::IsLoadedWithDLLExtension(static_cast<void*>(h_module))) {
        WCHAR module_path_buf[MAX_PATH];
        if (GetModuleFileNameW(h_module, module_path_buf, MAX_PATH) > 0) {
            std::filesystem::path module_dir = std::filesystem::path(module_path_buf).parent_path();
            std::filesystem::path base = display_commander::utils::GetLocalDcDirectory();
            std::filesystem::path stable_base = base / L"stable";
            std::filesystem::path debug_base = base / L"Debug";
            std::error_code ec;
            std::filesystem::path module_canon = std::filesystem::canonical(module_dir, ec);
            if (!ec) {
                bool we_are_under_stable = false;
                if (std::filesystem::exists(stable_base, ec)) {
                    std::filesystem::path stable_canon = std::filesystem::canonical(stable_base, ec);
                    if (!ec) {
                        auto m_it = module_canon.begin();
                        auto d_it = stable_canon.begin();
                        while (d_it != stable_canon.end() && m_it != module_canon.end() && *d_it == *m_it) {
                            ++d_it;
                            ++m_it;
                        }
                        we_are_under_stable = (d_it == stable_canon.end());
                    }
                }
                bool we_are_under_debug = false;
                if (std::filesystem::exists(debug_base, ec)) {
                    std::filesystem::path debug_canon = std::filesystem::canonical(debug_base, ec);
                    if (!ec) {
                        auto m_it = module_canon.begin();
                        auto d_it = debug_canon.begin();
                        while (d_it != debug_canon.end() && m_it != module_canon.end() && *d_it == *m_it) {
                            ++d_it;
                            ++m_it;
                        }
                        we_are_under_debug = (d_it == debug_canon.end());
                    }
                }
                bool we_are_under_versioned = we_are_under_stable || we_are_under_debug;
                if (!we_are_under_versioned) {
                    std::filesystem::path load_path =
                        display_commander::utils::GetDcDirectoryForLoading(static_cast<void*>(h_module));
                    if (load_path != base) {
                        std::filesystem::path addon_path =
                            display_commander::utils::GetDcAddonPathInDirectory(load_path);
                        if (!addon_path.empty()) {
                            g_display_commander_state.store(DisplayCommanderState::DC_STATE_DLL_LOADER,
                                                            std::memory_order_release);
                            if (LoadLibraryW(addon_path.c_str()) != nullptr) {
                                return ProcessAttachEarlyResult::LoaderOnly;
                            }
                            g_display_commander_state.store(DisplayCommanderState::DC_STATE_UNDECIDED,
                                                            std::memory_order_release);
                        }
                    }
                }
            }
        }
    }

    if (GetModuleHandleW(L"SpecialK32.dll") != nullptr || GetModuleHandleW(L"SpecialK64.dll") != nullptr) {
        OutputDebugStringA(
            "[DisplayCommander] SpecialK (SpecialK32.dll/SpecialK64.dll) is already loaded - refusing to "
            "load Display Commander.\n");
        return ProcessAttachEarlyResult::RefuseLoad;
    }
    bool another_undecided = false;
    ProcessAttach_ScanDisplayCommanderState(&another_undecided);
    if (IsDisplayCommanderHookingInstance() && DetectMultipleDisplayCommanderVersions()) {
        OutputDebugStringA("[DisplayCommander] Multiple Display Commander instances detected - refusing to load.\n");
        return ProcessAttachEarlyResult::RefuseLoad;
    }
    if (another_undecided) {
        LogWarn(
            "Another Display Commander instance was still Undecided during scan; this instance may have claimed "
            "HOOKED.");
    }
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

// Returns true if .DLL_DETECTOR existed and we copied self to dlls_loaded (caller should return TRUE from DllMain).
static bool DllDetectorCopyToLoadedIfEnabled(HMODULE h_module) {
    wchar_t module_path_buf[MAX_PATH] = {};
    if (GetModuleFileNameW(h_module, module_path_buf, MAX_PATH) == 0) return false;
    std::filesystem::path module_path(module_path_buf);
    std::filesystem::path local_dir = module_path.parent_path();
    std::error_code ec;
    bool has_dll_detector = false;
    for (const auto& entry : std::filesystem::directory_iterator(local_dir, ec)) {
        if (ec || !entry.is_regular_file(ec)) continue;
        std::wstring name = entry.path().filename().wstring();
        if (name.size() < 2 || name[0] != L'.') continue;
        size_t dot = name.find(L'.', 1);
        std::wstring first_seg = (dot == std::wstring::npos) ? name.substr(1) : name.substr(1, dot - 1);
        if (_wcsicmp(first_seg.c_str(), L"DLL_DETECTOR") == 0) {
            has_dll_detector = true;
            break;
        }
    }
    if (!has_dll_detector) return false;
    std::filesystem::path dlls_loaded_dir = local_dir / L"dlls_loaded";
    std::filesystem::create_directories(dlls_loaded_dir, ec);
    if (ec) return false;
    std::filesystem::path dest = dlls_loaded_dir / module_path.filename();
    if (CopyFileW(module_path.c_str(), dest.c_str(), FALSE) == 0) return false;

    std::ostringstream oss;
    oss << "[DisplayCommander] .DLL_DETECTOR: copied self to dlls_loaded " << dest.string() << "\n";
    OutputDebugStringA(oss.str().c_str());
    return true;
}

#if !defined(DISPLAY_COMMANDER_BUILD_EXE)
BOOL APIENTRY DllMain(HMODULE h_module, DWORD fdw_reason, LPVOID lpv_reserved) {
    switch (fdw_reason) {
        case DLL_PROCESS_ATTACH: {
            static const char* reason = "";
            if (DllDetectorCopyToLoadedIfEnabled(h_module)) return TRUE;
            auto set_process_attached_on_exit = [h_module]() {
                g_dll_initialization_complete.store(true);
                // log
                g_dll_initialization_complete.store(true);
                // log executable path
                WCHAR executable_path[MAX_PATH] = {0};
                if (GetModuleFileNameW(nullptr, executable_path, MAX_PATH) > 0) {
                    char executable_path_narrow[MAX_PATH];
                    WideCharToMultiByte(CP_ACP, 0, executable_path, -1, executable_path_narrow, MAX_PATH, nullptr,
                                        nullptr);
                    LogInfo("[main_entry] DLL_PROCESS_ATTACH: executable path: %s", executable_path_narrow);
                }
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

            ProcessAttachEarlyResult early = ProcessAttach_EarlyChecksAndInit(h_module);
            if (early == ProcessAttachEarlyResult::RefuseLoad) {
                reason = "RefuseLoad";
                return TRUE;
            }
            if (early == ProcessAttachEarlyResult::EarlySuccess) {
                reason = "EarlySuccess";
                return TRUE;
            }
            if (early == ProcessAttachEarlyResult::LoaderOnly) {
                reason = "LoaderOnly";
                return TRUE;
            }
            ProcessAttach_DetectReShadeInModules();
            ProcessAttach_LoadLocalAddonDlls(h_module);
            ProcessAttach_CheckNoReShadeMode();

            std::wstring entry_point;
            bool found_proxy = false;
            ProcessAttach_DetectEntryPoint(h_module, entry_point, found_proxy);

            if ((g_reshade_module == nullptr) && !g_no_reshade_mode.load()) {
                ProcessAttach_TryLoadReShadeWhenNotLoaded(h_module, found_proxy);
            }
            // If ReShade still not loaded, use standalone UI (same as .NORESHADE)
            if (g_reshade_module == nullptr) {
                const bool was_no_reshade = g_no_reshade_mode.load();
                g_no_reshade_mode.store(true);
                if (!was_no_reshade) {
                    OutputDebugStringA(
                        "[main_entry] ReShade not found - starting with standalone settings UI (same as "
                        ".NORESHADE).\n");
                }
            }

            if (g_no_reshade_mode.load()) {
                LogInfo("[main_entry] DLL_PROCESS_ATTACH: No ReShade mode");
                ProcessAttach_NoReShadeModeInit(h_module);
                g_dll_initialization_complete.store(true);
                reason = ".NO_RESHADE/.NORESHADE";
                break;
            }

            if (g_no_dc_mode.load()) {
                LogInfo("[main_entry] DLL_PROCESS_ATTACH: .NODC - proxy only, not registering as addon");
                g_dll_initialization_complete.store(true);
                reason = ".NODC";
                break;
            }

            if (!reshade::register_addon(h_module)) {
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
            if (g_no_dc_mode.load(std::memory_order_acquire)) {
                return TRUE;  // .NODC: we never registered or inited, nothing to clean up
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

            // Clean up continuous monitoring if it's running
            StopContinuousMonitoring();
            StopGPUCompletionMonitoring();

            // Clean up refresh rate monitoring
            dxgi::fps_limiter::StopRefreshRateMonitoring();

            // Clean up NVAPI actual refresh rate monitoring
            display_commander::nvapi::StopNvapiActualRefreshRateMonitoring();

            // Clean up experimental tab threads
            ui::new_ui::CleanupExperimentalTab();

            // Clean up DualSense support (only when experimental features were enabled)
            if (enabled_experimental_features) {
                display_commander::widgets::dualsense_widget::CleanupDualSenseWidget();
            }

            // Clean up HID suppression hooks
            renodx::hooks::UninstallHIDSuppressionHooks();

            // Clean up NVAPI instances before shutdown
            if (g_reflexProvider) {
                g_reflexProvider->Shutdown();
            }

            // Clean up PresentMon (must stop ETW session to prevent system-wide resource leaks)
            // ETW sessions are system-wide and persist until explicitly stopped
            // If not stopped, they can interfere with future processes

            // Try to remove post-ReShade addon temp dir (may fail while loaded DLLs still hold files)
            CleanupPostReShadeAddonTempDir();

            // Shutdown DisplayCommander logger (must be last to capture all cleanup messages)
            display_commander::logger::Shutdown();

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

            break;
    }

    return TRUE;
}
#endif  // !DISPLAY_COMMANDER_BUILD_EXE

// Standalone entry: same init as no-ReShade DLL path, then run Games-tab-only UI on main thread.
// Used by: (1) .exe build (main_exe.cpp), (2) rundll32 Launcher export (DLL build).
// Set HOOKED so DoInitializationWithoutHwndSafe_Late runs (StartContinuousMonitoring), which fills the
// running-games cache so the Games tab can show other processes with the addon loaded.
// Single-instance: a named mutex prevents multiple Launcher/exe copies; if one is already running we bring it to focus
// and exit.
void RunDisplayCommanderStandalone(HINSTANCE hInst) {
#ifdef _WIN64
    static const wchar_t* kLauncherMutexName = L"Local\\DisplayCommander_LauncherMutex64";
#else
    static const wchar_t* kLauncherMutexName = L"Local\\DisplayCommander_LauncherMutex32";
#endif
    HANDLE launcher_mutex = CreateMutexW(nullptr, FALSE, kLauncherMutexName);
    if (launcher_mutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(launcher_mutex);
        HWND existing = FindWindowW(L"DisplayCommanderGamesUI", nullptr);
        if (existing != nullptr) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        return;
    }
    // Hold mutex for process lifetime (do not close launcher_mutex so only one instance runs).

    g_shutdown.store(false);
    g_display_commander_state.store(DisplayCommanderState::DC_STATE_HOOKED, std::memory_order_release);
    ProcessAttach_NoReShadeModeInit(reinterpret_cast<HMODULE>(hInst));
    RunStandaloneGamesOnlyUI(hInst);
}

#if !defined(DISPLAY_COMMANDER_BUILD_EXE)
// rundll32 entry: e.g. rundll32.exe zzz_display_commander.addon64,Launcher
extern "C" __declspec(dllexport) void CALLBACK Launcher(HWND hwnd, HINSTANCE hInst, LPSTR lpszCmdLine, int nCmdShow) {
    (void)hwnd;
    (void)lpszCmdLine;
    (void)nCmdShow;
    RunDisplayCommanderStandalone(hInst);
}
#endif  // !DISPLAY_COMMANDER_BUILD_EXE

// CONTINUOUS RENDERING FUNCTIONS REMOVED - Focus spoofing is now handled by Win32 hooks

// CONTINUOUS RENDERING THREAD REMOVED - Focus spoofing is now handled by Win32 hooks
// This provides a much cleaner and more effective solution

// Auto-injection state
namespace {
HHOOK g_cbt_hook = nullptr;
std::atomic<LONGLONG> g_injection_start_time(0);
std::atomic<bool> g_injection_active(false);
constexpr LONGLONG INJECTION_DURATION_NS = 30LL * 1000000000LL;  // 30 seconds in nanoseconds

// Named event to signal injection is active (shared across processes)
HANDLE g_injection_active_event = nullptr;
HANDLE g_injection_stop_event = nullptr;

}  // namespace

// CBT Hook procedure - called when windows are created
// This is just for NOTIFICATION - the actual DLL injection happens automatically
// when SetWindowsHookEx is called with dwThreadId=0 (system-wide hook).
// Windows automatically loads the DLL into target processes when hook events occur.
// This callback runs in the TARGET process after the DLL is already loaded.
LRESULT CALLBACK CBTProc(int nCode, WPARAM wParam, LPARAM lParam) {
    // if nCode is less than 0, return the next hook
    if (nCode < 0) {
        return CallNextHookEx(g_cbt_hook, nCode, wParam, lParam);
    }
    OutputDebugStringA(std::format("CBTProc PID: {}", GetCurrentProcessId()).c_str());

    return CallNextHookEx(g_cbt_hook, nCode, wParam, lParam);
}

// Function to stop injection manually
void StopInjectionInternal() {
    // Signal stop event to notify any running Start process
    HANDLE stop_event = OpenEventW(EVENT_MODIFY_STATE, FALSE, INJECTION_STOP_EVENT_NAME);
    if (stop_event != nullptr) {
        SetEvent(stop_event);
        CloseHandle(stop_event);
    }

    if (g_cbt_hook != nullptr) {
        UnhookWindowsHookEx(g_cbt_hook);
        g_cbt_hook = nullptr;
        g_injection_active.store(false, std::memory_order_release);
        g_injection_start_time.store(0, std::memory_order_release);

        // Signal that injection is no longer active
        if (g_injection_active_event != nullptr) {
            ResetEvent(g_injection_active_event);
            CloseHandle(g_injection_active_event);
            g_injection_active_event = nullptr;
        }

        // Close stop event if we own it
        if (g_injection_stop_event != nullptr) {
            CloseHandle(g_injection_stop_event);
            g_injection_stop_event = nullptr;
        }

        OutputDebugStringA("Auto-injection stopped: CBT hook removed");
    }
}

// Cleanup thread to remove hook after 30 seconds
void StartInjectionCleanupThread() {
    std::thread([]() {
        Sleep(30000);  // 30 seconds
        StopInjectionInternal();
    }).detach();
}

// Internal function to start injection with optional duration limit
void StartInjectionInternal(bool forever) {
    OutputDebugStringA(
        std::format("StartInjectionInternal PID: {}, forever: {}", GetCurrentProcessId(), forever).c_str());

    // Initialize config system for logging
    display_commander::config::DisplayCommanderConfigManager::GetInstance().Initialize();
    display_commander::config::DisplayCommanderConfigManager::GetInstance().SetAutoFlushLogs(true);

    // Check if hook is already active
    if (g_injection_active.load(std::memory_order_acquire)) {
        OutputDebugStringA("Auto-injection already active, restarting timer");
        // Restart the timer
        g_injection_start_time.store(utils::get_now_ns(), std::memory_order_release);
        return;
    }

    // Install global CBT hook (thread ID 0 = system-wide)
    // This automatically injects the DLL into all processes when CBT events occur
    // The hook installation itself IS the injection mechanism - Windows loads the DLL automatically
    g_cbt_hook = SetWindowsHookEx(WH_CBT, CBTProc, g_hmodule, 0);

    if (g_cbt_hook == nullptr) {
        DWORD error = GetLastError();
        OutputDebugStringA(
            std::format("Failed to install CBT hook for auto-injection - Error: {} (0x{:X})", error, error).c_str());
        return;
    }

    // Mark injection as active and record start time
    g_injection_active.store(true, std::memory_order_release);
    g_injection_start_time.store(utils::get_now_ns(), std::memory_order_release);

    // Create named event to signal injection is active (shared across processes)
    g_injection_active_event = CreateEventW(nullptr, TRUE, TRUE, INJECTION_ACTIVE_EVENT_NAME);
    if (g_injection_active_event == nullptr) {
        DWORD error = GetLastError();
        OutputDebugStringA(
            std::format("Failed to create injection active event - Error: {} (0x{:X})", error, error).c_str());
    }

    if (forever) {
        // Create stop event for signaling from other processes
        g_injection_stop_event = CreateEventW(nullptr, TRUE, FALSE, INJECTION_STOP_EVENT_NAME);
        if (g_injection_stop_event == nullptr) {
            DWORD error = GetLastError();
            OutputDebugStringA(
                std::format("Failed to create injection stop event - Error: {} (0x{:X})", error, error).c_str());
        }

        OutputDebugStringA(
            "Auto-injection started: CBT hook installed, will inject into all new processes indefinitely");
        // Keep process alive to maintain the hook
        // The hook will remain active as long as this process is running
        // Check for stop signal periodically
        while (g_injection_active.load(std::memory_order_acquire)) {
            // Check if stop event was signaled (from another process calling Stop)
            if (g_injection_stop_event != nullptr) {
                DWORD wait_result = WaitForSingleObject(g_injection_stop_event, 1000);
                if (wait_result == WAIT_OBJECT_0) {
                    OutputDebugStringA("Stop signal received, stopping injection");
                    StopInjectionInternal();
                    break;
                }
            } else {
                Sleep(1000);  // Sleep in 1-second intervals if stop event creation failed
            }
        }
    } else {
        OutputDebugStringA(
            "Auto-injection started: CBT hook installed, will inject into all new processes for 30 seconds");
        // Start cleanup thread
        StartInjectionCleanupThread();
        Sleep(30000);  // 30 seconds
    }
}

// Helper structure for LoadLibrary injection
struct loading_data {
    WCHAR load_path[MAX_PATH] = L"";
    decltype(&GetLastError) GetLastError = nullptr;
    decltype(&LoadLibraryW) LoadLibraryW = nullptr;
    const WCHAR env_var_name[30] = L"RESHADE_DISABLE_LOADING_CHECK";
    const WCHAR env_var_value[2] = L"1";
    decltype(&SetEnvironmentVariableW) SetEnvironmentVariableW = nullptr;
};

// Loading thread function that runs in the target process
// Sets environment variable and then loads the DLL
static DWORD WINAPI LoadingThreadFunc(loading_data* arg) {
    arg->SetEnvironmentVariableW(arg->env_var_name, arg->env_var_value);
    if (arg->LoadLibraryW(arg->load_path) == NULL) {
        return arg->GetLastError();
    }
    return ERROR_SUCCESS;
}

// Helper function to get ReShade DLL path based on architecture
std::wstring GetReShadeDllPath(bool is_wow64) {
    wchar_t documents_path[MAX_PATH];
    wchar_t localappdata_path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localappdata_path))) {
        return L"";
    }

    std::filesystem::path localappdata_dir(localappdata_path);
    std::filesystem::path dc_reshade_dir = localappdata_dir / L"Programs" / L"Display_Commander" / L"Reshade";

    std::filesystem::path reshade_path;
#ifdef _WIN64
    reshade_path = is_wow64 ? (dc_reshade_dir / L"Reshade32.dll") : (dc_reshade_dir / L"Reshade64.dll");
#else
    reshade_path = dc_reshade_dir / L"Reshade32.dll";
#endif

    if (std::filesystem::exists(reshade_path)) {
        std::error_code ec;
        std::filesystem::path absolute_path = std::filesystem::canonical(reshade_path, ec);
        if (ec) {
            absolute_path = std::filesystem::absolute(reshade_path, ec);
            if (ec) {
                absolute_path = reshade_path;
            }
        }
        return absolute_path.wstring();
    }

    return L"";
}

namespace {
// Open process and verify architecture. Returns true and sets *out_process on success. Caller must CloseHandle.
bool InjectIntoProcess_OpenTarget(DWORD pid, HANDLE* out_process) {
    HANDLE remote_process = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION,
        FALSE, pid);
    if (remote_process == nullptr) {
        DWORD error = GetLastError();
        OutputDebugStringA(
            std::format("Failed to open target process (PID {}): Error {} (0x{:X})", pid, error, error).c_str());
        return false;
    }
    BOOL remote_is_wow64 = FALSE;
    IsWow64Process(remote_process, &remote_is_wow64);
#ifdef _WIN64
    if (remote_is_wow64 != FALSE) {
        CloseHandle(remote_process);
        OutputDebugStringA(
            std::format("Process architecture mismatch: 32-bit process, but injector is 64-bit (PID {})", pid).c_str());
        return false;
    }
#else
    if (remote_is_wow64 == FALSE) {
        CloseHandle(remote_process);
        OutputDebugStringA(
            std::format("Process architecture mismatch: 64-bit process, but injector is 32-bit (PID {})", pid).c_str());
        return false;
    }
#endif
    *out_process = remote_process;
    return true;
}

// Alloc, write loading_data, CreateRemoteThread(LoadLibraryW), wait, cleanup. Does not close process handle.
bool InjectIntoProcess_DoRemoteLoadLibrary(HANDLE remote_process, DWORD pid, const std::wstring& dll_path) {
    loading_data arg;
    wcscpy_s(arg.load_path, dll_path.c_str());
    arg.GetLastError = GetLastError;
    arg.LoadLibraryW = LoadLibraryW;
    arg.SetEnvironmentVariableW = SetEnvironmentVariableW;

    LPVOID load_param =
        VirtualAllocEx(remote_process, nullptr, sizeof(arg), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (load_param == nullptr) {
        DWORD error = GetLastError();
        OutputDebugStringA(
            std::format("Failed to allocate memory in target process (PID {}): Error {} (0x{:X})", pid, error, error)
                .c_str());
        return false;
    }
    if (!WriteProcessMemory(remote_process, load_param, &arg, sizeof(arg), nullptr)) {
        DWORD error = GetLastError();
        OutputDebugStringA(
            std::format("Failed to write loading data to target process (PID {}): Error {} (0x{:X})", pid, error, error)
                .c_str());
        VirtualFreeEx(remote_process, load_param, 0, MEM_RELEASE);
        return false;
    }
    HANDLE load_thread = CreateRemoteThread(
        remote_process, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(arg.LoadLibraryW), load_param, 0, nullptr);
    if (load_thread == nullptr) {
        DWORD error = GetLastError();
        OutputDebugStringA(std::format("Failed to create remote thread in target process (PID {}): Error {} (0x{:X})",
                                       pid, error, error)
                               .c_str());
        VirtualFreeEx(remote_process, load_param, 0, MEM_RELEASE);
        return false;
    }
    WaitForSingleObject(load_thread, INFINITE);
    DWORD exit_code = 0;
    bool success = (GetExitCodeThread(load_thread, &exit_code) != 0 && exit_code != 0);
    if (success) {
        OutputDebugStringA(std::format("Successfully injected ReShade into process (PID {})", pid).c_str());
    } else {
        OutputDebugStringA(
            std::format("Failed to inject Display Commander into process (PID {}): LoadLibrary returned NULL", pid)
                .c_str());
    }
    CloseHandle(load_thread);
    VirtualFreeEx(remote_process, load_param, 0, MEM_RELEASE);
    return success;
}
}  // namespace

// Helper function to inject ReShade DLL into a process using LoadLibrary
bool InjectIntoProcess(DWORD pid, const std::wstring& dll_path) {
    HANDLE remote_process = nullptr;
    if (!InjectIntoProcess_OpenTarget(pid, &remote_process)) {
        return false;
    }
    bool success = InjectIntoProcess_DoRemoteLoadLibrary(remote_process, pid, dll_path);
    CloseHandle(remote_process);
    return success;
}

namespace {
void WaitForProcessAndInject_MarkExistingProcesses(const std::wstring& exe_name,
                                                   std::array<bool, 65536>& process_seen) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W process_entry = {};
    process_entry.dwSize = sizeof(PROCESSENTRY32W);
    int existing_count = 0;
    if (Process32FirstW(snapshot, &process_entry)) {
        do {
            if (_wcsicmp(process_entry.szExeFile, exe_name.c_str()) == 0) {
                DWORD pid = process_entry.th32ProcessID;
                if (pid < process_seen.size()) {
                    process_seen[pid] = true;
                    existing_count++;
                }
            }
        } while (Process32NextW(snapshot, &process_entry));
    }
    CloseHandle(snapshot);
    if (existing_count > 0) {
        OutputDebugStringA(std::format("Found {} existing process(es) with name '{}', will wait for new ones",
                                       existing_count, std::string(exe_name.begin(), exe_name.end()))
                               .c_str());
        std::cout << "Found " << existing_count << " existing process(es) with name '"
                  << std::string(exe_name.begin(), exe_name.end()) << "', will wait for new ones" << '\n';
    }
}

void WaitForProcessAndInject_ProcessSnapshot(const std::wstring& exe_name, std::array<bool, 65536>& process_seen) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W process_entry = {};
    process_entry.dwSize = sizeof(PROCESSENTRY32W);
    if (Process32FirstW(snapshot, &process_entry)) {
        do {
            DWORD pid = process_entry.th32ProcessID;
            if (pid >= process_seen.size()) continue;
            if (_wcsicmp(process_entry.szExeFile, exe_name.c_str()) != 0) {
                process_seen[pid] = true;
                continue;
            }
            if (process_seen[pid]) continue;
            process_seen[pid] = true;

            OutputDebugStringA(
                std::format("Found new process: {} (PID {})", std::string(exe_name.begin(), exe_name.end()), pid)
                    .c_str());
            std::cout << "Found new process: " << std::string(exe_name.begin(), exe_name.end()) << " (PID " << pid
                      << ")" << '\n';

            BOOL is_wow64 = FALSE;
            HANDLE h_process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
            if (h_process != nullptr) {
                IsWow64Process(h_process, &is_wow64);
                CloseHandle(h_process);
            }
            std::wstring dll_path = GetReShadeDllPath(is_wow64 != FALSE);
            if (dll_path.empty()) {
                OutputDebugStringA(
                    std::format("Failed to find ReShade DLL path for process (PID {}), continuing to wait", pid)
                        .c_str());
                std::cout << "Failed to find ReShade DLL path for process (PID " << pid << "), continuing to wait"
                          << '\n';
                continue;
            }
            if (InjectIntoProcess(pid, dll_path)) {
                OutputDebugStringA(std::format("Successfully injected into process (PID {})", pid).c_str());
                std::cout << "Successfully injected into process (PID " << pid << ")" << '\n';
            } else {
                OutputDebugStringA(
                    std::format("Failed to inject into process (PID {}), continuing to wait", pid).c_str());
                std::cout << "Failed to inject into process (PID " << pid << "), continuing to wait" << '\n';
            }
        } while (Process32NextW(snapshot, &process_entry));
    }
    CloseHandle(snapshot);
}
}  // namespace

// Wait for a process with given exe name to start, then inject ReShade DLL
// Waits forever and injects into every new process that starts with the given exe name
void WaitForProcessAndInject(const std::wstring& exe_name) {
    g_wait_and_inject_stop.store(false);
    OutputDebugStringA(std::format("Waiting for process: {} (will inject into all new instances)",
                                   std::string(exe_name.begin(), exe_name.end()))
                           .c_str());

    std::array<bool, 65536> process_seen = {};
    WaitForProcessAndInject_MarkExistingProcesses(exe_name, process_seen);

    while (!g_wait_and_inject_stop.load()) {
        WaitForProcessAndInject_ProcessSnapshot(exe_name, process_seen);
        Sleep(10);
    }
    OutputDebugStringA("WaitForProcessAndInject: Stopped");
    std::cout << "WaitForProcessAndInject: Stopped" << '\n';
}

// RunDLL entry point for rundll32.exe compatibility
// Allows calling: rundll32.exe zzz_display_commander.addon64,RunDLL_DllMain
// This installs a system-wide CBT hook - Windows automatically loads the DLL into
// all processes when CBT events occur (window creation, etc.). The hook installation
// itself is the injection mechanism. CBTProc is just for notification.
extern "C" __declspec(dllexport) void CALLBACK RunDLL_DllMain(HWND hwnd, HINSTANCE hInst, LPSTR lpszCmdLine,
                                                              int nCmdShow) {
    UNREFERENCED_PARAMETER(hwnd);
    UNREFERENCED_PARAMETER(hInst);
    UNREFERENCED_PARAMETER(lpszCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    StartInjectionInternal(false);  // 30 seconds
}

// RunDLL entry point for 30-second injection
// Allows calling: rundll32.exe zzz_display_commander.addon64,Service30
extern "C" __declspec(dllexport) void CALLBACK Service30(HWND hwnd, HINSTANCE hInst, LPSTR lpszCmdLine, int nCmdShow) {
    UNREFERENCED_PARAMETER(hwnd);
    UNREFERENCED_PARAMETER(hInst);
    UNREFERENCED_PARAMETER(lpszCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    StartInjectionInternal(false);  // 30 seconds
}

// RunDLL entry point for indefinite injection service
// Allows calling: rundll32.exe zzz_display_commander.addon64,Start
extern "C" __declspec(dllexport) void CALLBACK Start(HWND hwnd, HINSTANCE hInst, LPSTR lpszCmdLine, int nCmdShow) {
    UNREFERENCED_PARAMETER(hwnd);
    UNREFERENCED_PARAMETER(hInst);
    UNREFERENCED_PARAMETER(lpszCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    // Ensure only one DC service per architecture (32-bit / 64-bit) in the current session.
    // If another service is already running for this architecture, do not start a new one.
    if (!display_commander::dc_service::InitializeServiceForCurrentProcess()) {
        OutputDebugStringA("Start: DC service already running for this architecture or initialization failed");
        return;
    }

    StartInjectionInternal(true);  // Forever
}

// RunDLL entry point to stop injection service
// Allows calling: rundll32.exe zzz_display_commander.addon64,Stop
extern "C" __declspec(dllexport) void CALLBACK Stop(HWND hwnd, HINSTANCE hInst, LPSTR lpszCmdLine, int nCmdShow) {
    UNREFERENCED_PARAMETER(hwnd);
    UNREFERENCED_PARAMETER(hInst);
    UNREFERENCED_PARAMETER(lpszCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    // Stop WaitAndInject if it's running
    g_wait_and_inject_stop.store(true);
    OutputDebugStringA("Stop: Signaled WaitAndInject to stop");

    StopInjectionInternal();
}

// RunDLL entry point to set or reset an NVIDIA driver profile DWORD setting (SpecialK-compatible).
// Allows calling: rundll32.exe "path\to\addon64", RunDLL_NvAPI_SetDWORD <HexID> <HexValue|~> <fullExePath>
// [resultFilePath] Use ~ for value to reset the setting to driver default (calls NvAPI_DRS_DeleteProfileSetting).
// Optional resultFilePath: if present, the process writes "OK" or "ERROR: <message>" to that file for the caller to
// read. Requires admin for DRS changes; run rundll32 elevated if you get NVAPI_INVALID_USER_PRIVILEGE.
extern "C" __declspec(dllexport) void CALLBACK RunDLL_NvAPI_SetDWORD(HWND hwnd, HINSTANCE hInst, LPSTR lpszCmdLine,
                                                                     int nCmdShow) {
    UNREFERENCED_PARAMETER(hwnd);
    UNREFERENCED_PARAMETER(hInst);
    UNREFERENCED_PARAMETER(nCmdShow);

    if (lpszCmdLine == nullptr) {
        std::printf("Arguments: <HexID> <HexValue|~> <fullExePath> [resultFilePath]\n");
        return;
    }
    unsigned int settingId = 0, settingVal = 0;
    int vals = sscanf_s(lpszCmdLine, "%x %x ", &settingId, &settingVal);
    bool clearSetting = false;
    if (vals != 2) {
        vals = sscanf_s(lpszCmdLine, "%x ~ ", &settingId);
        if (vals != 1) {
            std::printf("Arguments: <HexID> <HexValue|~> <fullExePath> [resultFilePath]\n");
            return;
        }
        clearSetting = true;
    }
    // Remainder after "<HexID> <HexValue|~> ": exe name and optional result file path (last token if it looks like a
    // path)
    const char* p = lpszCmdLine;
    for (int spaceCount = 0; *p && spaceCount < 2; ++p) {
        if (*p == ' ') {
            ++spaceCount;
            if (spaceCount < 2) {
                while (*p == ' ') ++p;
            }
        }
    }
    while (*p == ' ') ++p;
    std::string remainder(p);
    std::string exeNameAnsi;
    std::string resultFilePathAnsi;
    {
        const std::string::size_type lastSpace = remainder.rfind(' ');
        if (lastSpace != std::string::npos && lastSpace + 1 < remainder.size()) {
            std::string lastToken = remainder.substr(lastSpace + 1);
            // Trim quotes from token
            if (lastToken.size() >= 2 && lastToken.front() == '"' && lastToken.back() == '"') {
                lastToken = lastToken.substr(1, lastToken.size() - 2);
            }
            if (lastToken.find('\\') != std::string::npos || lastToken.find(':') != std::string::npos) {
                resultFilePathAnsi = lastToken;
                exeNameAnsi = remainder.substr(0, lastSpace);
                while (!exeNameAnsi.empty() && exeNameAnsi.back() == ' ') {
                    exeNameAnsi.pop_back();
                }
            }
        }
        if (exeNameAnsi.empty()) {
            exeNameAnsi = remainder;
        }
    }
    // Trim surrounding quotes (e.g. "C:\Program Files\game.exe") so path is passed correctly
    if (exeNameAnsi.size() >= 2 && exeNameAnsi.front() == '"' && exeNameAnsi.back() == '"') {
        exeNameAnsi = exeNameAnsi.substr(1, exeNameAnsi.size() - 2);
    }
    if (exeNameAnsi.empty()) {
        std::printf("Missing executable path.\n");
        return;
    }
    int sizeNeeded = MultiByteToWideChar(CP_ACP, 0, exeNameAnsi.c_str(), -1, nullptr, 0);
    if (sizeNeeded <= 0) {
        std::printf("Failed to convert executable name to wide string.\n");
        return;
    }
    std::vector<wchar_t> exeNameWide(sizeNeeded);
    MultiByteToWideChar(CP_ACP, 0, exeNameAnsi.c_str(), -1, exeNameWide.data(), sizeNeeded);
    std::wstring exeName(exeNameWide.data());

    if (!::nvapi::EnsureNvApiInitialized()) {
        const std::string msg = "NVAPI failed to initialize (NVIDIA GPU required).";
        std::printf("%s\n", msg.c_str());
        if (!resultFilePathAnsi.empty()) {
            FILE* f = nullptr;
            if (fopen_s(&f, resultFilePathAnsi.c_str(), "w") == 0 && f != nullptr) {
                std::fprintf(f, "ERROR: %s\n", msg.c_str());
                std::fclose(f);
            }
        }
        return;
    }
    auto [ok, err] = display_commander::nvapi::SetOrDeleteProfileSettingForExe(
        exeName, static_cast<std::uint32_t>(settingId), clearSetting, static_cast<std::uint32_t>(settingVal));
    if (!resultFilePathAnsi.empty()) {
        FILE* f = nullptr;
        if (fopen_s(&f, resultFilePathAnsi.c_str(), "w") == 0 && f != nullptr) {
            if (ok) {
                std::fprintf(f, "OK\n");
            } else {
                std::fprintf(f, "ERROR: %s\n", err.c_str());
            }
            std::fclose(f);
        }
    }
    if (ok) {
        std::printf("Setting %s.\n", clearSetting ? "reset to default" : "applied");
    } else {
        std::printf("Failed: %s\n", err.c_str());
    }
}

namespace display_commander {

bool RunNvApiSetDwordAsAdmin(std::uint32_t settingId, std::uint32_t value, const std::wstring& exeName,
                             HANDLE* outProcess, std::string* outError, const std::wstring* resultFilePath) {
    HMODULE hMod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&RunDLL_NvAPI_SetDWORD), &hMod)) {
        if (outError != nullptr) {
            *outError = "Apply as administrator failed: could not get module handle.";
        }
        return false;
    }
    wchar_t dllPath[MAX_PATH] = {};
    if (GetModuleFileNameW(hMod, dllPath, MAX_PATH) == 0) {
        if (outError != nullptr) {
            *outError = "Apply as administrator failed: could not get DLL path.";
        }
        return false;
    }
    // Parameters: " \"path\", RunDLL_NvAPI_SetDWORD 0x<id> 0x<val> exeName"
    // Quote exeName if it contains spaces
    std::wstring params = L" \"" + std::wstring(dllPath) + L"\", RunDLL_NvAPI_SetDWORD 0x";
    wchar_t idBuf[16], valBuf[16];
    (void)swprintf_s(idBuf, L"%X", settingId);
    (void)swprintf_s(valBuf, L"%X", value);
    params += idBuf;
    params += L" 0x";
    params += valBuf;
    params += L" ";
    if (exeName.find(L' ') != std::wstring::npos) {
        params += L"\"" + exeName + L"\"";
    } else {
        params += exeName;
    }
    if (resultFilePath != nullptr && !resultFilePath->empty()) {
        params += L" ";
        if (resultFilePath->find(L' ') != std::wstring::npos) {
            params += L"\"" + *resultFilePath + L"\"";
        } else {
            params += *resultFilePath;
        }
    }
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = L"rundll32.exe";
    sei.lpParameters = params.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&sei) == FALSE) {
        if (outError != nullptr) {
            DWORD err = GetLastError();
            wchar_t errMsg[256] = {};
            DWORD len = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, err,
                                       MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), errMsg,
                                       static_cast<DWORD>(sizeof(errMsg) / sizeof(errMsg[0])), nullptr);
            while (len > 0 && (errMsg[len - 1] == L'\n' || errMsg[len - 1] == L'\r')) {
                errMsg[--len] = L'\0';
            }
            char errMsgNarrow[256] = {};
            if (len > 0) {
                WideCharToMultiByte(CP_UTF8, 0, errMsg, -1, errMsgNarrow, sizeof(errMsgNarrow), nullptr, nullptr);
            }
            *outError = "Apply as administrator failed: could not start elevated process. ";
            if (errMsgNarrow[0] != '\0') {
                *outError += errMsgNarrow;
            } else {
                *outError += "(error ";
                *outError += std::to_string(static_cast<unsigned long>(err));
                *outError += ")";
            }
            if (err == 1225) {  // ERROR_CANCELLED
                *outError += " (UAC was cancelled—click again and accept the prompt.)";
            }
        }
        return false;
    }
    if (outProcess != nullptr) {
        *outProcess = sei.hProcess;
    } else if (sei.hProcess != nullptr) {
        CloseHandle(sei.hProcess);
    }
    return true;
}

}  // namespace display_commander
