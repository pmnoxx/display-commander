#include "vulkan_tab.hpp"
#include "../../hooks/pclstats_etw_hooks.hpp"
#include "../../hooks/vulkan/nvlowlatencyvk_hooks.hpp"
#include "../../hooks/vulkan/vulkan_loader_hooks.hpp"
#include "../../res/forkawesome.h"
#include "../../res/ui_colors.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../imgui_wrapper_base.hpp"
#include "settings_wrapper.hpp"

#include <imgui.h>
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ui::new_ui {

namespace {

// Value column X so labels (e.g. "VK_NV_low_latency2 last marker / presentID:") don't overlap values
constexpr float kVulkanTabValueColumnX = 380.0f;

// Check if NvLowLatencyVk.dll is loaded in the process (for status display)
bool IsNvLowLatencyVkLoaded() {
    HMODULE h = GetModuleHandleW(L"NvLowLatencyVk.dll");
    return (h != nullptr);
}

// Check if vulkan-1.dll (loader) is loaded
bool IsVulkanLoaderLoaded() {
    HMODULE h = GetModuleHandleW(L"vulkan-1.dll");
    return (h != nullptr);
}

}  // namespace

void InitVulkanTab() {
    // Reserved for future Vulkan Reflex hook initialization (e.g. when hooks are installed).
}

void DrawVulkanTab(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.TextColored(ui::colors::ICON_WARNING, ICON_FK_WARNING " Vulkan Reflex & frame pacing (experimental)");
    imgui.TextColored(ui::colors::TEXT_DIMMED,
                      "Hook Vulkan Reflex APIs to inject FPS limiter and native frame pacing, similar to D3D NVAPI.");
    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();

    // --- Hook status ---
    if (imgui.CollapsingHeader("Hook status", ImGuiTreeNodeFlags_DefaultOpen)) {
        imgui.Indent();

        const bool nvll_loaded = IsNvLowLatencyVkLoaded();
        const bool vk_loaded = IsVulkanLoaderLoaded();

        imgui.Text("NvLowLatencyVk.dll:");
        imgui.SameLine(kVulkanTabValueColumnX);
        if (nvll_loaded) {
            if (AreNvLowLatencyVkHooksInstalled()) {
                imgui.TextColored(ui::colors::ICON_POSITIVE, "Loaded (hooks active)");
            } else {
                imgui.TextColored(ui::colors::ICON_POSITIVE, "Loaded");
                imgui.SameLine();
                imgui.TextColored(ui::colors::TEXT_DIMMED, "(hooks not installed)");
            }
        } else {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "Not loaded");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "Game has not loaded NvLowLatencyVk.dll. Common for non-Vulkan or non-Reflex Vulkan games.");
            }
        }

        imgui.Text("vulkan-1.dll (loader):");
        imgui.SameLine(kVulkanTabValueColumnX);
        if (vk_loaded) {
            if (AreVulkanLoaderHooksInstalled()) {
                imgui.TextColored(ui::colors::ICON_POSITIVE, "Loaded (VK_NV_low_latency2 hooks active)");
            } else {
                imgui.TextColored(ui::colors::ICON_POSITIVE, "Loaded");
                imgui.SameLine();
                imgui.TextColored(ui::colors::TEXT_DIMMED, "(hooks not installed)");
            }
        } else {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "Not loaded");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Vulkan loader not present. This process is likely not a Vulkan application.");
            }
        }

        imgui.Unindent();
        imgui.Spacing();
    }

    // --- Controls ---
    if (imgui.CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        imgui.Indent();

        if (CheckboxSetting(settings::g_mainTabSettings.vulkan_nvll_hooks_enabled, "Enable NvLowLatencyVk hooks",
                            imgui)) {
            if (settings::g_mainTabSettings.vulkan_nvll_hooks_enabled.GetValue() && IsNvLowLatencyVkLoaded()
                && !AreNvLowLatencyVkHooksInstalled()) {
                InstallNvLowLatencyVkHooks(GetModuleHandleW(L"NvLowLatencyVk.dll"));
            }
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "When enabled, hooks NvLL_VK_SetLatencyMarker, NvLL_VK_Sleep, NvLL_VK_SetSleepMode for frame pacing. "
                "Install on next NvLowLatencyVk.dll load, or now if already loaded.");
        }

        if (CheckboxSetting(settings::g_mainTabSettings.vulkan_vk_loader_hooks_enabled,
                            "Enable vulkan-1 loader hooks (VK_NV_low_latency2)", imgui)) {
            if (settings::g_mainTabSettings.vulkan_vk_loader_hooks_enabled.GetValue() && IsVulkanLoaderLoaded()
                && !AreVulkanLoaderHooksInstalled()) {
                InstallVulkanLoaderHooks(GetModuleHandleW(L"vulkan-1.dll"));
            }
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "When enabled, hooks vkGetDeviceProcAddr and wraps vkSetLatencyMarkerNV for frame pacing. Install on "
                "next vulkan-1.dll load, or now if already loaded.");
        }

        if (CheckboxSetting(settings::g_mainTabSettings.vulkan_append_reflex_extensions,
                            "Append Reflex extensions in vkCreateDevice", imgui)) {
            // Setting persisted by CheckboxSetting
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "When enabled, appends VK_NV_low_latency2, VK_KHR_present_id, and VK_KHR_timeline_semaphore to the "
                "device extension list in vkCreateDevice (same as Special K). If creation fails, falls back to the "
                "original list. Requires vulkan-1 loader hooks to be installed.");
        }

        imgui.Unindent();
        imgui.Spacing();
    }

    // --- Enabled extensions (from vkCreateDevice) ---
    if (imgui.CollapsingHeader("Enabled extensions", ImGuiTreeNodeFlags_DefaultOpen)) {
        imgui.Indent();
        std::vector<std::string> exts;
        GetVulkanEnabledExtensions(exts);
        if (exts.empty()) {
            const bool loader_hooks = AreVulkanLoaderHooksInstalled();
            const bool create_device_called = HasVulkanCreateDeviceBeenCalled();
            const char* reason = nullptr;
            if (!loader_hooks) {
                reason = "Enable vulkan-1 loader hooks and let the game create a Vulkan device.";
            } else if (!create_device_called) {
                reason = "vkCreateDevice has not been called yet (game has not created a Vulkan device).";
            } else {
                reason = "vkCreateDevice was called but no enabled extensions were reported.";
            }
            imgui.TextColored(ui::colors::TEXT_DIMMED, "No data. %s", reason);
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "Extensions are captured when vkCreateDevice is called (via hooked vkGetInstanceProcAddr).");
            }
        } else {
            imgui.Text("Device extension count: %zu", exts.size());
            if (imgui.BeginChild("VulkanExtensionsList", ImVec2(-1.0f, 120.0f), true)) {
                for (const std::string& name : exts) {
                    imgui.TextUnformatted(name.c_str());
                }
            }
            imgui.EndChild();
        }
        imgui.Unindent();
        imgui.Spacing();
    }

    // --- Debug ---
    if (imgui.CollapsingHeader("Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
        imgui.Indent();

        const bool nvll_active = AreNvLowLatencyVkHooksInstalled();
        const bool loader_active = AreVulkanLoaderHooksInstalled();
        const bool pacing_active = nvll_active || loader_active;

        imgui.Text("Frame pacing active:");
        imgui.SameLine(kVulkanTabValueColumnX);
        if (pacing_active) {
            imgui.TextColored(ui::colors::ICON_SUCCESS, "Yes");
        } else {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "No");
        }

        // Which path is active (helps e.g. Doom = VK_NV_low_latency2 only)
        imgui.Text("Active path:");
        imgui.SameLine(kVulkanTabValueColumnX);
        if (nvll_active && loader_active) {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "NvLL + VK_NV_low_latency2");
        } else if (loader_active) {
            imgui.TextColored(ui::colors::ICON_SUCCESS, "VK_NV_low_latency2");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Game uses vulkan-1 vkSetLatencyMarkerNV (e.g. Doom).");
            }
        } else if (nvll_active) {
            imgui.TextColored(ui::colors::ICON_SUCCESS, "NvLowLatencyVk");
        } else {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "None");
        }

        // --- Detour call counts ---
        imgui.Spacing();
        imgui.TextColored(ui::colors::TEXT_SUBTLE, "Detour call counts");
        imgui.Separator();

        if (nvll_active) {
            std::uint64_t nvll_init = 0, nvll_marker = 0, nvll_sleep_mode = 0, nvll_sleep = 0;
            GetNvLowLatencyVkDetourCallCounts(&nvll_init, &nvll_marker, &nvll_sleep_mode, &nvll_sleep);
            imgui.Text("NvLL InitLowLatencyDevice:");
            imgui.SameLine(kVulkanTabValueColumnX);
            imgui.Text("%llu", static_cast<std::uint64_t>(nvll_init));
            imgui.Text("NvLL SetLatencyMarker:");
            imgui.SameLine(kVulkanTabValueColumnX);
            imgui.Text("%llu", static_cast<std::uint64_t>(nvll_marker));
            imgui.Text("NvLL SetSleepMode:");
            imgui.SameLine(kVulkanTabValueColumnX);
            imgui.Text("%llu", static_cast<std::uint64_t>(nvll_sleep_mode));
            imgui.Text("NvLL Sleep:");
            imgui.SameLine(kVulkanTabValueColumnX);
            imgui.Text("%llu", static_cast<std::uint64_t>(nvll_sleep));
        }

        if (loader_active) {
            std::uint64_t calls_get_instance = 0, calls_get_device = 0, calls_create_device = 0,
                          calls_set_latency_marker = 0;
            GetVulkanLoaderCallCounts(&calls_get_instance, &calls_get_device, &calls_create_device,
                                      &calls_set_latency_marker);
            imgui.Text("vkGetInstanceProcAddr:");
            imgui.SameLine(kVulkanTabValueColumnX);
            imgui.Text("%llu", static_cast<std::uint64_t>(calls_get_instance));
            imgui.Text("vkGetDeviceProcAddr:");
            imgui.SameLine(kVulkanTabValueColumnX);
            imgui.Text("%llu", static_cast<std::uint64_t>(calls_get_device));
            imgui.Text("vkCreateDevice:");
            imgui.SameLine(kVulkanTabValueColumnX);
            imgui.Text("%llu", static_cast<std::uint64_t>(calls_create_device));
            imgui.Text("vkSetLatencyMarkerNV:");
            imgui.SameLine(kVulkanTabValueColumnX);
            imgui.Text("%llu", static_cast<std::uint64_t>(calls_set_latency_marker));
        }

        // PCLStats ETW (game + Display Commander) – counts from EventWriteTransfer hook
        if (ArePCLStatsEtwHooksInstalled()) {
            std::uint64_t pcl_event = 0, pcl_v2 = 0, pcl_v3 = 0;
            GetPCLStatsEtwCounts(&pcl_event, &pcl_v2, &pcl_v3);
            imgui.Text("PCLStats ETW (game+DC):");
            imgui.SameLine(kVulkanTabValueColumnX);
            imgui.Text("PCLStatsEvent: %llu  V2: %llu  V3: %llu", static_cast<std::uint64_t>(pcl_event),
                       static_cast<std::uint64_t>(pcl_v2), static_cast<std::uint64_t>(pcl_v3));
            std::uint64_t by_marker[kPCLStatsMarkerTypeCount] = {};
            GetPCLStatsEtwCountsByMarker(by_marker);
            if (imgui.CollapsingHeader("PCLStats ETW by marker type", ImGuiTreeNodeFlags_None)) {
                imgui.Indent();
                for (size_t i = 0; i < kPCLStatsMarkerTypeCount; ++i) {
                    if (by_marker[i] == 0) continue;
                    imgui.Text("%zu %s:", i, GetPCLStatsMarkerTypeName(i));
                    imgui.SameLine(kVulkanTabValueColumnX);
                    imgui.Text("%llu", static_cast<std::uint64_t>(by_marker[i]));
                }
                imgui.Unindent();
            }
            if (imgui.SmallButton("Reset PCLStats ETW counts")) {
                ResetPCLStatsEtwCounts();
            }
        }

        imgui.Spacing();
        imgui.TextColored(ui::colors::TEXT_SUBTLE, "Last marker / frame");
        imgui.Separator();

        // NvLowLatencyVk path
        std::uint64_t marker_count = 0;
        int last_marker_type = -1;
        std::uint64_t last_frame_id = 0;
        GetNvLowLatencyVkDebugState(&marker_count, &last_marker_type, &last_frame_id);
        imgui.Text("NvLL last marker / frame ID:");
        imgui.SameLine(kVulkanTabValueColumnX);
        if (last_marker_type >= 0 || last_frame_id > 0) {
            imgui.Text("%d / %llu", last_marker_type, static_cast<std::uint64_t>(last_frame_id));
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("0=SIMULATION_START, 4=PRESENT_START, 5=PRESENT_END, ...");
            }
        } else {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "-");
        }

        // VK_NV_low_latency2 path (always show when loader hooks installed; primary for Doom etc.)
        if (loader_active) {
            int loader_last_marker = -1;
            std::uint64_t loader_last_present_id = 0;
            GetVulkanLoaderDebugState(nullptr, &loader_last_marker, &loader_last_present_id, nullptr);
            imgui.Text("VK_NV_low_latency2 last marker / presentID:");
            imgui.SameLine(kVulkanTabValueColumnX);
            if (loader_last_marker >= 0 || loader_last_present_id > 0) {
                imgui.Text("%d / %llu", loader_last_marker, static_cast<std::uint64_t>(loader_last_present_id));
            } else {
                imgui.TextColored(ui::colors::TEXT_DIMMED, "-");
            }
        }

        imgui.Unindent();
        imgui.Spacing();
    }

    imgui.Separator();
    imgui.TextColored(ui::colors::TEXT_SUBTLE,
                      "See doc/tasks/vulkan_reflex_frame_pacing_plan.md for the implementation plan.");
}

}  // namespace ui::new_ui
