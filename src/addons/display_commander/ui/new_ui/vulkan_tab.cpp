#include "vulkan_tab.hpp"
#include "../../hooks/pclstats_etw_hooks.hpp"
#include "../../hooks/vulkan/nvlowlatencyvk_hooks.hpp"
#include "../../hooks/vulkan/vulkan_loader_hooks.hpp"
#include "../../res/forkawesome.h"
#include "../../res/ui_colors.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "settings_wrapper.hpp"

#include <windows.h>
#include <reshade_imgui.hpp>

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

void DrawVulkanTab(reshade::api::effect_runtime* runtime) {
    (void)runtime;

    ImGui::TextColored(ui::colors::ICON_WARNING, ICON_FK_WARNING " Vulkan Reflex & frame pacing (experimental)");
    ImGui::TextColored(ui::colors::TEXT_DIMMED,
                       "Hook Vulkan Reflex APIs to inject FPS limiter and native frame pacing, similar to D3D NVAPI.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Hook status ---
    if (ImGui::CollapsingHeader("Hook status", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();

        const bool nvll_loaded = IsNvLowLatencyVkLoaded();
        const bool vk_loaded = IsVulkanLoaderLoaded();

        ImGui::Text("NvLowLatencyVk.dll:");
        ImGui::SameLine(kVulkanTabValueColumnX);
        if (nvll_loaded) {
            if (AreNvLowLatencyVkHooksInstalled()) {
                ImGui::TextColored(ui::colors::ICON_POSITIVE, "Loaded (hooks active)");
            } else {
                ImGui::TextColored(ui::colors::ICON_POSITIVE, "Loaded");
                ImGui::SameLine();
                ImGui::TextColored(ui::colors::TEXT_DIMMED, "(hooks not installed)");
            }
        } else {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "Not loaded");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Game has not loaded NvLowLatencyVk.dll. Common for non-Vulkan or non-Reflex Vulkan games.");
            }
        }

        ImGui::Text("vulkan-1.dll (loader):");
        ImGui::SameLine(kVulkanTabValueColumnX);
        if (vk_loaded) {
            if (AreVulkanLoaderHooksInstalled()) {
                ImGui::TextColored(ui::colors::ICON_POSITIVE, "Loaded (VK_NV_low_latency2 hooks active)");
            } else {
                ImGui::TextColored(ui::colors::ICON_POSITIVE, "Loaded");
                ImGui::SameLine();
                ImGui::TextColored(ui::colors::TEXT_DIMMED, "(hooks not installed)");
            }
        } else {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "Not loaded");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Vulkan loader not present. This process is likely not a Vulkan application.");
            }
        }

        ImGui::Unindent();
        ImGui::Spacing();
    }

    // --- Controls ---
    if (ImGui::CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();

        if (CheckboxSetting(settings::g_mainTabSettings.vulkan_nvll_hooks_enabled, "Enable NvLowLatencyVk hooks")) {
            if (settings::g_mainTabSettings.vulkan_nvll_hooks_enabled.GetValue() && IsNvLowLatencyVkLoaded()
                && !AreNvLowLatencyVkHooksInstalled()) {
                InstallNvLowLatencyVkHooks(GetModuleHandleW(L"NvLowLatencyVk.dll"));
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "When enabled, hooks NvLL_VK_SetLatencyMarker, NvLL_VK_Sleep, NvLL_VK_SetSleepMode for frame pacing. "
                "Install on next NvLowLatencyVk.dll load, or now if already loaded.");
        }

        if (CheckboxSetting(settings::g_mainTabSettings.vulkan_vk_loader_hooks_enabled,
                            "Enable vulkan-1 loader hooks (VK_NV_low_latency2)")) {
            if (settings::g_mainTabSettings.vulkan_vk_loader_hooks_enabled.GetValue() && IsVulkanLoaderLoaded()
                && !AreVulkanLoaderHooksInstalled()) {
                InstallVulkanLoaderHooks(GetModuleHandleW(L"vulkan-1.dll"));
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "When enabled, hooks vkGetDeviceProcAddr and wraps vkSetLatencyMarkerNV for frame pacing. Install on "
                "next vulkan-1.dll load, or now if already loaded.");
        }

        if (CheckboxSetting(settings::g_mainTabSettings.vulkan_append_reflex_extensions,
                            "Append Reflex extensions in vkCreateDevice")) {
            // Setting persisted by CheckboxSetting
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "When enabled, appends VK_NV_low_latency2, VK_KHR_present_id, and VK_KHR_timeline_semaphore to the "
                "device extension list in vkCreateDevice (same as Special K). If creation fails, falls back to the "
                "original list. Requires vulkan-1 loader hooks to be installed.");
        }

        ImGui::Unindent();
        ImGui::Spacing();
    }

    // --- Enabled extensions (from vkCreateDevice) ---
    if (ImGui::CollapsingHeader("Enabled extensions", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        std::vector<std::string> exts;
        GetVulkanEnabledExtensions(exts);
        if (exts.empty()) {
            ImGui::TextColored(ui::colors::TEXT_DIMMED,
                               "No data. Enable vulkan-1 loader hooks and let the game create a Vulkan device.");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Extensions are captured when vkCreateDevice is called (via hooked vkGetInstanceProcAddr).");
            }
        } else {
            ImGui::Text("Device extension count: %zu", exts.size());
            if (ImGui::BeginChild("VulkanExtensionsList", ImVec2(-1.0f, 120.0f), true)) {
                for (const std::string& name : exts) {
                    ImGui::TextUnformatted(name.c_str());
                }
            }
            ImGui::EndChild();
        }
        ImGui::Unindent();
        ImGui::Spacing();
    }

    // --- Debug ---
    if (ImGui::CollapsingHeader("Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();

        const bool nvll_active = AreNvLowLatencyVkHooksInstalled();
        const bool loader_active = AreVulkanLoaderHooksInstalled();
        const bool pacing_active = nvll_active || loader_active;

        ImGui::Text("Frame pacing active:");
        ImGui::SameLine(kVulkanTabValueColumnX);
        if (pacing_active) {
            ImGui::TextColored(ui::colors::ICON_SUCCESS, "Yes");
        } else {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "No");
        }

        // Which path is active (helps e.g. Doom = VK_NV_low_latency2 only)
        ImGui::Text("Active path:");
        ImGui::SameLine(kVulkanTabValueColumnX);
        if (nvll_active && loader_active) {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "NvLL + VK_NV_low_latency2");
        } else if (loader_active) {
            ImGui::TextColored(ui::colors::ICON_SUCCESS, "VK_NV_low_latency2");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Game uses vulkan-1 vkSetLatencyMarkerNV (e.g. Doom).");
            }
        } else if (nvll_active) {
            ImGui::TextColored(ui::colors::ICON_SUCCESS, "NvLowLatencyVk");
        } else {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "None");
        }

        // --- Detour call counts ---
        ImGui::Spacing();
        ImGui::TextColored(ui::colors::TEXT_SUBTLE, "Detour call counts");
        ImGui::Separator();

        if (nvll_active) {
            uint64_t nvll_init = 0, nvll_marker = 0, nvll_sleep_mode = 0, nvll_sleep = 0;
            GetNvLowLatencyVkDetourCallCounts(&nvll_init, &nvll_marker, &nvll_sleep_mode, &nvll_sleep);
            ImGui::Text("NvLL InitLowLatencyDevice:");
            ImGui::SameLine(kVulkanTabValueColumnX);
            ImGui::Text("%llu", static_cast<unsigned long long>(nvll_init));
            ImGui::Text("NvLL SetLatencyMarker:");
            ImGui::SameLine(kVulkanTabValueColumnX);
            ImGui::Text("%llu", static_cast<unsigned long long>(nvll_marker));
            ImGui::Text("NvLL SetSleepMode:");
            ImGui::SameLine(kVulkanTabValueColumnX);
            ImGui::Text("%llu", static_cast<unsigned long long>(nvll_sleep_mode));
            ImGui::Text("NvLL Sleep:");
            ImGui::SameLine(kVulkanTabValueColumnX);
            ImGui::Text("%llu", static_cast<unsigned long long>(nvll_sleep));
        }

        if (loader_active) {
            uint64_t loader_marker_count = 0;
            uint64_t loader_intercept = 0;
            GetVulkanLoaderDebugState(&loader_marker_count, nullptr, nullptr, &loader_intercept);
            ImGui::Text("vkGetDeviceProcAddr(\"vkSetLatencyMarkerNV\") intercepts:");
            // ImGui::SameLine(kVulkanTabValueColumnX);
            ImGui::SameLine();
            ImGui::Text("%llu", static_cast<unsigned long long>(loader_intercept));
            ImGui::Text("vkSetLatencyMarkerNV (wrapper) calls:");
            // ImGui::SameLine(kVulkanTabValueColumnX);
            ImGui::SameLine();
            ImGui::Text("%llu", static_cast<unsigned long long>(loader_marker_count));

            uint64_t dummy_sleep_mode = 0, dummy_sleep = 0, dummy_marker = 0, dummy_timings = 0;
            GetVulkanLoaderDummyCallCounts(&dummy_sleep_mode, &dummy_sleep, &dummy_marker, &dummy_timings);
            if (dummy_sleep_mode > 0 || dummy_sleep > 0 || dummy_marker > 0 || dummy_timings > 0) {
                ImGui::TextColored(ui::colors::TEXT_SUBTLE, "Dummy procs (loader returned null):");
                ImGui::SameLine(kVulkanTabValueColumnX);
                ImGui::Text("SetSleepMode:%llu Sleep:%llu SetLatencyMarker:%llu GetLatencyTimings:%llu",
                            static_cast<unsigned long long>(dummy_sleep_mode),
                            static_cast<unsigned long long>(dummy_sleep), static_cast<unsigned long long>(dummy_marker),
                            static_cast<unsigned long long>(dummy_timings));
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Game called these although vkGetDeviceProcAddr returned null; we returned dummies to "
                        "observe.");
                }
            }
        }

        // PCLStats ETW (game + Display Commander) – counts from EventWriteTransfer hook
        if (ArePCLStatsEtwHooksInstalled()) {
            uint64_t pcl_event = 0, pcl_v2 = 0, pcl_v3 = 0;
            GetPCLStatsEtwCounts(&pcl_event, &pcl_v2, &pcl_v3);
            ImGui::Text("PCLStats ETW (game+DC):");
            ImGui::SameLine(kVulkanTabValueColumnX);
            ImGui::Text("PCLStatsEvent: %llu  V2: %llu  V3: %llu", static_cast<unsigned long long>(pcl_event),
                        static_cast<unsigned long long>(pcl_v2), static_cast<unsigned long long>(pcl_v3));
            uint64_t by_marker[kPCLStatsMarkerTypeCount] = {};
            GetPCLStatsEtwCountsByMarker(by_marker);
            if (ImGui::CollapsingHeader("PCLStats ETW by marker type", ImGuiTreeNodeFlags_None)) {
                ImGui::Indent();
                for (size_t i = 0; i < kPCLStatsMarkerTypeCount; ++i) {
                    if (by_marker[i] == 0) continue;
                    ImGui::Text("%zu %s:", i, GetPCLStatsMarkerTypeName(i));
                    ImGui::SameLine(kVulkanTabValueColumnX);
                    ImGui::Text("%llu", static_cast<unsigned long long>(by_marker[i]));
                }
                ImGui::Unindent();
            }
            if (ImGui::SmallButton("Reset PCLStats ETW counts")) {
                ResetPCLStatsEtwCounts();
            }
        }

        ImGui::Spacing();
        ImGui::TextColored(ui::colors::TEXT_SUBTLE, "Last marker / frame");
        ImGui::Separator();

        // NvLowLatencyVk path
        uint64_t marker_count = 0;
        int last_marker_type = -1;
        uint64_t last_frame_id = 0;
        GetNvLowLatencyVkDebugState(&marker_count, &last_marker_type, &last_frame_id);
        ImGui::Text("NvLL last marker / frame ID:");
        ImGui::SameLine(kVulkanTabValueColumnX);
        if (last_marker_type >= 0 || last_frame_id > 0) {
            ImGui::Text("%d / %llu", last_marker_type, static_cast<unsigned long long>(last_frame_id));
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("0=SIMULATION_START, 4=PRESENT_START, 5=PRESENT_END, ...");
            }
        } else {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "-");
        }

        // VK_NV_low_latency2 path (always show when loader hooks installed; primary for Doom etc.)
        if (loader_active) {
            int loader_last_marker = -1;
            uint64_t loader_last_present_id = 0;
            GetVulkanLoaderDebugState(nullptr, &loader_last_marker, &loader_last_present_id, nullptr);
            ImGui::Text("VK_NV_low_latency2 last marker / presentID:");
            ImGui::SameLine(kVulkanTabValueColumnX);
            if (loader_last_marker >= 0 || loader_last_present_id > 0) {
                ImGui::Text("%d / %llu", loader_last_marker, static_cast<unsigned long long>(loader_last_present_id));
            } else {
                ImGui::TextColored(ui::colors::TEXT_DIMMED, "-");
            }
        }

        ImGui::Unindent();
        ImGui::Spacing();
    }

    ImGui::Separator();
    ImGui::TextColored(ui::colors::TEXT_SUBTLE,
                       "See doc/tasks/vulkan_reflex_frame_pacing_plan.md for the implementation plan.");
}

}  // namespace ui::new_ui
