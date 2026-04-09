#include "main_new_tab.hpp"
#include "../../addon.hpp"
#include "../../adhd_multi_monitor/adhd_simple_api.hpp"
#include "../../config/default_overrides.hpp"
#include "../../config/default_settings_file.hpp"
#include "../../config/display_commander_config.hpp"
#include "../../display/hdr_control.hpp"
#include "../../dlss/dlss_indicator_manager.hpp"
#include "../../dxgi/vram_info.hpp"
#include "../../globals.hpp"
#include "../../hooks/loadlibrary_hooks.hpp"
#include "../../hooks/nvidia/ngx_hooks.hpp"
#include "../../hooks/nvidia/nvapi_hooks.hpp"
#include "../../hooks/present_traffic_tracking.hpp"
#include "../../hooks/vulkan/nvlowlatencyvk_hooks.hpp"
#include "../../hooks/vulkan/vulkan_loader_hooks.hpp"
#include "../../hooks/windows_hooks/api_hooks.hpp"
#include "../../hooks/windows_hooks/window_proc_hooks.hpp"
#include "../../hooks/windows_hooks/windows_message_hooks.hpp"
#include "../../latency/reflex_provider.hpp"
#include "../../latent_sync/latent_sync_limiter.hpp"
#include "../../latent_sync/refresh_rate_monitor_integration.hpp"
#include "../../modules/module_registry.hpp"
#include "../../nvapi/gpu_dynamic_utilization.hpp"
#include "../../nvapi/nvapi_init.hpp"
#include "../forkawesome.h"
#include "../ui_colors.hpp"
#include "../../settings/advanced_tab_settings.hpp"
#include "../../settings/experimental_tab_settings.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../settings/swapchain_tab_settings.hpp"
#include "../../utils/d3d9_api_version.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../widgets/resolution_widget/resolution_widget.hpp"
#include "new_ui_tabs.hpp"
#include "settings_wrapper.hpp"
#include "controls/main_tab_optional_panels.hpp"
#include "controls/display_settings/display_settings.hpp"
#include "controls/performance_overlay/important_info.hpp"
#include "../../utils/detour_call_tracker.hpp"
#include "../../version.hpp"

#include "imgui.h"

#include <d3d9.h>
#include <d3d9types.h>
#include <dxgi.h>
#include <minwindef.h>
#include <psapi.h>
#include <shellapi.h>
#include <sysinfoapi.h>
#include <reshade_imgui.hpp>

// Define IID for IDirect3DDevice9 if not already defined
#ifndef IID_IDirect3DDevice9
EXTERN_C const GUID IID_IDirect3DDevice9 = {
    0xd0223b96, 0xbf7a, 0x43fd, {0x92, 0xbd, 0xa4, 0x3b, 0x8d, 0x82, 0x9a, 0x7b}};
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// Minimum CPU cores that can be selected (excludes 1-5)
static constexpr int MIN_CPU_CORES_SELECTABLE = 6;

namespace ui::new_ui {

namespace {

// ReShade ADDON LoadFromDllMain: read once per process after g_reshade_module is set (avoids ini parse every frame).
std::atomic<bool> s_load_from_dll_main_fetched{false};
std::atomic<int32_t> s_load_from_dll_main_value{0};

// Draw DXGI overlay subsection (show DXGI VRR status, show DXGI refresh rate). Uses RefreshRateMonitor when
// enable_dxgi_refresh_rate_vrr_detection is on (Debug DXGI refresh tab in -DebugTabs builds, or config). Checkboxes are
// disabled when that setting is off.
void DrawDxgiOverlaySubsection(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Columns(1);
    imgui.Separator();
    imgui.TextUnformatted("DXGI");
    imgui.Columns(4, "overlay_checkboxes", false);

    const bool dxgi_detection_enabled =
        settings::g_advancedTabSettings.enable_dxgi_refresh_rate_vrr_detection.GetValue();
    if (!dxgi_detection_enabled) {
        imgui.BeginDisabled();
    }

    bool show_dxgi_vrr_status = settings::g_mainTabSettings.show_dxgi_vrr_status.GetValue();
    if (imgui.Checkbox("Show DXGI VRR status", &show_dxgi_vrr_status)) {
        settings::g_mainTabSettings.show_dxgi_vrr_status.SetValue(show_dxgi_vrr_status);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Shows DXGI-based VRR status in the performance overlay (RefreshRateMonitor heuristic: rate spread / "
            "samples below threshold). Enable \"DXGI refresh rate / VRR detection\" in the Debug DXGI refresh tab "
            "(-DebugTabs build) or via addon config for data.");
    }
    imgui.NextColumn();

    bool show_dxgi_refresh_rate = settings::g_mainTabSettings.show_dxgi_refresh_rate.GetValue();
    if (imgui.Checkbox("Show DXGI refresh rate", &show_dxgi_refresh_rate)) {
        settings::g_mainTabSettings.show_dxgi_refresh_rate.SetValue(show_dxgi_refresh_rate);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Shows DXGI refresh rate (Hz) in the performance overlay from swap chain GetFrameStatistics. "
            "Enable \"DXGI refresh rate / VRR detection\" in the Debug DXGI refresh tab (-DebugTabs build) or via addon "
            "config for data.");
    }
    imgui.NextColumn();

    if (!dxgi_detection_enabled) {
        imgui.EndDisabled();
    }

    imgui.Columns(1);

