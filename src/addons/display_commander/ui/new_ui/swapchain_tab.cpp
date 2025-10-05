#include "swapchain_tab.hpp"
#include "../../globals.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../swapchain_events_power_saving.hpp"

#include <dxgi1_6.h>
#include <imgui.h>
#include <wrl/client.h>

namespace ui::new_ui {

void DrawSwapchainTab() {
    ImGui::Text("Swapchain Tab - DXGI Information");
    ImGui::Separator();

    // Draw all swapchain-related sections
    DrawSwapchainEventCounters();
    ImGui::Spacing();
    DrawDxgiCompositionInfo();
}

void DrawSwapchainEventCounters() {
    if (ImGui::CollapsingHeader("Swapchain Event Counters", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Event Counters (Green = Working, Red = Not Working)");
        ImGui::Separator();


        // Display each event counter with color coding
        static const char* event_names[NUM_EVENTS] = {
            "SWAPCHAIN_EVENT_BEGIN_RENDER_PASS (0)",
            "SWAPCHAIN_EVENT_END_RENDER_PASS (1)",
            "SWAPCHAIN_EVENT_CREATE_SWAPCHAIN_CAPTURE (2)",
            "SWAPCHAIN_EVENT_INIT_SWAPCHAIN (3)",
            "SWAPCHAIN_EVENT_PRESENT_UPDATE_AFTER (4)",
            "SWAPCHAIN_EVENT_PRESENT_UPDATE_BEFORE (5)",
            "SWAPCHAIN_EVENT_PRESENT_UPDATE_BEFORE2 (6)",
            "SWAPCHAIN_EVENT_INIT_COMMAND_LIST (7)",
            "SWAPCHAIN_EVENT_EXECUTE_COMMAND_LIST (8)",
            "SWAPCHAIN_EVENT_BIND_PIPELINE (9)",
            "SWAPCHAIN_EVENT_INIT_COMMAND_QUEUE (10)",
            "SWAPCHAIN_EVENT_RESET_COMMAND_LIST (11)",
            "SWAPCHAIN_EVENT_PRESENT_FLAGS (12)",
            "SWAPCHAIN_EVENT_DRAW (13)",
            "SWAPCHAIN_EVENT_DRAW_INDEXED (14)",
            "SWAPCHAIN_EVENT_DRAW_OR_DISPATCH_INDIRECT (15)",
            // New power saving events
            "SWAPCHAIN_EVENT_DISPATCH (16)",
            "SWAPCHAIN_EVENT_DISPATCH_MESH (17)",
            "SWAPCHAIN_EVENT_DISPATCH_RAYS (18)",
            "SWAPCHAIN_EVENT_COPY_RESOURCE (19)",
            "SWAPCHAIN_EVENT_UPDATE_BUFFER_REGION (20)",
            "SWAPCHAIN_EVENT_UPDATE_BUFFER_REGION_COMMAND (21)",
            "SWAPCHAIN_EVENT_BIND_RESOURCE (22)",
            "SWAPCHAIN_EVENT_MAP_RESOURCE (23)",
            // Additional frame-specific GPU operations for power saving
            "SWAPCHAIN_EVENT_COPY_BUFFER_REGION (24)",
            "SWAPCHAIN_EVENT_COPY_BUFFER_TO_TEXTURE (25)",
            "SWAPCHAIN_EVENT_COPY_TEXTURE_TO_BUFFER (26)",
            "SWAPCHAIN_EVENT_COPY_TEXTURE_REGION (27)",
            "SWAPCHAIN_EVENT_RESOLVE_TEXTURE_REGION (28)",
            "SWAPCHAIN_EVENT_CLEAR_RENDER_TARGET_VIEW (29)",
            "SWAPCHAIN_EVENT_CLEAR_DEPTH_STENCIL_VIEW (30)",
            "SWAPCHAIN_EVENT_CLEAR_UNORDERED_ACCESS_VIEW_UINT (31)",
            "SWAPCHAIN_EVENT_CLEAR_UNORDERED_ACCESS_VIEW_FLOAT (32)",
            "SWAPCHAIN_EVENT_GENERATE_MIPMAPS (33)",
            "SWAPCHAIN_EVENT_BLIT (34)",
            "SWAPCHAIN_EVENT_BEGIN_QUERY (35)",
            "SWAPCHAIN_EVENT_END_QUERY (36)",
            "SWAPCHAIN_EVENT_RESOLVE_QUERY_DATA (37)",
            // DXGI Present hooks
            "SWAPCHAIN_EVENT_DXGI_PRESENT (38)",
            "SWAPCHAIN_EVENT_DXGI_GETBUFFER (39)",
            "SWAPCHAIN_EVENT_DXGI_SETFULLSCREENSTATE (40)",
            "SWAPCHAIN_EVENT_DXGI_GETFULLSCREENSTATE (41)",
            "SWAPCHAIN_EVENT_DXGI_GETDESC (42)",
            "SWAPCHAIN_EVENT_DXGI_RESIZEBUFFERS (43)",
            "SWAPCHAIN_EVENT_DXGI_RESIZETARGET (44)",
            "SWAPCHAIN_EVENT_DXGI_GETCONTAININGOUTPUT (45)",
            "SWAPCHAIN_EVENT_DXGI_GETFRAMESTATISTICS (46)",
            "SWAPCHAIN_EVENT_DXGI_GETLASTPRESENTCOUNT (47)",
            "SWAPCHAIN_EVENT_DXGI_GETDESC1 (48)",
            "SWAPCHAIN_EVENT_DXGI_GETFULLSCREENDESC (49)",
            "SWAPCHAIN_EVENT_DXGI_GETHWND (50)",
            "SWAPCHAIN_EVENT_DXGI_GETCOREWINDOW (51)",
            "SWAPCHAIN_EVENT_DXGI_PRESENT1 (52)",
            "SWAPCHAIN_EVENT_DXGI_ISTEMPORARYMONOSUPPORTED (53)",
            "SWAPCHAIN_EVENT_DXGI_GETRESTRICTTOOUTPUT (54)",
            "SWAPCHAIN_EVENT_DXGI_SETBACKGROUNDCOLOR (55)",
            "SWAPCHAIN_EVENT_DXGI_GETBACKGROUNDCOLOR (56)",
            "SWAPCHAIN_EVENT_DXGI_SETROTATION (57)",
            "SWAPCHAIN_EVENT_DXGI_GETROTATION (58)",
            "SWAPCHAIN_EVENT_DXGI_SETSOURCESIZE (59)",
            "SWAPCHAIN_EVENT_DXGI_GETSOURCESIZE (60)",
            "SWAPCHAIN_EVENT_DXGI_SETMAXIMUMFRAMELATENCY (61)",
            "SWAPCHAIN_EVENT_DXGI_GETMAXIMUMFRAMELATENCY (62)",
            "SWAPCHAIN_EVENT_DXGI_GETFRAMELATENCYWAIABLEOBJECT (63)",
            "SWAPCHAIN_EVENT_DXGI_SETMATRIXTRANSFORM (64)",
            "SWAPCHAIN_EVENT_DXGI_GETMATRIXTRANSFORM (65)",
            "SWAPCHAIN_EVENT_DXGI_GETCURRENTBACKBUFFERINDEX (66)",
            "SWAPCHAIN_EVENT_DXGI_CHECKCOLORSPACESUPPORT (67)",
            "SWAPCHAIN_EVENT_DXGI_SETCOLORSPACE1 (68)",
            "SWAPCHAIN_EVENT_DXGI_RESIZEBUFFERS1 (69)",
            "SWAPCHAIN_EVENT_DXGI_FACTORY_CREATESWAPCHAIN (70)",
            "SWAPCHAIN_EVENT_DXGI_CREATEFACTORY (71)",
            "SWAPCHAIN_EVENT_DXGI_CREATEFACTORY1 (72)",
            "SWAPCHAIN_EVENT_DX9_PRESENT (73)",
            "SWAPCHAIN_EVENT_NVAPI_GET_HDR_CAPABILITIES (74)"
        };

        uint32_t total_events = 0;

        // Group events by category for better organization
        struct EventGroup {
            const char* name;
            int start_idx;
            int end_idx;
            ImVec4 color;
        };

        static EventGroup event_groups[] = {
            {"ReShade Events (0-37)", 0, 37, ImVec4(0.8f, 0.8f, 1.0f, 1.0f)},
            {"DXGI Core Methods (38-47)", 38, 47, ImVec4(0.8f, 1.0f, 0.8f, 1.0f)},
            {"DXGI SwapChain1 Methods (48-58)", 48, 58, ImVec4(1.0f, 0.8f, 0.8f, 1.0f)},
            {"DXGI SwapChain2 Methods (59-65)", 59, 65, ImVec4(1.0f, 1.0f, 0.8f, 1.0f)},
            {"DXGI SwapChain3 Methods (66-69)", 66, 69, ImVec4(0.8f, 1.0f, 1.0f, 1.0f)},
            {"DXGI Factory Methods (70-72)", 70, 72, ImVec4(1.0f, 0.8f, 1.0f, 1.0f)},
            {"DirectX 9 Methods (73)", 73, 73, ImVec4(1.0f, 0.6f, 0.6f, 1.0f)},
            {"NVAPI Methods (74)", 74, 74, ImVec4(0.6f, 1.0f, 0.6f, 1.0f)}
        };

        for (const auto& group : event_groups) {
            if (ImGui::CollapsingHeader(group.name, ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();

                for (int i = group.start_idx; i <= group.end_idx && i < NUM_EVENTS; i++) {
                    uint32_t count = g_swapchain_event_counters[i].load();
                    total_events += count;

                    // Green if > 0, red if 0
                    ImVec4 color = (count > 0) ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
                    ImGui::TextColored(color, "%s: %u", event_names[i], count);
                }

                ImGui::Unindent();
            }
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Total Events: %u", total_events);

        // DXGI-specific summary
        uint32_t dxgi_events = 0;
        uint32_t dxgi_active = 0;
        for (int i = 38; i < NUM_EVENTS; i++) {
            uint32_t count = g_swapchain_event_counters[i].load();
            dxgi_events += count;
            if (count > 0) dxgi_active++;
        }

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "DXGI Hooks Summary:");
        ImGui::Text("  Total DXGI Calls: %u", dxgi_events);
        ImGui::Text("  Active DXGI Methods: %u/33", dxgi_active);

        if (dxgi_active > 0) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "  ✓ DXGI hooks are working correctly");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "  ⚠ No DXGI method calls detected");
        }

        // Show status message
        if (total_events > 0) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Status: Swapchain events are working correctly");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
                               "Status: No swapchain events detected - check if addon is properly loaded");
        }
    }

    // Power Saving Settings Section
    if (ImGui::CollapsingHeader("Power Saving Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "GPU Power Saving Controls");
        ImGui::Separator();

        // Main power saving toggle
        static bool main_power_saving = s_no_render_in_background.load();
        if (ImGui::Checkbox("Enable Power Saving in Background", &main_power_saving)) {
            s_no_render_in_background.store(main_power_saving);
        }

        if (main_power_saving) {
            ImGui::Indent();

            // Compute shader suppression
            static bool suppress_compute = s_suppress_compute_in_background.load();
            if (ImGui::Checkbox("Suppress Compute Shaders (Dispatch)", &suppress_compute)) {
                s_suppress_compute_in_background.store(suppress_compute);
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "?");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Skip compute shader dispatches when app is in background");
            }

            // Resource copy suppression
            static bool suppress_copy = s_suppress_copy_in_background.load();
            if (ImGui::Checkbox("Suppress Resource Copying", &suppress_copy)) {
                s_suppress_copy_in_background.store(suppress_copy);
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "?");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Skip resource copy operations when app is in background");
            }

            // Memory operations suppression
            static bool suppress_memory = s_suppress_memory_ops_in_background.load();
            if (ImGui::Checkbox("Suppress Memory Operations", &suppress_memory)) {
                s_suppress_memory_ops_in_background.store(suppress_memory);
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "?");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Skip resource mapping operations when app is in background");
            }

            // Resource binding suppression (more conservative)
            static bool suppress_binding = s_suppress_binding_in_background.load();
            if (ImGui::Checkbox("Suppress Resource Binding (Experimental)", &suppress_binding)) {
                s_suppress_binding_in_background.store(suppress_binding);
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "⚠");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Skip resource binding operations (may cause rendering issues)");
            }

            ImGui::Unindent();
        }

        // Power saving status
        ImGui::Separator();
        bool is_background = g_app_in_background.load(std::memory_order_acquire);
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Current Status:");
        ImGui::Text("  App in Background: %s", is_background ? "Yes" : "No");
        ImGui::Text("  Power Saving Active: %s", (main_power_saving && is_background) ? "Yes" : "No");

        if (main_power_saving && is_background) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "  ✁EPower saving is currently active");
        }
    }
}


