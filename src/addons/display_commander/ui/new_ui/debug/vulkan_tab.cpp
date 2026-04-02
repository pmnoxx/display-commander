// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "vulkan_tab.hpp"
#include "../../../hooks/vulkan/vulkan_loader_hooks.hpp"

// Libraries <ReShade> / <imgui>
#include <imgui.h>

// Libraries <standard C++>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ui::new_ui::debug {

namespace {

constexpr std::size_t kHookCount = static_cast<std::size_t>(VulkanLoaderHook::Count);

}  // namespace

void DrawVulkanTab(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Text("Vulkan debug (loader hooks)");

    const bool hooks_installed = AreVulkanLoaderHooksInstalled();
    imgui.Text("Loader hooks installed: %s", hooks_installed ? "YES" : "NO");

    uint64_t marker_count = 0;
    int last_marker_type = -1;
    uint64_t last_present_id = 0;
    uint64_t fse_acquire_spoof_calls = 0;
    GetVulkanLoaderDebugState(&marker_count, &last_marker_type, &last_present_id, &fse_acquire_spoof_calls);

    imgui.Separator();
    imgui.Text("vkSetLatencyMarkerNV calls: %llu", static_cast<unsigned long long>(marker_count));
    imgui.Text("Last marker type: %d", last_marker_type);
    imgui.Text("Last present ID: %llu", static_cast<unsigned long long>(last_present_id));

    imgui.Separator();
    imgui.Text("FSE acquire spoof calls (vkAcquireFullScreenExclusiveModeEXT): %llu",
               static_cast<unsigned long long>(fse_acquire_spoof_calls));

    imgui.Separator();
    std::array<uint64_t, kHookCount> counts{};
    GetVulkanLoaderHookCallCounts(counts.data(), counts.size());

    imgui.Text("Hook call counts:");
    for (std::size_t i = 0; i < counts.size(); ++i) {
        const auto hook = static_cast<VulkanLoaderHook>(i);
        const char* name = GetVulkanLoaderHookName(hook);
        imgui.Text(" - %s: %llu", (name != nullptr) ? name : "(null)",
                   static_cast<unsigned long long>(counts[i]));
    }

    imgui.Separator();
    const bool create_device_called = HasVulkanCreateDeviceBeenCalled();
    imgui.Text("vkCreateDevice observed: %s", create_device_called ? "YES" : "NO");

    std::vector<std::string> exts;
    GetVulkanEnabledExtensions(exts);
    imgui.Text("Enabled device extensions captured: %d", static_cast<int>(exts.size()));
    if (!exts.empty()) {
        for (const std::string& s : exts) {
            imgui.Text(" - %s", s.c_str());
        }
    }
}

}  // namespace ui::new_ui::debug