    // Show current DXGI VRR status and refresh rate on the main tab when detection is enabled
    if (dxgi_detection_enabled) {
        dxgi::fps_limiter::RefreshRateStats dxgi_stats = dxgi::fps_limiter::GetRefreshRateStats();
        double dxgi_hz = dxgi::fps_limiter::GetSmoothedRefreshRate();
        if (dxgi_stats.is_valid || dxgi_hz > 0.0) {
            imgui.Spacing();
            if (dxgi_stats.is_valid) {
                if (dxgi_stats.all_last_20_within_1s && dxgi_stats.samples_below_threshold_last_10s >= 2) {
                    imgui.TextColored(ui::colors::TEXT_SUCCESS, "VRR: On");
                } else {
                    imgui.TextColored(ui::colors::TEXT_DIMMED, "VRR: Off");
                }
                imgui.SameLine(0.0f, imgui.GetStyle().ItemInnerSpacing.x * 2.0f);
            }
            if (dxgi_hz > 0.0) {
                imgui.Text("Refresh rate: %.1f Hz (min %.1f / max %.1f)", dxgi_hz, dxgi_stats.min_rate,
                           dxgi_stats.max_rate);
            } else {
                imgui.TextColored(ui::colors::TEXT_DIMMED, "Refresh rate: -- Hz");
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "Current DXGI refresh rate from swap chain. Enable \"Show DXGI VRR status\" / \"Show DXGI refresh "
                    "rate\" above to show in overlay.");
            }
        }
    }
}

// Draw NVAPI stats subsection. Whole subsection is disabled when NVAPI is not initialized.
// (Optional NVAPI overlay stats remain 64-bit build only via is_64_bit().)
void DrawNvapiStatsOverlaySubsection(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Columns(1);  // Reset to single column
    imgui.Separator();
    imgui.TextWrapped(
        "NVAPI stats (NVIDIA only). These options may cause occasional hiccups; not available on Intel/AMD, "
        "Linux, or 32-bit builds.");
    imgui.Columns(4, "overlay_checkboxes", false);

    const bool nvapi_initialized = nvapi::EnsureNvApiInitialized();
    const bool nvapi_stats_available = nvapi_initialized && is_64_bit();
    if (!nvapi_stats_available) {
        imgui.BeginDisabled();
    }

    bool show_vrr_status = settings::g_mainTabSettings.show_vrr_status.GetValue();
    if (imgui.Checkbox("VRR Status", &show_vrr_status)) {
        settings::g_mainTabSettings.show_vrr_status.SetValue(show_vrr_status);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Shows whether Variable Refresh Rate (VRR) is active in the performance overlay. "
            "Uses NVAPI (NVIDIA only; may cause occasional hiccups).");
    }
    imgui.NextColumn();

    #if 0
    bool vrr_debug_mode = settings::g_mainTabSettings.vrr_debug_mode.GetValue();
    if (imgui.Checkbox("VRR Debug Mode", &vrr_debug_mode)) {
        settings::g_mainTabSettings.vrr_debug_mode.SetValue(vrr_debug_mode);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Shows detailed VRR debugging in the performance overlay: Fixed Hz, Threshold, Samples, and NVAPI "
            "fields from NvAPI_Disp_GetVRRInfo (NV_GET_VRR_INFO):\n"
            "  enabled: VRR is enabled for the display (driver/app has enabled it).\n"
            "  req: VRR has been requested (e.g. by the application or swap chain).\n"
            "  poss: The display and link support VRR (capability).\n"
            "  in_mode: The display is currently operating in VRR mode (authoritative hardware state).\n"
            "Uses NVAPI (NVIDIA only; may cause occasional hiccups).");
    }
    imgui.NextColumn();
    #endif

    bool show_overlay_nvapi_gpu_util = settings::g_mainTabSettings.show_overlay_nvapi_gpu_util.GetValue();
    if (imgui.Checkbox("GPU util", &show_overlay_nvapi_gpu_util)) {
        settings::g_mainTabSettings.show_overlay_nvapi_gpu_util.SetValue(show_overlay_nvapi_gpu_util);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Shows NVIDIA GPU engine busy %% in the performance overlay (NvAPI_GPU_GetDynamicPstatesInfoEx; "
            "driver-reported ~1 s rolling average). Uses the first enumerated physical GPU. "
            "Uses NVAPI (NVIDIA only; may cause occasional hiccups).");
    }
    imgui.NextColumn();

    bool show_nvapi_latency_stats = settings::g_mainTabSettings.show_nvapi_latency_stats.GetValue();
    if (imgui.Checkbox("Latency PCL(AV)", &show_nvapi_latency_stats)) {
        settings::g_mainTabSettings.show_nvapi_latency_stats.SetValue(show_nvapi_latency_stats);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Shows NVIDIA Reflex NVAPI latency stats (PC latency and GPU frame time) in the performance overlay.\n"
            "Requires a D3D11/D3D12 device with Reflex latency reporting available. Uses NvAPI_D3D_GetLatency, "
            "which may add minor overhead when enabled.");
    }

    imgui.Columns(1);  // Reset to single column

    if (!nvapi_stats_available) {
        imgui.EndDisabled();
    }
}

void ShowMainTabTopWarnings(display_commander::ui::IImGuiWrapper& imgui) {
    // Config save failure warning at the top
    g_rendering_ui_section.store("ui:tab:main_new:warnings:config", std::memory_order_release);
    auto config_save_failure_path = g_config_save_failure_path.load();
    if (config_save_failure_path != nullptr && !config_save_failure_path->empty()) {
        imgui.Spacing();
        imgui.TextColored(ui::colors::TEXT_ERROR, ICON_FK_WARNING " Error: Failed to save config to %s",
                          config_save_failure_path->c_str());
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("The configuration file could not be saved. Check file permissions and disk space.");
        }
        imgui.Spacing();
    }
    g_rendering_ui_section.store("ui:tab:main_new:warnings:vulkan1_not_loaded", std::memory_order_release);
    static bool vulkan1loaded = (GetModuleHandleW(L"vulkan-1.dll") != nullptr);
    if (!g_vulkan1_loaded_during_process_attach_init.load(std::memory_order_acquire) && vulkan1loaded) {
        imgui.TextColored(ui::colors::TEXT_WARNING,
                          ICON_FK_WARNING
                          "Display Commander was loaded before Vulkan Layer got initialized, "
                          "consider loading Display Commander as vulkan-1.dll or .addon64");
    }
}

}  // anonymous namespace