void DrawDxgiCompositionInfo() {
    if (ImGui::CollapsingHeader("DXGI Composition Information", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* mode_str = "Unknown";
        switch (static_cast<int>(s_dxgi_composition_state)) {
            case 1:  mode_str = "Composed Flip"; break;
            case 2:  mode_str = "Modern Independent Flip"; break;
            case 3:  mode_str = "Legacy Independent Flip"; break;
            default: mode_str = "Unknown"; break;
        }

        // Get backbuffer format
        std::string format_str = "Unknown";
        // Get colorspace string
        std::string colorspace_str = "Unknown";
        switch (g_current_colorspace) {
            case reshade::api::color_space::unknown:              colorspace_str = "Unknown"; break;
            case reshade::api::color_space::srgb_nonlinear:       colorspace_str = "sRGB"; break;
            case reshade::api::color_space::extended_srgb_linear: colorspace_str = "Extended sRGB Linear"; break;
            case reshade::api::color_space::hdr10_st2084:         colorspace_str = "HDR10 ST2084"; break;
            case reshade::api::color_space::hdr10_hlg:            colorspace_str = "HDR10 HLG"; break;
            default:                                              colorspace_str = "ColorSpace_" + std::to_string(static_cast<int>(g_current_colorspace)); break;
        }

        ImGui::Text("DXGI Composition: %s", mode_str);
        ImGui::Text("Backbuffer: %dx%d", g_last_backbuffer_width.load(), g_last_backbuffer_height.load());
        ImGui::Text("Format: %s", format_str.c_str());
        ImGui::Text("Colorspace: %s", colorspace_str.c_str());

        // Display HDR10 override status
        ImGui::Text("HDR10 Colorspace Override: %s (Last: %s)", g_hdr10_override_status.load()->c_str(),
                    g_hdr10_override_timestamp.load()->c_str());
    }
}

}  // namespace ui::new_ui
