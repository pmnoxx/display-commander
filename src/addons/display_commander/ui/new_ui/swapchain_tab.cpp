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

        // Event visibility flags - set to false to hide specific events
        static bool event_visibility[] = {
            false,  // reshade::addon_event::begin_render_pass (0 == ok)
            false,  // reshade::addon_event::end_render_pass (0 == ok)
            true,   // reshade::addon_event::create_swapchain (vsync on/off won't work)
            true,   // reshade::addon_event::init_swapchain
            true,   // reshade::addon_event::finish_present
            true,   // reshade::addon_event::present
            true,   // reshade::addon_event::reshade_present
            true,   // reshade::addon_event::init_command_list
            false,  // reshade::addon_event::execute_command_list
            false,  // reshade::addon_event::bind_pipeline (suppressed by default)
            true,   // reshade::addon_event::init_command_queue
            true,   // reshade::addon_event::reset_command_list
            true,   // reshade::addon_event::present_flags
            true,   // reshade::addon_event::draw
            true,   // reshade::addon_event::draw_indexed
            true,   // reshade::addon_event::draw_or_dispatch_indirect
            // New power saving events
            true,  // reshade::addon_event::dispatch
            true,  // reshade::addon_event::dispatch_mesh
            true,  // reshade::addon_event::dispatch_rays
            true,  // reshade::addon_event::copy_resource
            true,  // reshade::addon_event::update_buffer_region
            true,  // reshade::addon_event::update_buffer_region_command
            true,  // reshade::addon_event::bind_resource
            true,  // reshade::addon_event::map_resource
            // Additional frame-specific GPU operations for power saving
            true,  // reshade::addon_event::copy_buffer_region
            true,  // reshade::addon_event::copy_buffer_to_texture
            true,  // reshade::addon_event::copy_texture_to_buffer
            true,  // reshade::addon_event::copy_texture_region
            true,  // reshade::addon_event::resolve_texture_region
            true,  // reshade::addon_event::clear_render_target_view
            true,  // reshade::addon_event::clear_depth_stencil_view
            true,  // reshade::addon_event::clear_unordered_access_view_uint
            true,  // reshade::addon_event::clear_unordered_access_view_float
            true,  // reshade::addon_event::generate_mipmaps
            true,  // reshade::addon_event::blit
            true,  // reshade::addon_event::begin_query
            true,  // reshade::addon_event::end_query
            true   // reshade::addon_event::resolve_query_data
        };

        // Display each event counter with color coding
        static const char* event_names[] = {
            "reshade::addon_event::begin_render_pass (0 == ok)", "reshade::addon_event::end_render_pass (0 == ok)",
            "reshade::addon_event::create_swapchain (vsync on/off won't work)", "reshade::addon_event::init_swapchain",
            "reshade::addon_event::finish_present", "reshade::addon_event::present",
            "reshade::addon_event::reshade_present", "reshade::addon_event::init_command_list",
            "reshade::addon_event::execute_command_list", "reshade::addon_event::bind_pipeline",
            "reshade::addon_event::init_command_queue", "reshade::addon_event::reset_command_list",
            "reshade::addon_event::present_flags", "reshade::addon_event::draw", "reshade::addon_event::draw_indexed",
            "reshade::addon_event::draw_or_dispatch_indirect",
            // New power saving events
            "reshade::addon_event::dispatch", "reshade::addon_event::dispatch_mesh",
            "reshade::addon_event::dispatch_rays", "reshade::addon_event::copy_resource",
            "reshade::addon_event::update_buffer_region", "reshade::addon_event::update_buffer_region_command",
            "reshade::addon_event::bind_resource", "reshade::addon_event::map_resource",
            // Additional frame-specific GPU operations for power saving
            "reshade::addon_event::copy_buffer_region", "reshade::addon_event::copy_buffer_to_texture",
            "reshade::addon_event::copy_texture_to_buffer", "reshade::addon_event::copy_texture_region",
            "reshade::addon_event::resolve_texture_region", "reshade::addon_event::clear_render_target_view",
            "reshade::addon_event::clear_depth_stencil_view", "reshade::addon_event::clear_unordered_access_view_uint",
            "reshade::addon_event::clear_unordered_access_view_float", "reshade::addon_event::generate_mipmaps",
            "reshade::addon_event::blit", "reshade::addon_event::begin_query",
            "reshade::addon_event::end_query", "reshade::addon_event::resolve_query_data"};

        uint32_t total_events = 0;
        uint32_t visible_events = 0;

        for (int i = 0; i < NUM_EVENTS; i++) {
            // Skip events that are set to invisible
            if (!event_visibility[i]) {
                continue;
            }

            uint32_t count = g_swapchain_event_counters[i].load();
            total_events += count;
            visible_events++;

            // Green if > 0, red if 0
            ImVec4 color = (count > 0) ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
            ImGui::TextColored(color, "%s: %u", event_names[i], count);
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Total Events (Visible): %u", total_events);
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Hidden Events: %u", NUM_EVENTS - visible_events);

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