// Performance overlay / graphs / timeline implementations were extracted into:
// `ui/new_ui/controls/performance_overlay/*.cpp`

// DLSS UI was extracted into:
// `ui/new_ui/controls/dlss/dlss_info.*`

// Performance overlay / graphs implementations were extracted into:
// `ui/new_ui/controls/performance_overlay/*.cpp`

void InitMainNewTab() {
    CALL_GUARD_NO_TS();
    static bool settings_loaded_once = false;
    if (!settings_loaded_once) {
        // Settings already loaded at startup
        settings::g_mainTabSettings.LoadSettings();
        s_aspect_index = static_cast<AspectRatioType>(settings::g_mainTabSettings.aspect_index.GetValue());
        s_window_alignment = static_cast<WindowAlignment>(settings::g_mainTabSettings.alignment.GetValue());
        // FPS limits are now automatically synced via FloatSettingRef
        // Audio mute settings are automatically synced via BoolSettingRef
        // Background mute settings are automatically synced via BoolSettingRef
        // VSync & Tearing - all automatically synced via BoolSettingRef

        // Apply loaded mute state immediately if manual mute is enabled
        // Apply mute setting to the audio system
     /*   if (settings::g_mainTabSettings.audio_mute.GetValue()) {
            if (::SetMuteForCurrentProcess(true)) {
                ::g_muted_applied.store(true);
                LogInfo("Audio mute state loaded and applied from settings");
            } else {
                LogWarn("Failed to apply loaded mute state");
            }
        }*/

        // Update input blocking system with loaded settings
        // Input blocking is now handled by Windows message hooks

        settings_loaded_once = true;

        // FPS limiter: enabled checkbox + mode (0=OnPresentSync, 1=Reflex, 2=LatentSync; clamp to 0-2)
        s_fps_limiter_enabled.store(settings::g_mainTabSettings.fps_limiter_enabled.GetValue());
        int mode_val = settings::g_mainTabSettings.fps_limiter_mode.GetValue();
        if (mode_val < 0 || mode_val > 2) {
            mode_val = (mode_val < 0) ? 0 : 2;
            settings::g_mainTabSettings.fps_limiter_mode.SetValue(mode_val);
        }
        s_fps_limiter_mode.store(static_cast<FpsLimiterMode>(mode_val));
        // Scanline offset and VBlank Sync Divisor are now automatically synced via IntSettingRef

        // Initialize resolution widget
        display_commander::widgets::resolution_widget::InitializeResolutionWidget();

        // Log level is read directly from settings::g_mainTabSettings.log_level when needed (GetMinLogLevel()).
    }
}


void DrawAdvancedSettings(display_commander::ui::IImGuiWrapper& imgui) {
    (void)imgui;
    CALL_GUARD_NO_TS();
    // Advanced Settings Control
    {
        bool advanced_settings = settings::g_mainTabSettings.advanced_settings_enabled.GetValue();
        if (imgui.Checkbox(ICON_FK_FILE_CODE " Show All Tabs", &advanced_settings)) {
            settings::g_mainTabSettings.advanced_settings_enabled.SetValue(advanced_settings);
            LogInfo("Advanced settings %s", advanced_settings ? "enabled" : "disabled");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Enable advanced settings to show advanced tabs (Advanced, Debug, etc.).\n"
                "When disabled, advanced tabs will be hidden to simplify the interface.");
        }
    }

    imgui.Spacing();

    // Logging Level Control
    if (ComboSettingEnumWrapper(settings::g_mainTabSettings.log_level, "Logging Level", imgui)) {
        // Always log the level change (using LogCurrentLogLevel which uses LogError)
        LogCurrentLogLevel();
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Controls the minimum log level to display:\n\n"
            "- Error Only: Only error messages\n"
            "- Warning: Errors and warnings\n"
            "- Info: Errors, warnings, and info messages\n"
            "- Debug (Everything): All log messages (default)");
    }

    imgui.Spacing();

    // Individual Tab Visibility Settings
    imgui.Text("Show Individual Tabs:");
    imgui.Indent();

    if (ui::new_ui::g_tab_manager.HasTab("hotkeys")) {
        if (CheckboxSetting(settings::g_mainTabSettings.show_hotkeys_tab, "Show Hotkeys Tab", imgui)) {
            LogInfo("Show Hotkeys tab %s",
                    settings::g_mainTabSettings.show_hotkeys_tab.GetValue() ? "enabled" : "disabled");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Show the Hotkeys tab (keyboard shortcuts for volume, overlay, window actions, etc.).");
        }
    }

    if (ui::new_ui::g_tab_manager.HasTab("advanced")) {
        if (CheckboxSetting(settings::g_mainTabSettings.show_advanced_tab, "Show Advanced Tab", imgui)) {
            LogInfo("Show Advanced tab %s",
                    settings::g_mainTabSettings.show_advanced_tab.GetValue() ? "enabled" : "disabled");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Shows the Advanced tab even when 'Show All Tabs' is disabled.");
        }
    }

    if (ui::new_ui::g_tab_manager.HasTab("controller")) {
        if (CheckboxSetting(settings::g_mainTabSettings.show_controller_tab, "Show Controller Tab", imgui)) {
            LogInfo("Show Controller tab %s",
                    settings::g_mainTabSettings.show_controller_tab.GetValue() ? "enabled" : "disabled");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Shows the Controller tab (XInput monitoring and remapping) even when 'Show All Tabs' is disabled.");
        }
    }

    if (ui::new_ui::g_tab_manager.HasTab("reshade")) {
        if (CheckboxSetting(settings::g_mainTabSettings.show_reshade_tab, "Show ReShade Tab", imgui)) {
            LogInfo("Show ReShade tab %s",
                    settings::g_mainTabSettings.show_reshade_tab.GetValue() ? "enabled" : "disabled");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Shows the ReShade/Addons tab even when 'Show All Tabs' is disabled.");
        }
    }

    const std::vector<modules::ModuleDescriptor> modules_list = modules::GetModules();
    if (!modules_list.empty()) {
        imgui.Spacing();
        imgui.Text("Module Features:");
        imgui.Indent();
        for (const modules::ModuleDescriptor& module : modules_list) {
            bool enabled = module.enabled;
            const std::string checkbox_label = "Enable " + module.display_name;
            if (imgui.Checkbox(checkbox_label.c_str(), &enabled)) {
                modules::SetModuleEnabled(module.id, enabled);
                LogInfo("Module '%s' %s", module.id.c_str(), enabled ? "enabled" : "disabled");
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("%s", module.description.c_str());
            }

            if (enabled) {
                imgui.SameLine();
                bool show_overlay = module.show_in_overlay;
                const std::string overlay_label = "Overlay##" + module.id;
                if (imgui.Checkbox(overlay_label.c_str(), &show_overlay)) {
                    modules::SetModuleOverlayEnabled(module.id, show_overlay);
                    LogInfo("Module '%s' overlay %s", module.id.c_str(), show_overlay ? "enabled" : "disabled");
                }
            }

            modules::DrawModuleMainTabInlineById(module.id, imgui, nullptr);
        }
        imgui.Unindent();
    }

    imgui.Unindent();

    imgui.Spacing();

    DrawMainTabOptionalPanelsAdvancedSettingsUi(imgui);

    imgui.Spacing();
}

display_commander::ui::GraphicsApi GetGraphicsApiFromRuntime(reshade::api::effect_runtime* runtime) {
    if (!runtime) return display_commander::ui::GraphicsApi::Unknown;
    auto rapi = runtime->get_device()->get_api();
    switch (rapi) {
        case reshade::api::device_api::d3d9:   return display_commander::ui::GraphicsApi::D3D9;
        case reshade::api::device_api::d3d10:  return display_commander::ui::GraphicsApi::D3D10;
        case reshade::api::device_api::d3d11:  return display_commander::ui::GraphicsApi::D3D11;
        case reshade::api::device_api::d3d12:  return display_commander::ui::GraphicsApi::D3D12;
        case reshade::api::device_api::opengl: return display_commander::ui::GraphicsApi::OpenGL;
        case reshade::api::device_api::vulkan: return display_commander::ui::GraphicsApi::Vulkan;
        default:                               return display_commander::ui::GraphicsApi::Unknown;
    }
}

display_commander::ui::GraphicsApi GetGraphicsApiFromLastDeviceApi() {
    if (!(g_reshade_module != nullptr)) return display_commander::ui::GraphicsApi::Unknown;
    const reshade::api::device_api api = g_last_reshade_device_api.load();
    switch (api) {
        case reshade::api::device_api::d3d9:   return display_commander::ui::GraphicsApi::D3D9;
        case reshade::api::device_api::d3d10:  return display_commander::ui::GraphicsApi::D3D10;
        case reshade::api::device_api::d3d11:  return display_commander::ui::GraphicsApi::D3D11;
        case reshade::api::device_api::d3d12:  return display_commander::ui::GraphicsApi::D3D12;
        case reshade::api::device_api::opengl: return display_commander::ui::GraphicsApi::OpenGL;
        case reshade::api::device_api::vulkan: return display_commander::ui::GraphicsApi::Vulkan;
        default:                               return display_commander::ui::GraphicsApi::Unknown;
    }
}


void DrawMainNewTab(display_commander::ui::GraphicsApi api, display_commander::ui::IImGuiWrapper& imgui,
                    reshade::api::effect_runtime* runtime) {
    CALL_GUARD_NO_TS();
    RefreshReShadeModuleIfNeeded();
    // Load saved settings once and sync legacy globals
    g_rendering_ui_section.store("ui:tab:main_new:entry", std::memory_order_release);

    // Game default overrides info banner (when overrides are in use for this exe)
    if (display_commander::config::HasActiveOverrides()) {
        g_rendering_ui_section.store("ui:tab:main_new:default_overrides", std::memory_order_release);
        imgui.Spacing();
        imgui.TextColored(ImVec4(0.4f, 0.6f, 0.9f, 1.0f),
                          ICON_FK_FILE " Game default overrides are in use for this game.");
        if (imgui.IsItemHovered()) {
            std::string tooltip = "Active overrides (per-exe defaults):\n";
            for (const auto& e : display_commander::config::GetActiveOverrideEntries()) {
                std::string value_display = (e.value == "1") ? "On" : (e.value == "0") ? "Off" : e.value;
                tooltip += "  - " + e.display_name + " = " + value_display + "\n";
            }
            tooltip += "\nClick \"Apply to config\" to save these to DisplayCommander.ini.";
            imgui.SetTooltipEx("%s", tooltip.c_str());
        }
        imgui.SameLine();
        if (imgui.Button("Apply to config")) {
            display_commander::config::ApplyDefaultOverridesToConfig();
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Save the current override values to this game's DisplayCommander.ini.");
        }
        imgui.Spacing();
    }

    ShowMainTabTopWarnings(imgui);

    g_rendering_ui_section.store("ui:tab:main_new:warnings:load_from_dll", std::memory_order_release);
    // LoadFromDllMain warning (config read once; requires restart to pick up ini changes)
    int32_t load_from_dll_main_value = 0;
    if (g_reshade_module != nullptr) {
        if (!s_load_from_dll_main_fetched.load(std::memory_order_acquire)) {
            int32_t v = 0;
            const bool ok = reshade::get_config_value(nullptr, "ADDON", "LoadFromDllMain", v);
            s_load_from_dll_main_value.store(ok ? v : 0, std::memory_order_relaxed);
            s_load_from_dll_main_fetched.store(true, std::memory_order_release);
        }
        load_from_dll_main_value = s_load_from_dll_main_value.load(std::memory_order_relaxed);
    }
    if (load_from_dll_main_value == 1) {
        imgui.Spacing();
        imgui.TextColored(ui::colors::TEXT_WARNING,
                          ICON_FK_WARNING " WARNING: LoadFromDllMain is set to 1 in ReShade configuration");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "LoadFromDllMain=1 can cause compatibility issues with some games and addons. "
                "Consider disabling it in the Advanced tab or ReShade.ini if you experience problems.");
        }
        imgui.Spacing();
    }

    g_rendering_ui_section.store("ui:tab:main_new:warnings:multi_version", std::memory_order_release);
    // Multiple Display Commander versions warning
    auto other_dc_version = g_other_dc_version_detected.load();
    if (other_dc_version != nullptr && !other_dc_version->empty()) {
        imgui.Spacing();
        imgui.TextColored(ui::colors::TEXT_ERROR,
                          ICON_FK_WARNING " ERROR: Multiple Display Commander versions detected!");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Another Display Commander instance (v%s) is loaded in this process. "
                "This may cause conflicts and unexpected behavior. Please ensure only one version is loaded.",
                other_dc_version->c_str());
        }
        imgui.SameLine();
        imgui.TextColored(ui::colors::TEXT_ERROR, "Other version: v%s", other_dc_version->c_str());
        imgui.Spacing();
    }

    g_rendering_ui_section.store("ui:tab:main_new:warnings:multi_swapchain", std::memory_order_release);
    // Multiple swapchains (ReShade runtimes) warning — skip when RenoDX is loaded (expected multiple runtimes)
    const size_t runtime_count = GetReShadeRuntimeCount();
    if (runtime_count > 1 && !g_is_renodx_loaded.load(std::memory_order_relaxed)) {
        imgui.Spacing();
        imgui.TextColored(ui::colors::TEXT_WARNING,
                          ICON_FK_WARNING " WARNING: Multiple swapchains detected (%zu ReShade runtimes)",
                          runtime_count);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "More than one swapchain/runtime is active. Some features may target only the first runtime. "
                "This can happen with multi-window or multi-context games.");
        }
        imgui.Spacing();
    }

    g_rendering_ui_section.store("ui:tab:main_new:version_build", std::memory_order_release);
    // Version and build information at the top
    {
        imgui.TextColored(ui::colors::TEXT_DEFAULT, "Version: %s", DISPLAY_COMMANDER_VERSION_STRING);

        // Display current graphics API with feature level/version
        const reshade::api::device_api api = g_last_reshade_device_api.load();
        imgui.SameLine();
        if (api != static_cast<reshade::api::device_api>(0)) {
            uint32_t api_version = g_last_api_version.load();

            if (api == reshade::api::device_api::d3d9 && s_d3d9e_upgrade_successful.load()) {
                api_version =
                    static_cast<uint32_t>(display_commander::D3D9ApiVersion::D3D9Ex);  // due to reshade's bug.
            }

            // Display API with version/feature level and bitness
#ifdef _WIN64
            const char* bitness_label = "64-bit";
#else
            const char* bitness_label = "32-bit";
#endif
            if (api_version != 0) {
                std::string api_string = GetDeviceApiVersionString(api, api_version);
                imgui.TextColored(ui::colors::TEXT_LABEL, "| %s: %s", bitness_label, api_string.c_str());
            } else {
                imgui.TextColored(ui::colors::TEXT_LABEL, "| %s: %s", bitness_label, GetDeviceApiString(api));
            }
        } else {
#ifdef _WIN64
            const char* bitness_label = "64-bit";
#else
            const char* bitness_label = "32-bit";
#endif
            imgui.TextColored(ui::colors::TEXT_LABEL, "| %s", bitness_label);
        }

        // Ko-fi support button
        //imgui.SameLine();
        //imgui.TextColored(ui::colors::TEXT_LABEL, "Support the project:");
        imgui.PushStyleColor(ImGuiCol_Text, ui::colors::ICON_SPECIAL);
        if (imgui.Button(ICON_FK_PLUS " Buy me a coffee on Ko-fi")) {
            ShellExecuteA(nullptr, "open", "https://ko-fi.com/pmnox", nullptr, nullptr, SW_SHOW);
        }
        imgui.PopStyleColor();
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Support Display Commander development with a coffee!");
        }
    }

    // Display Settings Section
    g_rendering_ui_section.store("ui:tab:main_new:display_settings", std::memory_order_release);
    ui::colors::PushHeaderColors(&imgui);
    const bool display_settings_open = imgui.CollapsingHeader("Display Settings", ImGuiTreeNodeFlags_DefaultOpen);
    ui::colors::PopCollapsingHeaderColors(&imgui);
    if (display_settings_open) {
        imgui.Indent();
        DrawDisplaySettings(api, imgui, runtime);
        imgui.Unindent();
    }

    imgui.Spacing();

    // Monitor/Display Resolution Settings Section
    g_rendering_ui_section.store("ui:tab:main_new:resolution", std::memory_order_release);
    ui::colors::PushHeaderColors(&imgui);
    const bool resolution_control_open = imgui.CollapsingHeader("Resolution Control", ImGuiTreeNodeFlags_None);
    ui::colors::PopCollapsingHeaderColors(&imgui);
    if (resolution_control_open) {
        imgui.Indent();
        display_commander::widgets::resolution_widget::DrawResolutionWidget(imgui);
        imgui.Unindent();
    }

    imgui.Spacing();

    g_rendering_ui_section.store("ui:tab:main_new:window_control", std::memory_order_release);
    ui::colors::PushHeaderColors(&imgui);
    const bool window_control_open = imgui.CollapsingHeader("Window Control", ImGuiTreeNodeFlags_None);
    ui::colors::PopCollapsingHeaderColors(&imgui);
    if (window_control_open) {
        imgui.Indent();

        CheckboxSetting(settings::g_advancedTabSettings.prevent_always_on_top, "Prevent Always On Top", imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Prevents windows from becoming always on top, even if they are moved or resized.");
        }

        imgui.SameLine();
        CheckboxSetting(settings::g_advancedTabSettings.prevent_minimize, "Prevent Minimize", imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Prevents the game window from being minimized (e.g. via taskbar or system menu).");
        }


        // Continue rendering in background (also labeled "Fake Fullscreen" for discoverability)
        static bool continue_rendering_changed = false;
        if (CheckboxSetting(settings::g_advancedTabSettings.continue_rendering,
                            "Continue Rendering in Background (Fake Fullscreen)", imgui)) {
            continue_rendering_changed = true;
            bool new_value = settings::g_advancedTabSettings.continue_rendering.GetValue();
            LogInfo("Continue rendering in background %s", new_value ? "enabled" : "disabled");

            // Install or uninstall window proc hooks based on the setting
            HWND game_window = display_commanderhooks::GetGameWindow();
            if (new_value) {
                // Enable: Install hooks if we have a valid window
                if (game_window != nullptr && IsWindow(game_window)) {
                    if (display_commanderhooks::InstallWindowProcHooks(game_window)) {
                        LogInfo("Window procedure hooks installed after enabling continue rendering");
                    } else {
                        LogWarn("Failed to install window procedure hooks after enabling continue rendering");
                    }
                } else {
                    LogInfo("Window procedure hooks will be installed when a valid window is available");
                }
            } else {
                // Disable: Uninstall hooks
                display_commanderhooks::UninstallWindowProcHooks();
                LogInfo("Window procedure hooks uninstalled after disabling continue rendering");
            }
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Prevent games from pausing or reducing performance when alt-tabbed. Blocks window focus "
                "messages so the game keeps running in the background. Also called fake fullscreen.");
        }

        /*
        if (display_commanderhooks::g_wgi_state.wgi_called.load() && continue_rendering_changed) {
            imgui.TextColored(ui::colors::TEXT_WARNING,
                              ICON_FK_WARNING " Game restart may be required for changes to take full effect.");
        }
        imgui.Spacing();
*/
        // Prevent display sleep & screensaver mode
        if (RadioSettingEnumWrapper(
                settings::g_mainTabSettings.screensaver_mode, "Prevent display sleep & screensaver",
                "Controls display sleep and screensaver while the game is running:\n\n"
                "- Default: Preserves original game behavior\n"
                "- In foreground: Prevents display sleep & screensaver while the game window is focused\n"
                "- Always: Prevents display sleep & screensaver for the whole session\n\n"
                "Note: Enable \"Prevent display sleep & screensaver\" in the Advanced tab for this to take effect.",
                imgui, RadioSettingLayout::Horizontal)) {
            LogInfo("Prevent display sleep & screensaver mode changed to %d",
                    settings::g_mainTabSettings.screensaver_mode.GetValue());
        }

        imgui.Unindent();
    }

    imgui.Spacing();

    g_rendering_ui_section.store("ui:tab:main_new:performance_overlay", std::memory_order_release);
    ui::colors::PushHeaderColors(&imgui);
    const bool performance_overlay_open = imgui.CollapsingHeader("Performance Overlay", ImGuiTreeNodeFlags_None);
    ui::colors::PopCollapsingHeaderColors(&imgui);
    if (performance_overlay_open) {
        imgui.Indent();
        DrawImportantInfo(imgui);
        imgui.Unindent();
    }
    imgui.Spacing();
    g_rendering_ui_section.store("ui:tab:main_new:advanced_settings", std::memory_order_release);
    ui::colors::PushHeaderColors(&imgui);
    const bool advanced_settings_open = imgui.CollapsingHeader("Features", ImGuiTreeNodeFlags_None);
    ui::colors::PopCollapsingHeaderColors(&imgui);
    if (advanced_settings_open) {
        imgui.Indent();
        DrawAdvancedSettings(imgui);
        imgui.Unindent();
    }

    DrawMainTabOptionalPanelsInOrder(api, imgui, runtime);
}

void DrawWindowControlButtons(display_commander::ui::IImGuiWrapper& imgui) {
    (void)imgui;
    CALL_GUARD_NO_TS();
    HWND hwnd = g_last_swapchain_hwnd.load();
    if (hwnd == nullptr) {
        LogWarn("Maximize Window: no window handle available");
        return;
    }
    // Window Control Buttons (Minimize, Restore, and Maximize side by side)
    imgui.BeginGroup();

    // Minimize Window Button
    imgui.PushStyleColor(ImGuiCol_Text, ui::colors::ICON_ACTION);
    if (imgui.Button(ICON_FK_MINUS " Minimize Window")) {
        HWND hwnd = g_last_swapchain_hwnd.load();
        std::thread([hwnd]() {
            LogDebug("Minimize Window button pressed (bg thread)");
            ShowWindow(hwnd, SW_MINIMIZE);
        }).detach();
    }
    imgui.PopStyleColor();
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Minimize the current game window.");
    }


    imgui.SameLine();

    // Close Window Button (graceful close via WM_CLOSE)
    imgui.PushStyleColor(ImGuiCol_Text, ui::colors::ICON_ACTION);
    if (imgui.Button(ICON_FK_CANCEL " Close")) {
        HWND close_hwnd = g_last_swapchain_hwnd.load();
        std::thread([close_hwnd]() {
            LogDebug("Close button pressed (bg thread)");
            PostMessageW(close_hwnd, WM_CLOSE, 0, 0);
        }).detach();
    }
    imgui.PopStyleColor();
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Request graceful close of the game window (sends WM_CLOSE).");
    }

    imgui.SameLine();

    // Open Game Folder Button
    imgui.PushStyleColor(ImGuiCol_Text, ui::colors::ICON_ACTION);
    if (imgui.Button(ICON_FK_FOLDER_OPEN " Open Game Folder")) {
        std::thread([]() {
            LogDebug("Open Game Folder button pressed (bg thread)");

            // Get current process executable path
            char process_path[MAX_PATH];
            DWORD path_length = GetModuleFileNameA(nullptr, process_path, MAX_PATH);

            if (path_length == 0) {
                LogError("Failed to get current process path for folder opening");
                return;
            }

            // Get the parent directory of the executable
            std::string full_path(process_path);
            size_t last_slash = full_path.find_last_of("\\/");

            if (last_slash == std::string::npos) {
                LogError("Invalid process path format: %s", full_path.c_str());
                return;
            }

            std::string game_folder = full_path.substr(0, last_slash);
            LogInfo("Opening game folder: %s", game_folder.c_str());

            // Open the folder in Windows Explorer
            HINSTANCE result = ShellExecuteA(nullptr, "explore", game_folder.c_str(), nullptr, nullptr, SW_SHOW);

            if (reinterpret_cast<intptr_t>(result) <= 32) {
                LogError("Failed to open game folder: %s (Error: %ld)", game_folder.c_str(),
                         reinterpret_cast<intptr_t>(result));
            } else {
                LogInfo("Successfully opened game folder: %s", game_folder.c_str());
            }
        }).detach();
    }
    imgui.PopStyleColor();
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Open the game's installation folder in Windows Explorer.");
    }

    imgui.SameLine();

    // Open DisplayCommander.ini (config) Button
    imgui.PushStyleColor(ImGuiCol_Text, ui::colors::ICON_ACTION);
    if (imgui.Button(ICON_FK_FILE " Config")) {
        std::string config_path =
            display_commander::config::DisplayCommanderConfigManager::GetInstance().GetConfigPath();
        if (!config_path.empty()) {
            std::thread([config_path]() {
                LogDebug("Open DisplayCommander.toml button pressed (bg thread)");
                LogInfo("Opening DisplayCommander.ini: %s", config_path.c_str());
                HINSTANCE result = ShellExecuteA(nullptr, "open", config_path.c_str(), nullptr, nullptr, SW_SHOW);
                if (reinterpret_cast<intptr_t>(result) <= 32) {
                    LogError("Failed to open DisplayCommander.ini: %s (Error: %ld)", config_path.c_str(),
                             reinterpret_cast<intptr_t>(result));
                } else {
                    LogInfo("Successfully opened DisplayCommander.ini: %s", config_path.c_str());
                }
            }).detach();
        }
    }
    imgui.PopStyleColor();
    if (imgui.IsItemHovered()) {
        std::string config_path =
            display_commander::config::DisplayCommanderConfigManager::GetInstance().GetConfigPath();
        if (!config_path.empty()) {
            imgui.SetTooltipEx("Open DisplayCommander config in the default text editor.\nFull path: %s",
                               config_path.c_str());
        } else {
            imgui.SetTooltipEx("Open DisplayCommander.ini (config path not available).");
        }
    }

    imgui.SameLine();

    // Open DisplayCommander.log Button
    imgui.PushStyleColor(ImGuiCol_Text, ui::colors::ICON_ACTION);
    if (imgui.Button(ICON_FK_FILE " Log")) {
        std::thread([]() {
            LogDebug("Open DisplayCommander.log button pressed (bg thread)");

            // Get current process executable path
            char process_path[MAX_PATH];
            DWORD path_length = GetModuleFileNameA(nullptr, process_path, MAX_PATH);

            if (path_length == 0) {
                LogError("Failed to get current process path for log file opening");
                return;
            }

            // Get the parent directory of the executable
            std::string full_path(process_path);
            size_t last_slash = full_path.find_last_of("\\/");

            if (last_slash == std::string::npos) {
                LogError("Invalid process path format: %s", full_path.c_str());
                return;
            }

            std::string log_path = full_path.substr(0, last_slash) + "\\DisplayCommander.log";
            LogInfo("Opening DisplayCommander.log: %s", log_path.c_str());

            // Open the log file with default text editor
            HINSTANCE result = ShellExecuteA(nullptr, "open", log_path.c_str(), nullptr, nullptr, SW_SHOW);

            if (reinterpret_cast<intptr_t>(result) <= 32) {
                LogError("Failed to open DisplayCommander.log: %s (Error: %ld)", log_path.c_str(),
                         reinterpret_cast<intptr_t>(result));
            } else {
                LogInfo("Successfully opened DisplayCommander.log: %s", log_path.c_str());
            }
        }).detach();
    }
    imgui.PopStyleColor();
    if (imgui.IsItemHovered()) {
        char process_path[MAX_PATH];
        if (GetModuleFileNameA(nullptr, process_path, MAX_PATH) != 0) {
            std::string full_path(process_path);
            size_t last_slash = full_path.find_last_of("\\/");
            if (last_slash != std::string::npos) {
                std::string log_path = full_path.substr(0, last_slash) + "\\DisplayCommander.log";
                imgui.SetTooltipEx("Open DisplayCommander.log in the default text editor.\nFull path: %s",
                                   log_path.c_str());
            } else {
                imgui.SetTooltipEx("Open DisplayCommander.log in the default text editor.");
            }
        } else {
            imgui.SetTooltipEx("Open DisplayCommander.log in the default text editor.");
        }
    }

    if ((g_reshade_module != nullptr)) {
        imgui.SameLine();

        // Open reshade.log Button
        imgui.PushStyleColor(ImGuiCol_Text, ui::colors::ICON_ACTION);
        if (imgui.Button(ICON_FK_FILE " reshade.log")) {
            std::thread([]() {
                LogDebug("Open reshade.log button pressed (bg thread)");

                // Get current process executable path
                char process_path[MAX_PATH];
                DWORD path_length = GetModuleFileNameA(nullptr, process_path, MAX_PATH);

                if (path_length == 0) {
                    LogError("Failed to get current process path for log file opening");
                    return;
                }

                // Get the parent directory of the executable
                std::string full_path(process_path);
                size_t last_slash = full_path.find_last_of("\\/");

                if (last_slash == std::string::npos) {
                    LogError("Invalid process path format: %s", full_path.c_str());
                    return;
                }

                std::string log_path = full_path.substr(0, last_slash) + "\\reshade.log";
                LogInfo("Opening reshade.log: %s", log_path.c_str());

                // Open the log file with default text editor
                HINSTANCE result = ShellExecuteA(nullptr, "open", log_path.c_str(), nullptr, nullptr, SW_SHOW);

                if (reinterpret_cast<intptr_t>(result) <= 32) {
                    LogError("Failed to open reshade.log: %s (Error: %ld)", log_path.c_str(),
                             reinterpret_cast<intptr_t>(result));
                } else {
                    LogInfo("Successfully opened reshade.log: %s", log_path.c_str());
                }
            }).detach();
        }
        imgui.PopStyleColor();
        if (imgui.IsItemHovered()) {
            char process_path[MAX_PATH];
            if (GetModuleFileNameA(nullptr, process_path, MAX_PATH) != 0) {
                std::string full_path(process_path);
                size_t last_slash = full_path.find_last_of("\\/");
                if (last_slash != std::string::npos) {
                    std::string log_path = full_path.substr(0, last_slash) + "\\reshade.log";
                    imgui.SetTooltipEx("Open reshade.log in the default text editor.\nFull path: %s", log_path.c_str());
                } else {
                    imgui.SetTooltipEx("Open reshade.log in the default text editor.");
                }
            } else {
                imgui.SetTooltipEx("Open reshade.log in the default text editor.");
            }
        }

        imgui.SameLine();

        // Open reshade.ini Button
        imgui.PushStyleColor(ImGuiCol_Text, ui::colors::ICON_ACTION);
        if (imgui.Button(ICON_FK_FILE " reshade.ini")) {
            std::thread([]() {
                LogDebug("Open reshade.ini button pressed (bg thread)");

                char process_path[MAX_PATH];
                DWORD path_length = GetModuleFileNameA(nullptr, process_path, MAX_PATH);

                if (path_length == 0) {
                    LogError("Failed to get current process path for reshade.ini opening");
                    return;
                }

                std::string full_path(process_path);
                size_t last_slash = full_path.find_last_of("\\/");

                if (last_slash == std::string::npos) {
                    LogError("Invalid process path format: %s", full_path.c_str());
                    return;
                }

                std::string ini_path = full_path.substr(0, last_slash) + "\\reshade.ini";
                LogInfo("Opening reshade.ini: %s", ini_path.c_str());

                HINSTANCE result = ShellExecuteA(nullptr, "open", ini_path.c_str(), nullptr, nullptr, SW_SHOW);

                if (reinterpret_cast<intptr_t>(result) <= 32) {
                    LogError("Failed to open reshade.ini: %s (Error: %ld)", ini_path.c_str(),
                             reinterpret_cast<intptr_t>(result));
                } else {
                    LogInfo("Successfully opened reshade.ini: %s", ini_path.c_str());
                }
            }).detach();
        }
        imgui.PopStyleColor();
        if (imgui.IsItemHovered()) {
            char process_path[MAX_PATH];
            if (GetModuleFileNameA(nullptr, process_path, MAX_PATH) != 0) {
                std::string full_path(process_path);
                size_t last_slash = full_path.find_last_of("\\/");
                if (last_slash != std::string::npos) {
                    std::string ini_path = full_path.substr(0, last_slash) + "\\reshade.ini";
                    imgui.SetTooltipEx("Open reshade.ini (ReShade config) in the default text editor.\nFull path: %s",
                                       ini_path.c_str());
                } else {
                    imgui.SetTooltipEx("Open reshade.ini (ReShade config) in the default text editor.");
                }
            } else {
                imgui.SetTooltipEx("Open reshade.ini (ReShade config) in the default text editor.");
            }
        }
    }

    imgui.EndGroup();
}

void DrawAdhdMultiMonitorControls(display_commander::ui::IImGuiWrapper& imgui) {
    CALL_GUARD_NO_TS();
    // Black curtain (game display) is shown even with one monitor; other displays only when multiple monitors
    bool hasMultipleMonitors = adhd_multi_monitor::api::HasMultipleMonitors();

    imgui.BeginGroup();
    // Use CheckboxSetting so the checkbox always reflects the current setting (e.g. when toggled via hotkey)
    if (CheckboxSetting(settings::g_mainTabSettings.adhd_single_monitor_enabled_for_game_display,
                        "Black curtain (game display)", imgui)) {
        LogInfo("Black curtain (game display) %s",
                settings::g_mainTabSettings.adhd_single_monitor_enabled_for_game_display.GetValue() ? "enabled"
                                                                                                    : "disabled");
    }

    if (hasMultipleMonitors) {
        imgui.SameLine();
        if (CheckboxSetting(settings::g_mainTabSettings.adhd_multi_monitor_enabled, "Black curtain (other displays)",
                            imgui)) {
            LogInfo("Black curtain (other displays) %s",
                    settings::g_mainTabSettings.adhd_multi_monitor_enabled.GetValue() ? "enabled" : "disabled");
        }
    }
    imgui.Unindent();
    imgui.EndGroup();
    if (imgui.IsItemHovered()) {
        adhd_multi_monitor::BackgroundWindowDebugInfo info = {};
        adhd_multi_monitor::api::GetBackgroundWindowDebugInfo(&info);
        char buf[384];
        int n = std::snprintf(buf, sizeof(buf),
                              "Black curtain (game display): black window on the game's monitor.\n"
                              "Black curtain (other displays): covers all other monitors.\n\n"
                              "Background window: HWND %p, %s\n"
                              "Position: (%d, %d), Size: %d x %d\n"
                              "Visible: %s",
                              info.hwnd, info.not_null ? "not null" : "null", info.left, info.top, info.width,
                              info.height, info.is_visible ? "yes" : "no");
        if (n > 0 && n < static_cast<int>(sizeof(buf))) {
            imgui.SetTooltipEx("%s", buf);
        } else {
            imgui.SetTooltipEx(
                "Black curtain (game display): black window on the game's monitor.\n"
                "Black curtain (other displays): covers all other monitors.");
        }
    }
}

}  // namespace ui::new_ui
