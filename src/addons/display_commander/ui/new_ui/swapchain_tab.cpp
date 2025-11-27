#include "swapchain_tab.hpp"
#include "../../config/display_commander_config.hpp"
#include "../../globals.hpp"
#include "../../hooks/api_hooks.hpp"
#include "../../hooks/ngx_hooks.hpp"
#include "../../res/forkawesome.h"
#include "../../res/ui_colors.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../settings/swapchain_tab_settings.hpp"
#include "../../swapchain_events_power_saving.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/timing.hpp"

#include <dxgi1_6.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <unordered_map>
#include <reshade_imgui.hpp>
#include <sstream>
#include <vector>

namespace ui::new_ui {
bool has_last_metadata = false;
bool auto_apply_hdr_metadata = false;

// Static variables to track last set HDR metadata values
DXGI_HDR_METADATA_HDR10 last_hdr_metadata = {
    .RedPrimary = {32768, 21634},
    .GreenPrimary = {19661, 39321},
    .BluePrimary = {9830, 3932},
    .WhitePoint = {20493, 21564},
    .MaxMasteringLuminance = 1000,
    .MinMasteringLuminance = 0,
    .MaxContentLightLevel = 1000,
    .MaxFrameAverageLightLevel = 400,
};
DXGI_HDR_METADATA_HDR10 dirty_last_metadata = last_hdr_metadata;

std::string last_metadata_source = "None";

// Initialize swapchain tab
void InitSwapchainTab() {
    // Settings already loaded at startup
    // Default Rec. 2020 values
    double prim_red_x = 0.708;
    double prim_red_y = 0.292;
    double prim_green_x = 0.170;
    double prim_green_y = 0.797;
    double prim_blue_x = 0.131;
    double prim_blue_y = 0.046;
    double white_point_x = 0.3127;
    double white_point_y = 0.3290;
    int32_t max_mdl = 1000;
    float min_mdl = 0.0f;
    int32_t max_cll = 1000;
    int32_t max_fall = 100;

    // Read HDR metadata settings from DisplayCommander config
    display_commander::config::get_config_value("ReShade_HDR_Metadata", "prim_red_x", prim_red_x);
    display_commander::config::get_config_value("ReShade_HDR_Metadata", "prim_red_y", prim_red_y);
    display_commander::config::get_config_value("ReShade_HDR_Metadata", "prim_green_x", prim_green_x);
    display_commander::config::get_config_value("ReShade_HDR_Metadata", "prim_green_y", prim_green_y);
    display_commander::config::get_config_value("ReShade_HDR_Metadata", "prim_blue_x", prim_blue_x);
    display_commander::config::get_config_value("ReShade_HDR_Metadata", "prim_blue_y", prim_blue_y);
    display_commander::config::get_config_value("ReShade_HDR_Metadata", "white_point_x", white_point_x);
    display_commander::config::get_config_value("ReShade_HDR_Metadata", "white_point_y", white_point_y);
    display_commander::config::get_config_value("ReShade_HDR_Metadata", "max_mdl", max_mdl);
    display_commander::config::get_config_value("ReShade_HDR_Metadata", "min_mdl", min_mdl);
    display_commander::config::get_config_value("ReShade_HDR_Metadata", "max_cll", max_cll);
    display_commander::config::get_config_value("ReShade_HDR_Metadata", "max_fall", max_fall);

    // Read has_last_metadata flag from config
    display_commander::config::get_config_value("ReShade_HDR_Metadata", "has_last_metadata", has_last_metadata);
    display_commander::config::get_config_value("ReShade_HDR_Metadata", "auto_apply_hdr_metadata",
                                                auto_apply_hdr_metadata);

    // Initialize HDR metadata with loaded values
    last_hdr_metadata.RedPrimary[0] = static_cast<UINT16>(prim_red_x * 65535);
    last_hdr_metadata.RedPrimary[1] = static_cast<UINT16>(prim_red_y * 65535);
    last_hdr_metadata.GreenPrimary[0] = static_cast<UINT16>(prim_green_x * 65535);
    last_hdr_metadata.GreenPrimary[1] = static_cast<UINT16>(prim_green_y * 65535);
    last_hdr_metadata.BluePrimary[0] = static_cast<UINT16>(prim_blue_x * 65535);
    last_hdr_metadata.BluePrimary[1] = static_cast<UINT16>(prim_blue_y * 65535);
    last_hdr_metadata.WhitePoint[0] = static_cast<UINT16>(white_point_x * 65535);
    last_hdr_metadata.WhitePoint[1] = static_cast<UINT16>(white_point_y * 65535);
    last_hdr_metadata.MaxMasteringLuminance = static_cast<UINT>(max_mdl);
    last_hdr_metadata.MinMasteringLuminance = static_cast<UINT>(min_mdl * 10000.0f);
    last_hdr_metadata.MaxContentLightLevel = static_cast<UINT16>(max_cll);
    last_hdr_metadata.MaxFrameAverageLightLevel = static_cast<UINT16>(max_fall);

    // Initialize dirty metadata to match
    dirty_last_metadata = last_hdr_metadata;

    // Set metadata source and flag from config
    last_metadata_source = "Loaded from ReShade config";
}

void AutoApplyTrigger() {
    if (!auto_apply_hdr_metadata) {
        return;
    }
    if (g_last_reshade_device_api.load() != static_cast<int>(reshade::api::device_api::d3d12)
        && g_last_reshade_device_api.load() != static_cast<int>(reshade::api::device_api::d3d11)
        && g_last_reshade_device_api.load() != static_cast<int>(reshade::api::device_api::d3d10)) {
        return;
    }
    static bool first_apply = true;

    // Get the current swapchain
    HWND hwnd = g_last_swapchain_hwnd.load();
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return;  // No valid swapchain window
    }

    // Get the DXGI swapchain from the window
    Microsoft::WRL::ComPtr<IDXGISwapChain> dxgi_swapchain;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapchain4;

    // Try to get the swapchain from the window (this is a simplified approach)
    // In a real implementation, you'd need to get the actual swapchain from ReShade
    // For now, we'll assume we can get it somehow

    if (has_last_metadata) {
        // Auto-apply using last_hdr_metadata
        DXGI_HDR_METADATA_HDR10 hdr10_metadata = last_hdr_metadata;

        // Apply the stored metadata
        // Note: This is a placeholder - in real implementation you'd need access to the actual swapchain
        HRESULT hr = swapchain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(hdr10_metadata), &hdr10_metadata);

        LogDebug("AutoApplyTrigger: Applied stored HDR metadata");
        if (SUCCEEDED(hr)) {
            if (first_apply) {
                LogDebug("AutoApplyTrigger: First time applying stored HDR metadata");
                first_apply = false;
            }
        } else {
            LogDebug("AutoApplyTrigger: Failed to apply stored HDR metadata");
        }
    } else {
        HRESULT hr = swapchain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr);
        if (SUCCEEDED(hr)) {
            if (first_apply) {
                LogDebug("AutoApplyTrigger: Disabled HDR metadata");
                first_apply = false;
            }
        } else {
            LogDebug("AutoApplyTrigger: Failed to disable HDR metadata");
        }
    }
}

// Forward declarations
void DrawDLSSGSummaryContent();
void DrawDLSSPresetOverrideContent();
void DrawDLSSSettings();

void DrawSwapchainTab(reshade::api::effect_runtime* runtime) {
    ImGui::Text("Swapchain Tab - DXGI Information");

    // Draw all swapchain-related sections
    DrawSwapchainWrapperStats();
    ImGui::Spacing();
    DrawSwapchainEventCounters();
    ImGui::Spacing();
    DrawDLSSSettings();
    ImGui::Spacing();
    DrawNGXParameters();
    ImGui::Spacing();
    DrawSwapchainInfo(runtime);
    ImGui::Spacing();
    DrawDxgiCompositionInfo();
}

void DrawSwapchainWrapperStats() {
    if (ImGui::CollapsingHeader("Swapchain Wrapper Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Present/Present1 Calls Per Second & Frame Time Graphs");
        ImGui::Separator();

        // Helper function to display stats and frame graph for each swapchain type
        auto displayStatsAndGraph = [](const char* type_name, SwapChainWrapperStats& stats, ImVec4 color) {
            uint64_t present_calls = stats.total_present_calls.load(std::memory_order_acquire);
            uint64_t present1_calls = stats.total_present1_calls.load(std::memory_order_acquire);
            double present_fps = stats.smoothed_present_fps.load(std::memory_order_acquire);
            double present1_fps = stats.smoothed_present1_fps.load(std::memory_order_acquire);

            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::Text("%s Swapchain:", type_name);
            ImGui::PopStyleColor();

            ImGui::Indent();
            ImGui::Text("  Present: %.2f calls/sec (total: %llu)", present_fps, present_calls);
            ImGui::Text("  Present1: %.2f calls/sec (total: %llu)", present1_fps, present1_calls);

            // Get frame time data from ring buffer
            uint32_t head = stats.frame_time_head.load(std::memory_order_acquire);
            uint32_t count = (head > kSwapchainFrameTimeCapacity) ? kSwapchainFrameTimeCapacity : head;

            if (count > 0) {
                // Collect frame times for the graph (last 256 frames)
                std::vector<float> frame_times;
                frame_times.reserve(count);

                uint32_t start = (head >= kSwapchainFrameTimeCapacity) ? (head - kSwapchainFrameTimeCapacity) : 0;
                for (uint32_t i = start; i < head; ++i) {
                    float frame_time = stats.frame_times[i & (kSwapchainFrameTimeCapacity - 1)];
                    if (frame_time > 0.0f) {
                        frame_times.push_back(frame_time);
                    }
                }

                if (!frame_times.empty()) {
                    // Calculate statistics
                    auto minmax_it = std::minmax_element(frame_times.begin(), frame_times.end());
                    float min_ft = *minmax_it.first;
                    float max_ft = *minmax_it.second;
                    float avg_ft = 0.0f;
                    for (float ft : frame_times) {
                        avg_ft += ft;
                    }
                    avg_ft /= static_cast<float>(frame_times.size());

                    // Calculate average FPS from average frame time
                    float avg_fps = (avg_ft > 0.0f) ? (1000.0f / avg_ft) : 0.0f;

                    // Display statistics
                    ImGui::Text("  Frame Time: Min: %.2f ms | Max: %.2f ms | Avg: %.2f ms | FPS: %.1f",
                                min_ft, max_ft, avg_ft, avg_fps);

                    // Create overlay text
                    std::string overlay_text = "Frame Time: " + std::to_string(frame_times.back()).substr(0, 4) + " ms";

                    // Set graph size and scale
                    ImVec2 graph_size = ImVec2(-1.0f, 150.0f); // Full width, 150px height
                    float scale_min = 0.0f;
                    float scale_max = (std::max)(avg_ft * 3.0f, max_ft + 2.0f);

                    // Draw the frame time graph
                    std::string graph_label = std::string("##FrameTime") + type_name;
                    ImGui::PlotLines(graph_label.c_str(),
                                     frame_times.data(),
                                     static_cast<int>(frame_times.size()),
                                     0, // values_offset
                                     overlay_text.c_str(),
                                     scale_min,
                                     scale_max,
                                     graph_size);
                } else {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "  No frame time data available yet...");
                }
            } else {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "  No frame time data available yet...");
            }

            ImGui::Unindent();
        };

        displayStatsAndGraph("Proxy", g_swapchain_wrapper_stats_proxy, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
        ImGui::Spacing();
        displayStatsAndGraph("Native", g_swapchain_wrapper_stats_native, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
    }
}

void DrawSwapchainEventCounters() {
    // Swapchain Event Counters Section (see docs/UI_STYLE_GUIDE.md for depth/indent rules)
    // Depth 1: Nested subsection with indentation and distinct colors
    ImGui::Indent();  // Indent nested header
    ui::colors::PushNestedHeaderColors();  // Apply distinct colors for nested header
    if (ImGui::CollapsingHeader("Swapchain Event Counters", ImGuiTreeNodeFlags_None)) {
        ImGui::Indent();  // Indent content inside subsection
        ImGui::TextColored(ui::colors::TEXT_INFO, "Event Counters (Green = Working, Red = Not Working)");
        ImGui::Separator();

        // Display each event counter with color coding

        uint32_t total_events = 0;

        // Helper function to display event category
        auto displayEventCategory = [&](const char* name, const auto& event_array, const auto& event_names_map,
                                        ImVec4 header_color) {
            ui::colors::PushNestedHeaderColors();  // Apply distinct colors for nested category headers
            if (ImGui::CollapsingHeader(name, ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();

                uint32_t category_total = 0;
                for (size_t i = 0; i < event_array.size(); ++i) {
                    uint32_t count = event_array[i].load();
                    category_total += count;
                    total_events += count;

                    // Get the event name from the map
                    auto it = event_names_map.find(static_cast<decltype(event_names_map.begin()->first)>(i));
                    const char* event_name = (it != event_names_map.end()) ? it->second : "UNKNOWN_EVENT";

                    // Green if > 0, red if 0
                    ImVec4 color = (count > 0) ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
                    ImGui::TextColored(color, "%s (%zu): %u", event_name, i, count);
                }

                ImGui::TextColored(header_color, "Total %s: %u", name, category_total);
                ImGui::Unindent();
            }
            ui::colors::PopNestedHeaderColors();  // Restore default header colors
        };

        // ReShade Events
        static const std::map<ReShadeEventIndex, const char*> reshade_event_names = {
            {RESHADE_EVENT_BEGIN_RENDER_PASS, "RESHADE_EVENT_BEGIN_RENDER_PASS"},
            {RESHADE_EVENT_END_RENDER_PASS, "RESHADE_EVENT_END_RENDER_PASS"},
            {RESHADE_EVENT_CREATE_SWAPCHAIN_CAPTURE, "RESHADE_EVENT_CREATE_SWAPCHAIN_CAPTURE"},
            {RESHADE_EVENT_INIT_SWAPCHAIN, "RESHADE_EVENT_INIT_SWAPCHAIN"},
            {RESHADE_EVENT_PRESENT_UPDATE_AFTER, "RESHADE_EVENT_PRESENT_UPDATE_AFTER"},
            {RESHADE_EVENT_PRESENT_UPDATE_BEFORE, "RESHADE_EVENT_PRESENT_UPDATE_BEFORE"},
            {RESHADE_EVENT_PRESENT_UPDATE_BEFORE2_UNUSED, "RESHADE_EVENT_PRESENT_UPDATE_BEFORE2_UNUSED"},
            {RESHADE_EVENT_INIT_COMMAND_LIST, "RESHADE_EVENT_INIT_COMMAND_LIST"},
            {RESHADE_EVENT_EXECUTE_COMMAND_LIST, "RESHADE_EVENT_EXECUTE_COMMAND_LIST"},
            {RESHADE_EVENT_BIND_PIPELINE, "RESHADE_EVENT_BIND_PIPELINE"},
            {RESHADE_EVENT_INIT_COMMAND_QUEUE, "RESHADE_EVENT_INIT_COMMAND_QUEUE"},
            {RESHADE_EVENT_RESET_COMMAND_LIST, "RESHADE_EVENT_RESET_COMMAND_LIST"},
            {RESHADE_EVENT_PRESENT_FLAGS, "RESHADE_EVENT_PRESENT_FLAGS"},
            {RESHADE_EVENT_DRAW, "RESHADE_EVENT_DRAW"},
            {RESHADE_EVENT_DRAW_INDEXED, "RESHADE_EVENT_DRAW_INDEXED"},
            {RESHADE_EVENT_DRAW_OR_DISPATCH_INDIRECT, "RESHADE_EVENT_DRAW_OR_DISPATCH_INDIRECT"},
            {RESHADE_EVENT_DISPATCH, "RESHADE_EVENT_DISPATCH"},
            {RESHADE_EVENT_DISPATCH_MESH, "RESHADE_EVENT_DISPATCH_MESH"},
            {RESHADE_EVENT_DISPATCH_RAYS, "RESHADE_EVENT_DISPATCH_RAYS"},
            {RESHADE_EVENT_COPY_RESOURCE, "RESHADE_EVENT_COPY_RESOURCE"},
            {RESHADE_EVENT_UPDATE_BUFFER_REGION, "RESHADE_EVENT_UPDATE_BUFFER_REGION"},
            {RESHADE_EVENT_UPDATE_BUFFER_REGION_COMMAND, "RESHADE_EVENT_UPDATE_BUFFER_REGION_COMMAND"},
            {RESHADE_EVENT_BIND_RESOURCE, "RESHADE_EVENT_BIND_RESOURCE"},
            {RESHADE_EVENT_MAP_RESOURCE, "RESHADE_EVENT_MAP_RESOURCE"},
            {RESHADE_EVENT_COPY_BUFFER_REGION, "RESHADE_EVENT_COPY_BUFFER_REGION"},
            {RESHADE_EVENT_COPY_BUFFER_TO_TEXTURE, "RESHADE_EVENT_COPY_BUFFER_TO_TEXTURE"},
            {RESHADE_EVENT_COPY_TEXTURE_TO_BUFFER, "RESHADE_EVENT_COPY_TEXTURE_TO_BUFFER"},
            {RESHADE_EVENT_COPY_TEXTURE_REGION, "RESHADE_EVENT_COPY_TEXTURE_REGION"},
            {RESHADE_EVENT_RESOLVE_TEXTURE_REGION, "RESHADE_EVENT_RESOLVE_TEXTURE_REGION"},
            {RESHADE_EVENT_CLEAR_RENDER_TARGET_VIEW, "RESHADE_EVENT_CLEAR_RENDER_TARGET_VIEW"},
            {RESHADE_EVENT_CLEAR_DEPTH_STENCIL_VIEW, "RESHADE_EVENT_CLEAR_DEPTH_STENCIL_VIEW"},
            {RESHADE_EVENT_CLEAR_UNORDERED_ACCESS_VIEW_UINT, "RESHADE_EVENT_CLEAR_UNORDERED_ACCESS_VIEW_UINT"},
            {RESHADE_EVENT_CLEAR_UNORDERED_ACCESS_VIEW_FLOAT, "RESHADE_EVENT_CLEAR_UNORDERED_ACCESS_VIEW_FLOAT"},
            {RESHADE_EVENT_GENERATE_MIPMAPS, "RESHADE_EVENT_GENERATE_MIPMAPS"},
            {RESHADE_EVENT_BLIT, "RESHADE_EVENT_BLIT"},
            {RESHADE_EVENT_BEGIN_QUERY, "RESHADE_EVENT_BEGIN_QUERY"},
            {RESHADE_EVENT_END_QUERY, "RESHADE_EVENT_END_QUERY"},
            {RESHADE_EVENT_RESOLVE_QUERY_DATA, "RESHADE_EVENT_RESOLVE_QUERY_DATA"}};
        displayEventCategory("ReShade Events", g_reshade_event_counters, reshade_event_names,
                             ImVec4(0.8f, 0.8f, 1.0f, 1.0f));

        // DXGI Core Methods
        static const std::map<DxgiCoreEventIndex, const char*> dxgi_core_event_names = {
            {DXGI_CORE_EVENT_PRESENT, "DXGI_CORE_EVENT_PRESENT"},
            {DXGI_CORE_EVENT_GETBUFFER, "DXGI_CORE_EVENT_GETBUFFER"},
            {DXGI_CORE_EVENT_SETFULLSCREENSTATE, "DXGI_CORE_EVENT_SETFULLSCREENSTATE"},
            {DXGI_CORE_EVENT_GETFULLSCREENSTATE, "DXGI_CORE_EVENT_GETFULLSCREENSTATE"},
            {DXGI_CORE_EVENT_GETDESC, "DXGI_CORE_EVENT_GETDESC"},
            {DXGI_CORE_EVENT_RESIZEBUFFERS, "DXGI_CORE_EVENT_RESIZEBUFFERS"},
            {DXGI_CORE_EVENT_RESIZETARGET, "DXGI_CORE_EVENT_RESIZETARGET"},
            {DXGI_CORE_EVENT_GETCONTAININGOUTPUT, "DXGI_CORE_EVENT_GETCONTAININGOUTPUT"},
            {DXGI_CORE_EVENT_GETFRAMESTATISTICS, "DXGI_CORE_EVENT_GETFRAMESTATISTICS"},
            {DXGI_CORE_EVENT_GETLASTPRESENTCOUNT, "DXGI_CORE_EVENT_GETLASTPRESENTCOUNT"}};
        displayEventCategory("DXGI Core Methods", g_dxgi_core_event_counters, dxgi_core_event_names,
                             ImVec4(0.8f, 1.0f, 0.8f, 1.0f));

        // DXGI SwapChain1 Methods
        static const std::map<DxgiSwapChain1EventIndex, const char*> dxgi_sc1_event_names = {
            {DXGI_SC1_EVENT_GETDESC1, "DXGI_SC1_EVENT_GETDESC1"},
            {DXGI_SC1_EVENT_GETFULLSCREENDESC, "DXGI_SC1_EVENT_GETFULLSCREENDESC"},
            {DXGI_SC1_EVENT_GETHWND, "DXGI_SC1_EVENT_GETHWND"},
            {DXGI_SC1_EVENT_GETCOREWINDOW, "DXGI_SC1_EVENT_GETCOREWINDOW"},
            {DXGI_SC1_EVENT_PRESENT1, "DXGI_SC1_EVENT_PRESENT1"},
            {DXGI_SC1_EVENT_ISTEMPORARYMONOSUPPORTED, "DXGI_SC1_EVENT_ISTEMPORARYMONOSUPPORTED"},
            {DXGI_SC1_EVENT_GETRESTRICTTOOUTPUT, "DXGI_SC1_EVENT_GETRESTRICTTOOUTPUT"},
            {DXGI_SC1_EVENT_SETBACKGROUNDCOLOR, "DXGI_SC1_EVENT_SETBACKGROUNDCOLOR"},
            {DXGI_SC1_EVENT_GETBACKGROUNDCOLOR, "DXGI_SC1_EVENT_GETBACKGROUNDCOLOR"},
            {DXGI_SC1_EVENT_SETROTATION, "DXGI_SC1_EVENT_SETROTATION"},
            {DXGI_SC1_EVENT_GETROTATION, "DXGI_SC1_EVENT_GETROTATION"}};
        displayEventCategory("DXGI SwapChain1 Methods", g_dxgi_sc1_event_counters, dxgi_sc1_event_names,
                             ImVec4(1.0f, 0.8f, 0.8f, 1.0f));

        // DXGI SwapChain2 Methods
        static const std::map<DxgiSwapChain2EventIndex, const char*> dxgi_sc2_event_names = {
            {DXGI_SC2_EVENT_SETSOURCESIZE, "DXGI_SC2_EVENT_SETSOURCESIZE"},
            {DXGI_SC2_EVENT_GETSOURCESIZE, "DXGI_SC2_EVENT_GETSOURCESIZE"},
            {DXGI_SC2_EVENT_SETMAXIMUMFRAMELATENCY, "DXGI_SC2_EVENT_SETMAXIMUMFRAMELATENCY"},
            {DXGI_SC2_EVENT_GETMAXIMUMFRAMELATENCY, "DXGI_SC2_EVENT_GETMAXIMUMFRAMELATENCY"},
            {DXGI_SC2_EVENT_GETFRAMELATENCYWAIABLEOBJECT, "DXGI_SC2_EVENT_GETFRAMELATENCYWAIABLEOBJECT"},
            {DXGI_SC2_EVENT_SETMATRIXTRANSFORM, "DXGI_SC2_EVENT_SETMATRIXTRANSFORM"},
            {DXGI_SC2_EVENT_GETMATRIXTRANSFORM, "DXGI_SC2_EVENT_GETMATRIXTRANSFORM"}};
        displayEventCategory("DXGI SwapChain2 Methods", g_dxgi_sc2_event_counters, dxgi_sc2_event_names,
                             ImVec4(1.0f, 1.0f, 0.8f, 1.0f));

        // DXGI SwapChain3 Methods
        static const std::map<DxgiSwapChain3EventIndex, const char*> dxgi_sc3_event_names = {
            {DXGI_SC3_EVENT_GETCURRENTBACKBUFFERINDEX, "DXGI_SC3_EVENT_GETCURRENTBACKBUFFERINDEX"},
            {DXGI_SC3_EVENT_CHECKCOLORSPACESUPPORT, "DXGI_SC3_EVENT_CHECKCOLORSPACESUPPORT"},
            {DXGI_SC3_EVENT_SETCOLORSPACE1, "DXGI_SC3_EVENT_SETCOLORSPACE1"},
            {DXGI_SC3_EVENT_RESIZEBUFFERS1, "DXGI_SC3_EVENT_RESIZEBUFFERS1"}};
        displayEventCategory("DXGI SwapChain3 Methods", g_dxgi_sc3_event_counters, dxgi_sc3_event_names,
                             ImVec4(0.8f, 1.0f, 1.0f, 1.0f));

        // DXGI Factory Methods
        static const std::map<DxgiFactoryEventIndex, const char*> dxgi_factory_event_names = {
            {DXGI_FACTORY_EVENT_CREATESWAPCHAIN, "DXGI_FACTORY_EVENT_CREATESWAPCHAIN"},
            {DXGI_FACTORY_EVENT_CREATEFACTORY, "DXGI_FACTORY_EVENT_CREATEFACTORY"},
            {DXGI_FACTORY_EVENT_CREATEFACTORY1, "DXGI_FACTORY_EVENT_CREATEFACTORY1"}};
        displayEventCategory("DXGI Factory Methods", g_dxgi_factory_event_counters, dxgi_factory_event_names,
                             ImVec4(1.0f, 0.8f, 1.0f, 1.0f));

        // DXGI SwapChain4 Methods
        static const std::map<DxgiSwapChain4EventIndex, const char*> dxgi_sc4_event_names = {
            {DXGI_SC4_EVENT_SETHDRMETADATA, "DXGI_SC4_EVENT_SETHDRMETADATA"}};
        displayEventCategory("DXGI SwapChain4 Methods", g_dxgi_sc4_event_counters, dxgi_sc4_event_names,
                             ImVec4(0.8f, 0.8f, 0.8f, 1.0f));

        // DXGI Output Methods
        static const std::map<DxgiOutputEventIndex, const char*> dxgi_output_event_names = {
            {DXGI_OUTPUT_EVENT_SETGAMMACONTROL, "DXGI_OUTPUT_EVENT_SETGAMMACONTROL"},
            {DXGI_OUTPUT_EVENT_GETGAMMACONTROL, "DXGI_OUTPUT_EVENT_GETGAMMACONTROL"},
            {DXGI_OUTPUT_EVENT_GETDESC, "DXGI_OUTPUT_EVENT_GETDESC"}};
        displayEventCategory("DXGI Output Methods", g_dxgi_output_event_counters, dxgi_output_event_names,
                             ImVec4(0.8f, 1.0f, 0.8f, 1.0f));

        // DirectX 9 Methods
        static const std::map<Dx9EventIndex, const char*> dx9_event_names = {{DX9_EVENT_PRESENT, "DX9_EVENT_PRESENT"}};
        displayEventCategory("DirectX 9 Methods", g_dx9_event_counters, dx9_event_names,
                             ImVec4(1.0f, 0.6f, 0.6f, 1.0f));

        // Streamline Methods
        static const std::map<StreamlineEventIndex, const char*> streamline_event_names = {
            {STREAMLINE_EVENT_SL_INIT, "STREAMLINE_EVENT_SL_INIT"},
            {STREAMLINE_EVENT_SL_IS_FEATURE_SUPPORTED, "STREAMLINE_EVENT_SL_IS_FEATURE_SUPPORTED"},
            {STREAMLINE_EVENT_SL_GET_NATIVE_INTERFACE, "STREAMLINE_EVENT_SL_GET_NATIVE_INTERFACE"},
            {STREAMLINE_EVENT_SL_UPGRADE_INTERFACE, "STREAMLINE_EVENT_SL_UPGRADE_INTERFACE"}};
        displayEventCategory("Streamline Methods", g_streamline_event_counters, streamline_event_names,
                             ImVec4(0.6f, 0.8f, 1.0f, 1.0f));

        // D3D11 Texture Methods
        static const std::map<D3D11TextureEventIndex, const char*> d3d11_texture_event_names = {
            {D3D11_EVENT_CREATE_TEXTURE2D, "D3D11_EVENT_CREATE_TEXTURE2D"},
            {D3D11_EVENT_UPDATE_SUBRESOURCE, "D3D11_EVENT_UPDATE_SUBRESOURCE"},
            {D3D11_EVENT_UPDATE_SUBRESOURCE1, "D3D11_EVENT_UPDATE_SUBRESOURCE1"}};
        displayEventCategory("D3D11 Texture Methods", g_d3d11_texture_event_counters, d3d11_texture_event_names,
                             ImVec4(1.0f, 0.8f, 0.6f, 1.0f));

        ImGui::Separator();
        ImGui::TextColored(ui::colors::TEXT_INFO, "Total Events: %u", total_events);

        // NVAPI Event Counters Section
        ImGui::Spacing();
        ui::colors::PushNestedHeaderColors();  // Apply distinct colors for nested NVAPI header
        if (ImGui::CollapsingHeader("NVAPI Event Counters", ImGuiTreeNodeFlags_DefaultOpen)) {
            // NVAPI event mapping
            static const std::vector<std::pair<NvapiEventIndex, const char*>> nvapi_event_mapping = {
                {NVAPI_EVENT_GET_HDR_CAPABILITIES, "NVAPI_EVENT_GET_HDR_CAPABILITIES"},
                {NVAPI_EVENT_D3D_SET_LATENCY_MARKER, "NVAPI_EVENT_D3D_SET_LATENCY_MARKER"},
                {NVAPI_EVENT_D3D_SET_SLEEP_MODE, "NVAPI_EVENT_D3D_SET_SLEEP_MODE"},
                {NVAPI_EVENT_D3D_SLEEP, "NVAPI_EVENT_D3D_SLEEP"},
                {NVAPI_EVENT_D3D_GET_LATENCY, "NVAPI_EVENT_D3D_GET_LATENCY"}};

            uint32_t nvapi_total_events = 0;

            // Group NVAPI events by category
            struct NvapiEventGroup {
                const char* name;
                NvapiEventIndex start_idx;
                NvapiEventIndex end_idx;
                ImVec4 color;
            };

            static const std::vector<NvapiEventGroup> nvapi_event_groups = {
                {.name = "NVAPI HDR Methods",
                 .start_idx = NVAPI_EVENT_GET_HDR_CAPABILITIES,
                 .end_idx = NVAPI_EVENT_GET_HDR_CAPABILITIES,
                 .color = ImVec4(0.6f, 1.0f, 0.6f, 1.0f)},
                {.name = "NVAPI Reflex Methods",
                 .start_idx = NVAPI_EVENT_D3D_SET_LATENCY_MARKER,
                 .end_idx = NVAPI_EVENT_D3D_GET_LATENCY,
                 .color = ImVec4(0.6f, 1.0f, 0.8f, 1.0f)}};

            for (const auto& group : nvapi_event_groups) {
                if (ImGui::CollapsingHeader(group.name, ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Indent();

                    for (int i = static_cast<int>(group.start_idx); i <= static_cast<int>(group.end_idx); ++i) {
                        uint32_t count = g_nvapi_event_counters[i].load();
                        nvapi_total_events += count;

                        ImGui::TextColored(group.color, "%s: %u", nvapi_event_mapping[i].second, count);
                    }

                    ImGui::Unindent();
                }
            }

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Total NVAPI Events: %u", nvapi_total_events);

            // Show last sleep timestamp
            uint64_t last_sleep_timestamp = g_nvapi_last_sleep_timestamp_ns.load();
            if (last_sleep_timestamp > 0) {
                uint64_t current_time = utils::get_now_ns();
                uint64_t time_since_sleep = current_time - last_sleep_timestamp;
                double time_since_sleep_ms = static_cast<double>(time_since_sleep) / utils::NS_TO_MS;

                ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "Last Sleep: %.2f ms ago", time_since_sleep_ms);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Time since the last NVAPI_D3D_Sleep call was made.\nLower values indicate more recent sleep "
                        "calls.");
                }
            } else {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Last Sleep: Never");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("No NVAPI_D3D_Sleep calls have been made yet.");
                }
            }

            // Show NVAPI status message
            if (nvapi_total_events > 0) {
                ImGui::TextColored(ui::colors::TEXT_SUCCESS, "Status: NVAPI events are working correctly");
            } else {
                ImGui::TextColored(ui::colors::TEXT_WARNING, "Status: No NVAPI events detected");
            }
        }
        ui::colors::PopNestedHeaderColors();  // Restore default header colors

        // Show status message
        if (total_events > 0) {
            ImGui::TextColored(ui::colors::TEXT_SUCCESS, "Status: Swapchain events are working correctly");
        } else {
            ImGui::TextColored(ui::colors::TEXT_ERROR,
                               "Status: No swapchain events detected - check if addon is properly loaded");
        }
        ImGui::Unindent();  // Unindent content
    }
    ui::colors::PopNestedHeaderColors();  // Restore default header colors
    ImGui::Unindent();  // Unindent nested header section

    // NGX Counters Section (see docs/UI_STYLE_GUIDE.md for depth/indent rules)
    // Depth 0: Main section header
    if (ImGui::CollapsingHeader("NGX Counters", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextColored(ui::colors::TEXT_SUCCESS, "NVIDIA NGX Function Call Counters");
        ImGui::Separator();

        // Depth 1: Nested subsections with indentation and distinct colors
        // Parameter functions
        ImGui::Indent();  // Indent nested headers
        ui::colors::PushNestedHeaderColors();  // Apply distinct colors for nested headers
        if (ImGui::CollapsingHeader("Parameter Functions", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();  // Indent content inside subsection
            ImGui::TextColored(ui::colors::TEXT_VALUE, "SetF: %u", g_ngx_counters.parameter_setf_count.load());
            ImGui::TextColored(ui::colors::TEXT_VALUE, "SetD: %u", g_ngx_counters.parameter_setd_count.load());
            ImGui::TextColored(ui::colors::TEXT_VALUE, "SetI: %u", g_ngx_counters.parameter_seti_count.load());
            ImGui::TextColored(ui::colors::TEXT_VALUE, "SetUI: %u", g_ngx_counters.parameter_setui_count.load());
            ImGui::TextColored(ui::colors::TEXT_VALUE, "SetULL: %u", g_ngx_counters.parameter_setull_count.load());
            ImGui::TextColored(ui::colors::TEXT_VALUE, "GetI: %u", g_ngx_counters.parameter_geti_count.load());
            ImGui::TextColored(ui::colors::TEXT_VALUE, "GetUI: %u", g_ngx_counters.parameter_getui_count.load());
            ImGui::TextColored(ui::colors::TEXT_VALUE, "GetULL: %u", g_ngx_counters.parameter_getull_count.load());
            ImGui::TextColored(ui::colors::TEXT_VALUE, "GetVoidPointer: %u",
                               g_ngx_counters.parameter_getvoidpointer_count.load());
            ImGui::Unindent();  // Unindent content
        }
        ui::colors::PopNestedHeaderColors();  // Restore default header colors

        // D3D12 Feature Management
        ui::colors::PushNestedHeaderColors();
        if (ImGui::CollapsingHeader("D3D12 Feature Management", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            ImGui::TextColored(ui::colors::TEXT_VALUE, "Init: %u", g_ngx_counters.d3d12_init_count.load());
            ImGui::TextColored(ui::colors::TEXT_VALUE, "Init Ext: %u", g_ngx_counters.d3d12_init_ext_count.load());
            ImGui::TextColored(ui::colors::TEXT_VALUE, "Init ProjectID: %u",
                               g_ngx_counters.d3d12_init_projectid_count.load());
            ImGui::TextColored(ui::colors::TEXT_VALUE, "CreateFeature: %u",
                               g_ngx_counters.d3d12_createfeature_count.load());
            ImGui::TextColored(ui::colors::TEXT_VALUE, "ReleaseFeature: %u",
                               g_ngx_counters.d3d12_releasefeature_count.load());
            ImGui::TextColored(ui::colors::TEXT_VALUE, "EvaluateFeature: %u",
                               g_ngx_counters.d3d12_evaluatefeature_count.load());
            ImGui::TextColored(ui::colors::TEXT_VALUE, "GetParameters: %u",
                               g_ngx_counters.d3d12_getparameters_count.load());
            ImGui::TextColored(ui::colors::TEXT_VALUE, "AllocateParameters: %u",
                               g_ngx_counters.d3d12_allocateparameters_count.load());
            ImGui::Unindent();
        }
        ui::colors::PopNestedHeaderColors();

        // D3D11 Feature Management
        ui::colors::PushNestedHeaderColors();
        if (ImGui::CollapsingHeader("D3D11 Feature Management", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            ImGui::TextColored(ui::colors::TEXT_VALUE, "Init: %u", g_ngx_counters.d3d11_init_count.load());
            ImGui::TextColored(ui::colors::TEXT_VALUE, "Init Ext: %u", g_ngx_counters.d3d11_init_ext_count.load());
            ImGui::TextColored(ui::colors::TEXT_VALUE, "Init ProjectID: %u",
                               g_ngx_counters.d3d11_init_projectid_count.load());
            ImGui::TextColored(ui::colors::TEXT_VALUE, "CreateFeature: %u",
                               g_ngx_counters.d3d11_createfeature_count.load());
            ImGui::TextColored(ui::colors::TEXT_VALUE, "ReleaseFeature: %u",
                               g_ngx_counters.d3d11_releasefeature_count.load());
            ImGui::TextColored(ui::colors::TEXT_VALUE, "EvaluateFeature: %u",
                               g_ngx_counters.d3d11_evaluatefeature_count.load());
            ImGui::TextColored(ui::colors::TEXT_VALUE, "GetParameters: %u",
                               g_ngx_counters.d3d11_getparameters_count.load());
            ImGui::TextColored(ui::colors::TEXT_VALUE, "AllocateParameters: %u",
                               g_ngx_counters.d3d11_allocateparameters_count.load());
            ImGui::Unindent();
        }
        ui::colors::PopNestedHeaderColors();
        ImGui::Unindent();  // Unindent nested headers section

        ImGui::Separator();
        ImGui::TextColored(ui::colors::TEXT_INFO, "Total NGX Calls: %u", g_ngx_counters.total_count.load());

        // Reset button
        if (ImGui::Button("Reset NGX Counters")) {
            g_ngx_counters.reset();
        }

        // Show status message
        uint32_t total_ngx_calls = g_ngx_counters.total_count.load();
        if (total_ngx_calls > 0) {
            ImGui::TextColored(ui::colors::TEXT_SUCCESS, "Status: NGX functions are being called");
        } else {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "Status: No NGX calls detected yet");
        }
    }

    // NVAPI SetSleepMode Values Section
    if (ImGui::CollapsingHeader("NVAPI SetSleepMode Values", ImGuiTreeNodeFlags_None)) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Last NVAPI SetSleepMode Parameters");
        ImGui::Separator();

        auto params = g_last_nvapi_sleep_mode_params.load();
        if (params) {
            ImGui::Text("Low Latency Mode: %s", params->bLowLatencyMode ? "Enabled" : "Disabled");
            ImGui::Text("Boost: %s", params->bLowLatencyBoost ? "Enabled" : "Disabled");
            ImGui::Text("Use Markers to Optimize: %s", params->bUseMarkersToOptimize ? "Enabled" : "Disabled");
            ImGui::Text("Minimum Interval: %u μs", params->minimumIntervalUs);

            // Calculate FPS from interval
            if (params->minimumIntervalUs > 0) {
                float fps = 1000000.0f / params->minimumIntervalUs;
                ImGui::Text("Target FPS: %.1f", fps);
            } else {
                ImGui::Text("Target FPS: Unlimited");
            }
        } else {
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "No NVAPI SetSleepMode calls detected yet");
        }
    }

    // Power Saving Settings Section
    if (ImGui::CollapsingHeader("Power Saving Settings", ImGuiTreeNodeFlags_None)) {
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
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), ICON_FK_WARNING);
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
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "  " ICON_FK_OK " Power saving is currently active");
        }
    }
}

void DrawNGXParameters() {

    if (ImGui::CollapsingHeader("NGX Parameters", ImGuiTreeNodeFlags_None)) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "NGX Parameter Values (Live from Game)");
        ImGui::Separator();

        // Collect all parameters into a unified list
        struct ParameterEntry {
            std::string name;
            std::string value;
            std::string type;
            ImVec4 color;
        };

        std::vector<ParameterEntry> all_params;

        // Add all parameters from unified storage
        auto all_params_map = g_ngx_parameters.get_all();
        if (all_params_map) {
            for (const auto& [key, value] : *all_params_map) {
                std::string value_str;
                std::string type_str;
                ImVec4 color;

                switch (value.type) {
                    case ParameterValue::FLOAT: {
                        char buffer[32];
                        snprintf(buffer, sizeof(buffer), "%.6f", value.get_as_float());
                        value_str = std::string(buffer);
                        type_str = "float";
                        color = ImVec4(0.0f, 1.0f, 1.0f, 1.0f);  // Cyan
                        break;
                    }
                    case ParameterValue::DOUBLE: {
                        char buffer[32];
                        snprintf(buffer, sizeof(buffer), "%.6f", value.get_as_double());
                        value_str = std::string(buffer);
                        type_str = "double";
                        color = ImVec4(0.0f, 1.0f, 0.8f, 1.0f);  // Light cyan
                        break;
                    }
                    case ParameterValue::INT: {
                        value_str = std::to_string(value.get_as_int());
                        type_str = "int";
                        color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);  // Yellow
                        break;
                    }
                    case ParameterValue::UINT: {
                        value_str = std::to_string(value.get_as_uint());
                        type_str = "uint";
                        color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);  // Orange
                        break;
                    }
                    case ParameterValue::ULL: {
                        value_str = std::to_string(value.get_as_ull());
                        type_str = "ull";
                        color = ImVec4(1.0f, 0.6f, 0.0f, 1.0f);  // Dark orange
                        break;
                    }
                    default:
                        value_str = "unknown";
                        type_str = "unknown";
                        color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);  // Gray
                        break;
                }

                all_params.push_back({key, value_str, type_str, color});
            }
        }

        // Sort parameters alphabetically by name
        std::sort(all_params.begin(), all_params.end(),
                  [](const ParameterEntry& a, const ParameterEntry& b) { return a.name < b.name; });

        // Display unified parameter list
        if (!all_params.empty()) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f),
                               "All Parameters (%zu) - Sorted Alphabetically:", all_params.size());
            ImGui::Spacing();

            // Add search filter
            static char search_filter[256] = "";
            ImGui::InputTextWithHint("##NGXSearch", "Search parameters...", search_filter, sizeof(search_filter));
            ImGui::Spacing();

            // Create a table-like display
            ImGui::Columns(5, "NGXParameters", true);
            ImGui::SetColumnWidth(0, 500);  // Parameter name
            ImGui::SetColumnWidth(1, 80);    // Type
            ImGui::SetColumnWidth(2, 150);   // Value (game value)
            ImGui::SetColumnWidth(3, 150);   // Override value
            ImGui::SetColumnWidth(4, 600);   // Actions (wider for buttons)

            // Header
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "Parameter Name");
            ImGui::NextColumn();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "Type");
            ImGui::NextColumn();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "Game Value");
            ImGui::NextColumn();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "Override");
            ImGui::NextColumn();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "Actions");
            ImGui::NextColumn();
            ImGui::Separator();

            // Display each parameter (with filtering)
            size_t displayed_count = 0;
            for (const auto& param : all_params) {
                // Apply search filter
                if (strlen(search_filter) > 0) {
                    std::string lower_name = param.name;
                    std::string lower_filter = search_filter;
                    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
                    std::transform(lower_filter.begin(), lower_filter.end(), lower_filter.begin(), ::tolower);

                    if (lower_name.find(lower_filter) == std::string::npos) {
                        continue;  // Skip this parameter if it doesn't match the filter
                    }
                }

                ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "%s", param.name.c_str());
                ImGui::NextColumn();
                ImGui::TextColored(param.color, "%s", param.type.c_str());
                ImGui::NextColumn();
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s", param.value.c_str());
                ImGui::NextColumn();

                // Check if override exists
                bool has_override = false;
                std::string override_value_str = "-";
                ParameterValue override_value;
                if (g_ngx_parameter_overrides.get(param.name, override_value)) {
                    has_override = true;
                    switch (override_value.type) {
                        case ParameterValue::FLOAT: {
                            char buffer[32];
                            snprintf(buffer, sizeof(buffer), "%.6f", override_value.get_as_float());
                            override_value_str = std::string(buffer);
                            break;
                        }
                        case ParameterValue::DOUBLE: {
                            char buffer[32];
                            snprintf(buffer, sizeof(buffer), "%.6f", override_value.get_as_double());
                            override_value_str = std::string(buffer);
                            break;
                        }
                        case ParameterValue::INT:
                            override_value_str = std::to_string(override_value.get_as_int());
                            break;
                        case ParameterValue::UINT:
                            override_value_str = std::to_string(override_value.get_as_uint());
                            break;
                        case ParameterValue::ULL:
                            override_value_str = std::to_string(override_value.get_as_ull());
                            break;
                        default:
                            override_value_str = "unknown";
                            break;
                    }
                }

                // Display override value (highlighted if active)
                if (has_override) {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%s", override_value_str.c_str());
                } else {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", override_value_str.c_str());
                }
                ImGui::NextColumn();

                // Action buttons
                ImGui::PushID(param.name.c_str());

                // Input field for override value
                ImGui::SetNextItemWidth(120);
                if (has_override) {
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.3f, 0.0f, 1.0f)); // Green tint for active override
                }

                // Create input field based on type
                bool value_updated = false;
                if (param.type == "float" || param.type == "double") {
                    float float_val = has_override ? (param.type == "float" ? override_value.get_as_float() : static_cast<float>(override_value.get_as_double())) : 0.0f;
                    if (ImGui::InputFloat("##OverrideInput", &float_val, 0.0f, 0.0f, "%.6f", ImGuiInputTextFlags_EnterReturnsTrue)) {
                        if (param.type == "float") {
                            g_ngx_parameter_overrides.update_float(param.name, float_val);
                        } else {
                            g_ngx_parameter_overrides.update_double(param.name, static_cast<double>(float_val));
                        }
                        LogInfo("NGX Parameter Override Set: %s = %f", param.name.c_str(), float_val);
                        value_updated = true;
                    }
                } else if (param.type == "int") {
                    int int_val = has_override ? override_value.get_as_int() : 0;
                    if (ImGui::InputInt("##OverrideInput", &int_val, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
                        g_ngx_parameter_overrides.update_int(param.name, int_val);
                        LogInfo("NGX Parameter Override Set: %s = %d", param.name.c_str(), int_val);
                        value_updated = true;
                    }
                } else if (param.type == "uint") {
                    unsigned int uint_val = has_override ? override_value.get_as_uint() : 0;
                    int int_val = static_cast<int>(uint_val);
                    if (ImGui::InputInt("##OverrideInput", &int_val, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
                        if (int_val >= 0) {
                            g_ngx_parameter_overrides.update_uint(param.name, static_cast<unsigned int>(int_val));
                            LogInfo("NGX Parameter Override Set: %s = %u", param.name.c_str(), static_cast<unsigned int>(int_val));
                            value_updated = true;
                        }
                    }
                } else if (param.type == "ull") {
                    uint64_t ull_val = has_override ? override_value.get_as_ull() : 0;
                    char ull_str[32];
                    snprintf(ull_str, sizeof(ull_str), "%llu", ull_val);
                    if (ImGui::InputText("##OverrideInput", ull_str, sizeof(ull_str), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsDecimal)) {
                        uint64_t new_val = strtoull(ull_str, nullptr, 10);
                        g_ngx_parameter_overrides.update_ull(param.name, new_val);
                        LogInfo("NGX Parameter Override Set: %s = %llu", param.name.c_str(), new_val);
                        value_updated = true;
                    }
                }

                if (has_override) {
                    ImGui::PopStyleColor();
                }

                // Buttons on new line for better visibility
                ImGui::SameLine();
                if (ImGui::Button("Apply", ImVec2(100, 0))) {
                    // Force call NGX API to set the value immediately
                    if (ApplyNGXParameterOverride(param.name.c_str(), param.type.c_str())) {
                        LogInfo("NGX Parameter Applied: %s", param.name.c_str());
                    } else {
                        LogInfo("NGX Parameter Apply failed: %s (no override or parameter object)", param.name.c_str());
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Set", ImVec2(100, 0))) {
                    // Set override to current game value
                    auto all_params_map = g_ngx_parameters.get_all();
                    if (all_params_map) {
                        auto it = all_params_map->find(param.name);
                        if (it != all_params_map->end()) {
                            g_ngx_parameter_overrides.update(param.name, it->second);
                            LogInfo("NGX Parameter Override Set: %s", param.name.c_str());
                        }
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear", ImVec2(100, 0))) {
                    // Remove override
                    g_ngx_parameter_overrides.remove(param.name);
                    LogInfo("NGX Parameter Override Cleared: %s", param.name.c_str());
                }
                ImGui::PopID();
                ImGui::NextColumn();
                displayed_count++;
            }

            // Show filtered count if search is active
            if (strlen(search_filter) > 0) {
                ImGui::Columns(1);
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Showing %zu of %zu parameters", displayed_count,
                                   all_params.size());
                ImGui::Spacing();
            }

            ImGui::Columns(1);  // Reset columns
            ImGui::Spacing();

            // Show type legend with counts
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Type Legend:");
            ImGui::Indent();

            // Count parameters by type
            size_t float_count = 0, double_count = 0, int_count = 0, uint_count = 0, ull_count = 0;
            for (const auto& param : all_params) {
                if (param.type == "float")
                    float_count++;
                else if (param.type == "double")
                    double_count++;
                else if (param.type == "int")
                    int_count++;
                else if (param.type == "uint")
                    uint_count++;
                else if (param.type == "ull")
                    ull_count++;
            }

            ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "float (%zu)", float_count);
            ImGui::SameLine(100);
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.8f, 1.0f), "double (%zu)", double_count);
            ImGui::SameLine(200);
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "int (%zu)", int_count);
            ImGui::SameLine(300);
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "uint (%zu)", uint_count);
            ImGui::SameLine(400);
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "ull (%zu)", ull_count);
            ImGui::Unindent();
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), ICON_FK_WARNING " No NGX parameters detected yet");
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Total NGX Parameters: %zu", all_params.size());

        if (!all_params.empty()) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), ICON_FK_OK " NGX parameter hooks are working correctly");
        }
    }

}

// Draw DLSS Settings section with nested subheaders (see docs/UI_STYLE_GUIDE.md for depth/indent rules)
void DrawDLSSSettings() {
    // Depth 0: Main section header
    if (ImGui::CollapsingHeader("DLSS Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextColored(ui::colors::TEXT_DEFAULT, "DLSS/DLSS-G/Ray Reconstruction Configuration");
        ImGui::Separator();

        // Depth 1: Nested subsections with indentation and distinct colors
        ImGui::Indent();  // Indent nested headers

        // DLSS/DLSS-G/RR Summary subsection
        ui::colors::PushNestedHeaderColors();  // Apply distinct colors for nested header
        if (ImGui::CollapsingHeader("DLSS/DLSS-G/RR Summary", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();  // Indent content inside subsection
            DrawDLSSGSummaryContent();
            ImGui::Unindent();  // Unindent content
        }
        ui::colors::PopNestedHeaderColors();  // Restore default header colors

        ImGui::Spacing();

        // DLSS Preset Override subsection
        ui::colors::PushNestedHeaderColors();  // Apply distinct colors for nested header
        if (ImGui::CollapsingHeader("DLSS Preset Override", ImGuiTreeNodeFlags_None)) {
            ImGui::Indent();  // Indent content inside subsection
            DrawDLSSPresetOverrideContent();
            ImGui::Unindent();  // Unindent content
        }
        ui::colors::PopNestedHeaderColors();  // Restore default header colors

        ImGui::Unindent();  // Unindent nested headers section
    }
}

void DrawDLSSGSummary() {
    // Legacy function - kept for backward compatibility
    // Now shows the old separate header format
    if (ImGui::CollapsingHeader("DLSS/DLSS-G/RR Summary", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        DrawDLSSGSummaryContent();
        ImGui::Unindent();
    }
}

void DrawDLSSGSummaryContent() {
    // Content of the DLSS/DLSS-G/RR Summary section
        ImGui::TextColored(ui::colors::TEXT_INFO, "DLSS/DLSS-G/Ray Reconstruction Status Overview");
        ImGui::Separator();

        DLSSGSummary summary = GetDLSSGSummary();

        // Create a two-column layout for the summary
        ImGui::Columns(2, "DLSSGSummaryColumns", false);
        ImGui::SetColumnWidth(0, 300);  // Label column
        ImGui::SetColumnWidth(1, 350);  // Value column

        // Status indicators
        ImGui::Text("DLSS Active:");
        ImGui::NextColumn();
        ImGui::TextColored(summary.dlss_active ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s",
                           summary.dlss_active ? "Yes" : "No");
        ImGui::NextColumn();

        ImGui::Text("DLSS-G Active:");
        ImGui::NextColumn();
        ImGui::TextColored(summary.dlss_g_active ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
                           "%s", summary.dlss_g_active ? "Yes" : "No");
        ImGui::NextColumn();

        ImGui::Text("Ray Reconstruction:");
        ImGui::NextColumn();
        ImGui::TextColored(
            summary.ray_reconstruction_active ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s",
            summary.ray_reconstruction_active ? "Yes" : "No");
        ImGui::NextColumn();

        ImGui::Text("FG Mode:");
        ImGui::NextColumn();
        // Color code based on FG mode
        ImVec4 fg_color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);  // Default gray
        if (summary.fg_mode == "2x") {
            fg_color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);  // Green for 2x
        } else if (summary.fg_mode == "3x") {
            fg_color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);  // Yellow for 3x
        } else if (summary.fg_mode == "4x") {
            fg_color = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);  // Orange for 4x
        } else if (summary.fg_mode.find("x") != std::string::npos) {
            fg_color = ImVec4(1.0f, 0.0f, 1.0f, 1.0f);  // Magenta for higher modes
        } else if (summary.fg_mode == "Disabled") {
            fg_color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);  // Red for disabled
        } else if (summary.fg_mode == "Unknown") {
            fg_color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);  // Gray for unknown
        }
        ImGui::TextColored(fg_color, "%s", summary.fg_mode.c_str());
        ImGui::NextColumn();

        // DLL Version information
        ImGui::Text("DLSS DLL Version:");
        ImGui::NextColumn();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "%s", summary.dlss_dll_version.c_str());
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), " [%s]", summary.supported_dlss_presets.c_str());
        ImGui::NextColumn();

        ImGui::Text("DLSS-G DLL Version:");
        ImGui::NextColumn();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "%s", summary.dlssg_dll_version.c_str());
        ImGui::NextColumn();

        if (summary.dlssd_dll_version != "Not loaded") {
            ImGui::Text("DLSS-D DLL Version:");
            ImGui::NextColumn();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "%s", summary.dlssd_dll_version.c_str());
            ImGui::NextColumn();
        }

        ImGui::Separator();

        // Resolution information
        ImGui::Text("Internal Resolution:");
        ImGui::NextColumn();
        ImGui::Text("%s", summary.internal_resolution.c_str());
        ImGui::NextColumn();

        ImGui::Text("Output Resolution:");
        ImGui::NextColumn();
        ImGui::Text("%s", summary.output_resolution.c_str());
        ImGui::NextColumn();

        ImGui::Text("Scaling Ratio:");
        ImGui::NextColumn();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "%s", summary.scaling_ratio.c_str());
        ImGui::NextColumn();

        ImGui::Text("Quality Preset:");
        ImGui::NextColumn();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s", summary.quality_preset.c_str());
        ImGui::NextColumn();

        ImGui::Separator();

        // Camera and rendering settings
        ImGui::Text("Aspect Ratio:");
        ImGui::NextColumn();
        ImGui::Text("%s", summary.aspect_ratio.c_str());
        ImGui::NextColumn();

        ImGui::Text("FOV:");
        ImGui::NextColumn();
        ImGui::Text("%s", summary.fov.c_str());
        ImGui::NextColumn();

        ImGui::Text("Jitter Offset:");
        ImGui::NextColumn();
        ImGui::Text("%s", summary.jitter_offset.c_str());
        ImGui::NextColumn();

        ImGui::Text("Exposure:");
        ImGui::NextColumn();
        ImGui::Text("%s", summary.exposure.c_str());
        ImGui::NextColumn();

        ImGui::Text("Sharpness:");
        ImGui::NextColumn();
        ImGui::Text("%s", summary.sharpness.c_str());
        ImGui::NextColumn();

        ImGui::Separator();

        // Technical settings
        ImGui::Text("Depth Inverted:");
        ImGui::NextColumn();
        ImGui::TextColored(
            summary.depth_inverted == "Yes" ? ImVec4(1.0f, 0.5f, 0.0f, 1.0f) : ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s",
            summary.depth_inverted.c_str());
        ImGui::NextColumn();

        ImGui::Text("HDR Enabled:");
        ImGui::NextColumn();
        ImGui::TextColored(
            summary.hdr_enabled == "Yes" ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s",
            summary.hdr_enabled.c_str());
        ImGui::NextColumn();

        ImGui::Text("Motion Vectors:");
        ImGui::NextColumn();
        ImGui::TextColored(
            summary.motion_vectors_included == "Yes" ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
            "%s", summary.motion_vectors_included.c_str());
        ImGui::NextColumn();

        ImGui::Text("Frame Time Delta:");
        ImGui::NextColumn();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "%s", summary.frame_time_delta.c_str());
        ImGui::NextColumn();

        ImGui::Text("Tonemapper Type:");
        ImGui::NextColumn();
        ImGui::Text("%s", summary.tonemapper_type.c_str());
        ImGui::NextColumn();

        ImGui::Text("Optical Flow Accelerator:");
        ImGui::NextColumn();
        ImGui::TextColored(
            summary.ofa_enabled == "Yes" ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s",
            summary.ofa_enabled.c_str());
        ImGui::NextColumn();

        ImGui::Columns(1);  // Reset columns

        // Add some helpful information
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f),
                           "Note: Values update in real-time as the game calls NGX functions");

        if (summary.dlss_g_active) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "DLSS Frame Generation is currently active!");
        }

        if (summary.ray_reconstruction_active) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Ray Reconstruction is currently active!");
        }

        if (summary.ofa_enabled == "Yes") {
            ImGui::TextColored(ui::colors::TEXT_SUCCESS, "NVIDIA Optical Flow Accelerator (OFA) is enabled!");
        }
}

void DrawDxgiCompositionInfo() {
    if (ImGui::CollapsingHeader("DXGI Composition Information", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* mode_str = "Unknown";
        int current_api = g_last_reshade_device_api.load();
        DxgiBypassMode flip_state = GetFlipStateForAPI(current_api);

        switch (flip_state) {
            case DxgiBypassMode::kUnset:                    mode_str = "Unset"; break;
            case DxgiBypassMode::kComposed:                 mode_str = "Composed Flip"; break;
            case DxgiBypassMode::kOverlay:                  mode_str = "Modern Independent Flip"; break;
            case DxgiBypassMode::kIndependentFlip:          mode_str = "Legacy Independent Flip"; break;
            case DxgiBypassMode::kQueryFailedSwapchainNull: mode_str = "Query Failed: Swapchain Null"; break;
            case DxgiBypassMode::kQueryFailedNoMedia:       mode_str = "Query Failed: No Media Interface"; break;
            case DxgiBypassMode::kQueryFailedNoSwapchain1:  mode_str = "Query Failed: No Swapchain1"; break;
            case DxgiBypassMode::kQueryFailedNoStats:       mode_str = "Query Failed: No Statistics"; break;
            case DxgiBypassMode::kUnknown:
            default:                                        mode_str = "Unknown"; break;
        }

        // Get backbuffer format
        std::string format_str = "Unknown";

        // Get colorspace string directly from swapchain
        std::string colorspace_str = "Unknown";
        if (auto* swapchain_ptr = g_last_swapchain_ptr_unsafe.load()) {
            auto* swapchain = static_cast<reshade::api::swapchain*>(swapchain_ptr);
            auto colorspace = swapchain->get_color_space();
            switch (colorspace) {
                case reshade::api::color_space::unknown:              colorspace_str = "Unknown"; break;
                case reshade::api::color_space::srgb_nonlinear:       colorspace_str = "sRGB"; break;
                case reshade::api::color_space::extended_srgb_linear: colorspace_str = "Extended sRGB Linear"; break;
                case reshade::api::color_space::hdr10_st2084:         colorspace_str = "HDR10 ST2084"; break;
                case reshade::api::color_space::hdr10_hlg:            colorspace_str = "HDR10 HLG"; break;
                default:                                              colorspace_str = "ColorSpace_" + std::to_string(static_cast<int>(colorspace)); break;
            }
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

void DrawSwapchainInfo(reshade::api::effect_runtime* runtime) {
    auto api = runtime->get_device()->get_api();
    auto native = runtime->get_native();

    Microsoft::WRL::ComPtr<IDXGISwapChain> dxgi_swapchain0 = nullptr;
    if (api == reshade::api::device_api::d3d10) {
        auto *id3d10device = reinterpret_cast<ID3D10Device *>(native);
        id3d10device->QueryInterface(IID_PPV_ARGS(&dxgi_swapchain0));
    }
    else if (api == reshade::api::device_api::d3d11) {
        auto *id3d11device = reinterpret_cast<ID3D11Device *>(native);
        id3d11device->QueryInterface(IID_PPV_ARGS(&dxgi_swapchain0));
    }
    else if (api == reshade::api::device_api::d3d12) {
        auto *id3d12device = reinterpret_cast<ID3D12Device *>(native);
        id3d12device->QueryInterface(IID_PPV_ARGS(&dxgi_swapchain0));
    }
    Microsoft::WRL::ComPtr<IDXGISwapChain1> dxgi_swapchain = nullptr;

    if (dxgi_swapchain0 != nullptr) {
        dxgi_swapchain0->QueryInterface(IID_PPV_ARGS(&dxgi_swapchain));
    }


    // todo replace g_last_swapchain_ptr_unsafe with runtime->get_device()->get_native()
    // runtime->get_device()->get_native();

    // ReShade Runtimes Section (see docs/UI_STYLE_GUIDE.md for depth/indent rules)
    // Depth 0: Main section header
    if (ImGui::CollapsingHeader("ReShade Runtimes", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextColored(ui::colors::TEXT_DEFAULT, "ReShade runtimes count: %zu", g_reshade_runtimes.size());
        ImGui::Separator();

        // Depth 1: Nested subsections with indentation and distinct colors
        ImGui::Indent();  // Indent nested headers
        for (size_t i = 0; i < g_reshade_runtimes.size(); ++i) {
            auto* runtime = g_reshade_runtimes[i];

            // Create a collapsible header for each runtime
            std::stringstream ss;
            ss << "Runtime " << i << " (0x" << std::hex << std::uppercase << reinterpret_cast<uintptr_t>(runtime)
               << ")";
            std::string runtime_header = ss.str();
            ui::colors::PushNestedHeaderColors();  // Apply distinct colors for nested headers
            if (ImGui::CollapsingHeader(runtime_header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();  // Indent content inside subsection

                // Basic runtime info
                ImGui::Text("Address: 0x%p", runtime);
                ImGui::Text("HWND: 0x%p", runtime->get_hwnd());
                ImGui::Text("Command Queue: 0x%p", runtime->get_command_queue());
                ImGui::Text("Device: 0x%p", runtime->get_device());

                // Backbuffer count and current index
                uint32_t back_buffer_count = runtime->get_back_buffer_count();
                uint32_t current_back_buffer_index = runtime->get_current_back_buffer_index();
                ImGui::Text("Back Buffer Count: %d", back_buffer_count);
                ImGui::Text("Current Back Buffer Index: %d", current_back_buffer_index);

                // Detailed backbuffer information
                if (back_buffer_count > 0) {
                    ImGui::Separator();
                    ImGui::Text("Backbuffer Details:");

                    // Get device for resource queries
                    auto* device = runtime->get_device();
                    if (device != nullptr) {
                        for (uint32_t j = 0; j < back_buffer_count; ++j) {
                            try {
                                auto back_buffer = runtime->get_back_buffer(j);
                                if (back_buffer.handle != 0) {
                                    auto desc = device->get_resource_desc(back_buffer);

                                    std::stringstream buffer_ss;
                                    buffer_ss << "Backbuffer " << j << " (0x" << std::hex << std::uppercase
                                              << back_buffer.handle << ")";
                                    std::string buffer_header = buffer_ss.str();
                                    ui::colors::PushNestedHeaderColors();  // Apply distinct colors for nested backbuffer headers
                                    if (ImGui::CollapsingHeader(buffer_header.c_str())) {
                                        ImGui::Indent();

                                        // Resource type
                                        const char* type_str = "Unknown";
                                        switch (desc.type) {
                                            case reshade::api::resource_type::buffer: type_str = "Buffer"; break;
                                            case reshade::api::resource_type::texture_1d:
                                                type_str = "Texture 1D";
                                                break;
                                            case reshade::api::resource_type::texture_2d:
                                                type_str = "Texture 2D";
                                                break;
                                            case reshade::api::resource_type::texture_3d:
                                                type_str = "Texture 3D";
                                                break;
                                            case reshade::api::resource_type::surface: type_str = "Surface"; break;
                                            default:                                   break;
                                        }
                                        ImGui::Text("Type: %s", type_str);

                                        // Texture dimensions and properties
                                        if (desc.type == reshade::api::resource_type::texture_2d
                                            || desc.type == reshade::api::resource_type::texture_3d
                                            || desc.type == reshade::api::resource_type::surface) {
                                            ImGui::Text("Dimensions: %dx%d", desc.texture.width, desc.texture.height);
                                            ImGui::Text("Depth/Layers: %d", desc.texture.depth_or_layers);
                                            ImGui::Text("Mip Levels: %d", desc.texture.levels);
                                            ImGui::Text("Samples: %d", desc.texture.samples);

                                            // Format information
                                            const char* format_str = "Unknown";
                                            switch (desc.texture.format) {
                                                case reshade::api::format::r8g8b8a8_unorm:
                                                    format_str = "R8G8B8A8_UNORM";
                                                    break;
                                                case reshade::api::format::r8g8b8a8_unorm_srgb:
                                                    format_str = "R8G8B8A8_UNORM_SRGB";
                                                    break;
                                                case reshade::api::format::b8g8r8a8_unorm:
                                                    format_str = "B8G8R8A8_UNORM";
                                                    break;
                                                case reshade::api::format::b8g8r8a8_unorm_srgb:
                                                    format_str = "B8G8R8A8_UNORM_SRGB";
                                                    break;
                                                case reshade::api::format::r10g10b10a2_unorm:
                                                    format_str = "R10G10B10A2_UNORM";
                                                    break;
                                                case reshade::api::format::r16g16b16a16_float:
                                                    format_str = "R16G16B16A16_FLOAT";
                                                    break;
                                                case reshade::api::format::r32g32b32a32_float:
                                                    format_str = "R32G32B32A32_FLOAT";
                                                    break;
                                                case reshade::api::format::r11g11b10_float:
                                                    format_str = "R11G11B10_FLOAT";
                                                    break;
                                                case reshade::api::format::r16g16b16a16_unorm:
                                                    format_str = "R16G16B16A16_UNORM";
                                                    break;
                                                case reshade::api::format::r16g16b16a16_snorm:
                                                    format_str = "R16G16B16A16_SNORM";
                                                    break;
                                                case reshade::api::format::r32g32b32a32_uint:
                                                    format_str = "R32G32B32A32_UINT";
                                                    break;
                                                case reshade::api::format::r32g32b32a32_sint:
                                                    format_str = "R32G32B32A32_SINT";
                                                    break;
                                                case reshade::api::format::d24_unorm_s8_uint:
                                                    format_str = "D24_UNORM_S8_UINT";
                                                    break;
                                                case reshade::api::format::d32_float: format_str = "D32_FLOAT"; break;
                                                case reshade::api::format::d32_float_s8_uint:
                                                    format_str = "D32_FLOAT_S8_UINT";
                                                    break;
                                                case reshade::api::format::bc1_unorm: format_str = "BC1_UNORM"; break;
                                                case reshade::api::format::bc1_unorm_srgb:
                                                    format_str = "BC1_UNORM_SRGB";
                                                    break;
                                                case reshade::api::format::bc2_unorm: format_str = "BC2_UNORM"; break;
                                                case reshade::api::format::bc2_unorm_srgb:
                                                    format_str = "BC2_UNORM_SRGB";
                                                    break;
                                                case reshade::api::format::bc3_unorm: format_str = "BC3_UNORM"; break;
                                                case reshade::api::format::bc3_unorm_srgb:
                                                    format_str = "BC3_UNORM_SRGB";
                                                    break;
                                                case reshade::api::format::bc4_unorm: format_str = "BC4_UNORM"; break;
                                                case reshade::api::format::bc4_snorm: format_str = "BC4_SNORM"; break;
                                                case reshade::api::format::bc5_unorm: format_str = "BC5_UNORM"; break;
                                                case reshade::api::format::bc5_snorm: format_str = "BC5_SNORM"; break;
                                                case reshade::api::format::bc6h_ufloat:
                                                    format_str = "BC6H_UFLOAT";
                                                    break;
                                                case reshade::api::format::bc6h_sfloat:
                                                    format_str = "BC6H_SFLOAT";
                                                    break;
                                                case reshade::api::format::bc7_unorm: format_str = "BC7_UNORM"; break;
                                                case reshade::api::format::bc7_unorm_srgb:
                                                    format_str = "BC7_UNORM_SRGB";
                                                    break;
                                                default: format_str = "Custom/Unknown"; break;
                                            }
                                            ImGui::Text("Format: %s", format_str);
                                        }

                                        // Buffer information
                                        if (desc.type == reshade::api::resource_type::buffer) {
                                            ImGui::Text("Size: %llu bytes", desc.buffer.size);
                                            ImGui::Text("Stride: %u bytes", desc.buffer.stride);
                                        }

                                        // Memory heap information
                                        const char* heap_str = "Unknown";
                                        switch (desc.heap) {
                                            case reshade::api::memory_heap::unknown:    heap_str = "Unknown"; break;
                                            case reshade::api::memory_heap::gpu_only:   heap_str = "GPU Only"; break;
                                            case reshade::api::memory_heap::cpu_to_gpu: heap_str = "CPU to GPU"; break;
                                            case reshade::api::memory_heap::gpu_to_cpu: heap_str = "GPU to CPU"; break;
                                            case reshade::api::memory_heap::cpu_only:   heap_str = "CPU Only"; break;
                                            case reshade::api::memory_heap::custom:     heap_str = "Custom"; break;
                                        }
                                        ImGui::Text("Memory Heap: %s", heap_str);

                                        // Usage flags
                                        std::string usage_flags;
                                        if ((desc.usage & reshade::api::resource_usage::render_target)
                                            != reshade::api::resource_usage::undefined)
                                            usage_flags += "RenderTarget ";
                                        if ((desc.usage & reshade::api::resource_usage::depth_stencil)
                                            != reshade::api::resource_usage::undefined)
                                            usage_flags += "DepthStencil ";
                                        if ((desc.usage & reshade::api::resource_usage::shader_resource)
                                            != reshade::api::resource_usage::undefined)
                                            usage_flags += "ShaderResource ";
                                        if ((desc.usage & reshade::api::resource_usage::copy_source)
                                            != reshade::api::resource_usage::undefined)
                                            usage_flags += "CopySource ";
                                        if ((desc.usage & reshade::api::resource_usage::copy_dest)
                                            != reshade::api::resource_usage::undefined)
                                            usage_flags += "CopyDest ";
                                        if ((desc.usage & reshade::api::resource_usage::resolve_source)
                                            != reshade::api::resource_usage::undefined)
                                            usage_flags += "ResolveSource ";
                                        if ((desc.usage & reshade::api::resource_usage::resolve_dest)
                                            != reshade::api::resource_usage::undefined)
                                            usage_flags += "ResolveDest ";
                                        if ((desc.usage & reshade::api::resource_usage::present)
                                            != reshade::api::resource_usage::undefined)
                                            usage_flags += "Present ";
                                        if ((desc.usage & reshade::api::resource_usage::acceleration_structure)
                                            != reshade::api::resource_usage::undefined)
                                            usage_flags += "AccelerationStructure ";
                                        if ((desc.usage & reshade::api::resource_usage::unordered_access)
                                            != reshade::api::resource_usage::undefined)
                                            usage_flags += "UnorderedAccess ";

                                        if (usage_flags.empty()) {
                                            usage_flags = "None";
                                        } else {
                                            usage_flags.pop_back();  // Remove trailing space
                                        }
                                        ImGui::Text("Usage: %s", usage_flags.c_str());

                                        // Resource flags
                                        std::string flag_str;
                                        if ((desc.flags & reshade::api::resource_flags::shared)
                                            != reshade::api::resource_flags::none)
                                            flag_str += "Shared ";
                                        if ((desc.flags & reshade::api::resource_flags::shared_nt_handle)
                                            != reshade::api::resource_flags::none)
                                            flag_str += "SharedNTHandle ";
                                        if ((desc.flags & reshade::api::resource_flags::cube_compatible)
                                            != reshade::api::resource_flags::none)
                                            flag_str += "CubeCompatible ";
                                        if ((desc.flags & reshade::api::resource_flags::sparse_binding)
                                            != reshade::api::resource_flags::none)
                                            flag_str += "SparseBinding ";
                                        if ((desc.flags & reshade::api::resource_flags::generate_mipmaps)
                                            != reshade::api::resource_flags::none)
                                            flag_str += "GenerateMipmaps ";
                                        if ((desc.flags & reshade::api::resource_flags::dynamic)
                                            != reshade::api::resource_flags::none)
                                            flag_str += "Dynamic ";

                                        if (flag_str.empty()) {
                                            flag_str = "None";
                                        } else {
                                            flag_str.pop_back();  // Remove trailing space
                                        }
                                        ImGui::Text("Flags: %s", flag_str.c_str());

                                        // Current buffer indicator
                                        if (j == current_back_buffer_index) {
                                            ImGui::TextColored(ui::colors::TEXT_SUCCESS,
                                                               "*** CURRENT BUFFER ***");
                                        }

                                        ImGui::Unindent();
                                    }
                                    ui::colors::PopNestedHeaderColors();  // Restore default header colors
                                }
                            } catch (...) {
                                ImGui::TextColored(ui::colors::TEXT_ERROR,
                                                   "Backbuffer %d: Error querying resource", j);
                            }
                        }
                    } else {
                        ImGui::TextColored(ui::colors::TEXT_ERROR, "Device not available for resource queries");
                    }
                }

                ImGui::Unindent();  // Unindent content
            }
            ui::colors::PopNestedHeaderColors();  // Restore default header colors
        }
        ImGui::Unindent();  // Unindent nested headers section
    }

    // Window Information section
    if (ImGui::CollapsingHeader("Window Information", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Get current foreground window
        HWND foreground_window = display_commanderhooks::GetForegroundWindow_Direct();
        HWND current_hwnd = g_last_swapchain_hwnd.load();

        ImGui::Text("Current Foreground Window: 0x%p", foreground_window);
        ImGui::Text("Swapchain Window: 0x%p", current_hwnd);

        // Check if current window is foreground
        bool is_foreground = (foreground_window == current_hwnd);
        ImGui::Text("Is Current Window Foreground: %s", is_foreground ? "Yes" : "No");

        // Input blocking status
        bool mouse_blocked = display_commanderhooks::ShouldBlockMouseInput();
        bool keyboard_blocked = display_commanderhooks::ShouldBlockKeyboardInput();
        bool toggle_active = s_input_blocking_toggle.load();

        ImGui::Separator();
        ImGui::Text("Input Blocking Status:");
        ImGui::Text("  Ctrl+I Toggle Active: %s", toggle_active ? "Yes" : "No");
        ImGui::Text("  Mouse Input Blocked: %s", mouse_blocked ? "Yes" : "No");
        ImGui::Text("  Keyboard Input Blocked: %s", keyboard_blocked ? "Yes" : "No");
    }

    auto last_api = g_last_reshade_device_api.load();
    auto is_dxgi = last_api == static_cast<int>(reshade::api::device_api::d3d11)
                   || last_api == static_cast<int>(reshade::api::device_api::d3d12);
    if (dxgi_swapchain == nullptr) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No active swapchain detected");
        return;
    }
    if (is_dxgi && ImGui::CollapsingHeader("Swapchain Information", ImGuiTreeNodeFlags_DefaultOpen) ) {
        // warning this tab may crash

        // Get the current swapchain from global variable


        // Get swapchain description
        DXGI_SWAP_CHAIN_DESC1 desc1 = {};
        HRESULT hr = dxgi_swapchain->GetDesc1(&desc1);
        if (FAILED(hr)) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Failed to get swapchain description: 0x%08lX",
                               static_cast<unsigned long>(hr));
            return;
        }

        // Display basic swapchain information
        ImGui::Text("Swapchain Description:");
        ImGui::Text("  Width: %u", desc1.Width);
        ImGui::Text("  Height: %u", desc1.Height);
        ImGui::Text("  Format: %s", GetDXGIFormatString(desc1.Format));
        ImGui::Text("  Stereo: %s", (desc1.Stereo != FALSE) ? "Yes" : "No");
        ImGui::Text("  Sample Count: %u", desc1.SampleDesc.Count);
        ImGui::Text("  Sample Quality: %u", desc1.SampleDesc.Quality);
        ImGui::Text("  Buffer Usage: 0x%08X", desc1.BufferUsage);
        ImGui::Text("  Buffer Count: %u", desc1.BufferCount);
        ImGui::Text("  Scaling: %s", GetDXGIScalingString(desc1.Scaling));
        ImGui::Text("  Swap Effect: %s", GetDXGISwapEffectString(desc1.SwapEffect));
        ImGui::Text("  Alpha Mode: %s", GetDXGIAlphaModeString(desc1.AlphaMode));
        ImGui::Text("  Flags: 0x%08X", desc1.Flags);

        ImGui::Spacing();

        // Try to get output information for HDR details
        Microsoft::WRL::ComPtr<IDXGIOutput> output;
        if (SUCCEEDED(dxgi_swapchain->GetContainingOutput(&output)) && output != nullptr) {
            Microsoft::WRL::ComPtr<IDXGIOutput6> output6;
            if (SUCCEEDED(output->QueryInterface(IID_PPV_ARGS(&output6)))) {
                DXGI_OUTPUT_DESC1 output_desc = {};
                if (SUCCEEDED(output6->GetDesc1(&output_desc))) {
                    ImGui::Text("Output HDR Information:");
                    ImGui::Text("  Device Name: %S", output_desc.DeviceName);
                    ImGui::Text("  Color Space: %s", GetDXGIColorSpaceString(output_desc.ColorSpace));
                    ImGui::Text("  Bits Per Color: %u", output_desc.BitsPerColor);
                    ImGui::Text("  Min Luminance: %.2f nits", output_desc.MinLuminance);
                    ImGui::Text("  Max Luminance: %.2f nits", output_desc.MaxLuminance);
                    ImGui::Text("  Max Full Frame Luminance: %.2f nits", output_desc.MaxFullFrameLuminance);

                    // Color primaries
                    ImGui::Text("  Red Primary: (%.3f, %.3f)", output_desc.RedPrimary[0], output_desc.RedPrimary[1]);
                    ImGui::Text("  Green Primary: (%.3f, %.3f)", output_desc.GreenPrimary[0],
                                output_desc.GreenPrimary[1]);
                    ImGui::Text("  Blue Primary: (%.3f, %.3f)", output_desc.BluePrimary[0], output_desc.BluePrimary[1]);
                    ImGui::Text("  White Point: (%.3f, %.3f)", output_desc.WhitePoint[0], output_desc.WhitePoint[1]);

                    // HDR support detection
                    bool supports_hdr = (output_desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020
                                         || output_desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
                    ImGui::Text("  HDR Support: %s", supports_hdr ? "Yes" : "No");

                    if (supports_hdr) {
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
                                           "  " ICON_FK_OK " HDR-capable display detected");
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                                           "  " ICON_FK_WARNING " Display does not support HDR");
                    }

                    // VRR support detection using CheckHardwareCompositionSupport
                    UINT support_flags = 0;
                    bool supports_vrr = false;
                    if (SUCCEEDED(output6->CheckHardwareCompositionSupport(&support_flags))) {
                        // Check for VRR support flag (0x1 =
                        // DXGI_HARDWARE_COMPOSITION_SUPPORT_FLAG_VARIABLE_REFRESH_RATE)
                        supports_vrr = (support_flags & 0x1) != 0;
                    }
                    ImGui::Text("  VRR Support: %s", supports_vrr ? "Yes" : "No");

                    if (supports_vrr) {
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
                                           "  " ICON_FK_OK
                                           " Variable Refresh Rate (VRR) supported (WIP - not implemented yet)");
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                                           "  " ICON_FK_WARNING " Display does not support VRR");
                    }
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Failed to get output description");
                }
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "IDXGIOutput6 not available");
            }
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Failed to get containing output");
        }

        ImGui::Spacing();

        // Try to get HDR metadata information from swapchain
        Microsoft::WRL::ComPtr<IDXGISwapChain4> swapchain4;
        if (SUCCEEDED(dxgi_swapchain->QueryInterface(IID_PPV_ARGS(&swapchain4)))) {
            ImGui::Text("HDR Metadata Support:");
            ImGui::Text("  IDXGISwapChain4: Available");
            ImGui::Text("  SetHDRMetaData: Supported");

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "HDR Metadata Controls:");
            ImGui::Separator();

            // Auto-apply HDR metadata checkbox
            if (ImGui::Checkbox("Auto-apply HDR metadata", &auto_apply_hdr_metadata)) {
                // Save the setting to config when changed
                display_commander::config::set_config_value("ReShade_HDR_Metadata", "auto_apply_hdr_metadata",
                                                            auto_apply_hdr_metadata);
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "?");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Automatically apply HDR metadata when swapchain is created (not implemented yet)");
            }

            // Set to Rec. 2020 defaults button
            if (ImGui::Button("Set to Rec. 2020 defaults")) {
                // Set HDR10 metadata to Rec. 2020 default values
                DXGI_HDR_METADATA_HDR10 hdr10_metadata = {};
                hdr10_metadata.RedPrimary[0] = 0.708 * 65535;    //(Rec. 2020 red x)
                hdr10_metadata.RedPrimary[1] = 0.292 * 65535;    //(Rec. 2020 red y)
                hdr10_metadata.GreenPrimary[0] = 0.170 * 65535;  //(Rec. 2020 green x)
                hdr10_metadata.GreenPrimary[1] = 0.797 * 65535;  //(Rec. 2020 green y)
                hdr10_metadata.BluePrimary[0] = 0.131 * 65535;   //(Rec. 2020 blue x)
                hdr10_metadata.BluePrimary[1] = 0.046 * 65535;   //(Rec. 2020 blue y)
                hdr10_metadata.WhitePoint[0] = 0.3127 * 65535;   //(D65 white x)
                hdr10_metadata.WhitePoint[1] = 0.3290 * 65535;   //(D65 white y)
                hdr10_metadata.MaxMasteringLuminance = 1000;     // 1000 nits
                hdr10_metadata.MinMasteringLuminance = 0;        // 0 nits
                hdr10_metadata.MaxContentLightLevel = 1000;      // 1000 nits
                hdr10_metadata.MaxFrameAverageLightLevel = 100;  // 100 nits

                HRESULT hr =
                    swapchain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(hdr10_metadata), &hdr10_metadata);
                if (SUCCEEDED(hr)) {
                    // Store the metadata for display
                    last_hdr_metadata = hdr10_metadata;
                    dirty_last_metadata = hdr10_metadata;
                    has_last_metadata = true;
                    last_metadata_source = "Set HDR Metadata (Rec. 2020 defaults)";

                    // Save HDR metadata to DisplayCommander config
                    display_commander::config::set_config_value("ReShade_HDR_Metadata", "prim_red_x", 0.708);
                    display_commander::config::set_config_value("ReShade_HDR_Metadata", "prim_red_y", 0.292);
                    display_commander::config::set_config_value("ReShade_HDR_Metadata", "prim_green_x", 0.170);
                    display_commander::config::set_config_value("ReShade_HDR_Metadata", "prim_green_y", 0.797);
                    display_commander::config::set_config_value("ReShade_HDR_Metadata", "prim_blue_x", 0.131);
                    display_commander::config::set_config_value("ReShade_HDR_Metadata", "prim_blue_y", 0.046);
                    display_commander::config::set_config_value("ReShade_HDR_Metadata", "white_point_x", 0.3127);
                    display_commander::config::set_config_value("ReShade_HDR_Metadata", "white_point_y", 0.3290);
                    display_commander::config::set_config_value("ReShade_HDR_Metadata", "max_mdl", 1000);
                    display_commander::config::set_config_value("ReShade_HDR_Metadata", "min_mdl", 0.0f);
                    display_commander::config::set_config_value("ReShade_HDR_Metadata", "max_cll", 1000);
                    display_commander::config::set_config_value("ReShade_HDR_Metadata", "max_fall", 100);
                    display_commander::config::set_config_value("ReShade_HDR_Metadata", "has_last_metadata", true);

                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
                                       ICON_FK_OK " HDR metadata set to Rec. 2020 defaults and saved to config");
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "✗ Failed to clear HDR metadata: 0x%08lX",
                                       static_cast<unsigned long>(hr));
                }
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "?");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Set HDR metadata to Rec. 2020 color primaries with standard luminance values");
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "?");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Set HDR metadata to match the monitor's color primaries and luminance range");
            }

            // Disable HDR metadata button
            if (ImGui::Button("Clear HDR Metadata")) {
                // Disable HDR metadata by setting it to NULL
                HRESULT hr = swapchain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr);
                if (SUCCEEDED(hr)) {
                    // Clear the stored metadata since it's disabled
                    has_last_metadata = false;
                    display_commander::config::set_config_value("ReShade_HDR_Metadata", "has_last_metadata", false);
                    last_metadata_source = "Disabled";
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), ICON_FK_OK " HDR metadata disabled");
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "✗ Failed to disable HDR metadata: 0x%08lX",
                                       static_cast<unsigned long>(hr));
                }
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "?");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Disable HDR metadata completely (set to DXGI_HDR_METADATA_TYPE_NONE)");
            }

            ImGui::Spacing();

            // MaxMDL (Maximum Mastering Display Luminance) control
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "HDR Metadata Controls:");
            ImGui::Separator();

            int max_mdl_value = dirty_last_metadata.MaxMasteringLuminance;
            if (ImGui::InputInt("MaxMDL (nits)", &max_mdl_value, 0, 0, ImGuiInputTextFlags_CharsDecimal)) {
                dirty_last_metadata.MaxMasteringLuminance = static_cast<UINT>(max_mdl_value);
            }

            // Allow any value, no clamping

            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "?");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Maximum Mastering Display Luminance in nits.\nThis controls the peak brightness of the mastering "
                    "display.");
            }

            int min_mdl_value = dirty_last_metadata.MinMasteringLuminance;
            if (ImGui::InputInt("MinMDL (nits)", &min_mdl_value, 0, 0, ImGuiInputTextFlags_CharsDecimal)) {
                dirty_last_metadata.MinMasteringLuminance = static_cast<UINT>(min_mdl_value);
                // Allow any value, no clamping
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "?");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Minimum Mastering Display Luminance in nits.\nThis controls the minimum brightness of the "
                    "mastering display.");
            }

            int max_content_light_level = dirty_last_metadata.MaxContentLightLevel;
            if (ImGui::InputInt("Max Content Light Level (nits)", &max_content_light_level, 0, 0,
                                ImGuiInputTextFlags_CharsDecimal)) {
                dirty_last_metadata.MaxContentLightLevel = static_cast<UINT>(max_content_light_level);
                // Allow any value, no clamping
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "?");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Maximum Content Light Level in nits.\nThis controls the peak brightness of the content.");
            }

            int max_frame_avg_light_level = dirty_last_metadata.MaxFrameAverageLightLevel;
            if (ImGui::InputInt("Max Frame Avg Light Level (nits)", &max_frame_avg_light_level, 0, 0,
                                ImGuiInputTextFlags_CharsDecimal)) {
                dirty_last_metadata.MaxFrameAverageLightLevel = static_cast<UINT>(max_frame_avg_light_level);
                // Allow any value, no clamping
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "?");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Maximum Frame Average Light Level in nits.\nThis controls the average brightness of the content.");
            }

            if (ImGui::Button("Apply All HDR Values")) {
                // Create HDR10 metadata with all custom values
                DXGI_HDR_METADATA_HDR10 hdr10_metadata = {};

                // Use current metadata values if available, otherwise use defaults
                if (has_last_metadata) {
                    last_hdr_metadata = dirty_last_metadata;
                    hdr10_metadata = last_hdr_metadata;
                } else {
                    // Default Rec. 709 values
                    hdr10_metadata.RedPrimary[0] = 32768;    // 0.5 * 65535
                    hdr10_metadata.RedPrimary[1] = 21634;    // 0.33 * 65535
                    hdr10_metadata.GreenPrimary[0] = 19661;  // 0.3 * 65535
                    hdr10_metadata.GreenPrimary[1] = 39321;  // 0.6 * 65535
                    hdr10_metadata.BluePrimary[0] = 9830;    // 0.15 * 65535
                    hdr10_metadata.BluePrimary[1] = 3932;    // 0.06 * 65535
                    hdr10_metadata.WhitePoint[0] = 20493;    // 0.3127 * 65535
                    hdr10_metadata.WhitePoint[1] = 21564;    // 0.3290 * 65535
                }

                // Set all the custom values
                hdr10_metadata.MaxMasteringLuminance = static_cast<UINT>(max_mdl_value);
                hdr10_metadata.MinMasteringLuminance = static_cast<UINT>(min_mdl_value);
                hdr10_metadata.MaxContentLightLevel = static_cast<UINT>(max_content_light_level);
                hdr10_metadata.MaxFrameAverageLightLevel = static_cast<UINT>(max_frame_avg_light_level);

                HRESULT hr =
                    swapchain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(hdr10_metadata), &hdr10_metadata);
                if (SUCCEEDED(hr)) {
                    // Store the metadata for display
                    last_hdr_metadata = hdr10_metadata;
                    has_last_metadata = true;
                    last_metadata_source = "Custom HDR Values";

                    // Save HDR metadata to DisplayCommander config
                    display_commander::config::set_config_value("ReShade_HDR_Metadata", "prim_red_x",
                                                                hdr10_metadata.RedPrimary[0] / 65535.0);
                    display_commander::config::set_config_value("ReShade_HDR_Metadata", "prim_red_y",
                                                                hdr10_metadata.RedPrimary[1] / 65535.0);
                    display_commander::config::set_config_value("ReShade_HDR_Metadata", "prim_green_x",
                                                                hdr10_metadata.GreenPrimary[0] / 65535.0);
                    display_commander::config::set_config_value("ReShade_HDR_Metadata", "prim_green_y",
                                                                hdr10_metadata.GreenPrimary[1] / 65535.0);
                    display_commander::config::set_config_value("ReShade_HDR_Metadata", "prim_blue_x",
                                                                hdr10_metadata.BluePrimary[0] / 65535.0);
                    display_commander::config::set_config_value("ReShade_HDR_Metadata", "prim_blue_y",
                                                                hdr10_metadata.BluePrimary[1] / 65535.0);
                    display_commander::config::set_config_value("ReShade_HDR_Metadata", "white_point_x",
                                                                hdr10_metadata.WhitePoint[0] / 65535.0);
                    display_commander::config::set_config_value("ReShade_HDR_Metadata", "white_point_y",
                                                                hdr10_metadata.WhitePoint[1] / 65535.0);
                    display_commander::config::set_config_value(
                        "ReShade_HDR_Metadata", "max_mdl", static_cast<int32_t>(hdr10_metadata.MaxMasteringLuminance));
                    display_commander::config::set_config_value("ReShade_HDR_Metadata", "min_mdl",
                                                                hdr10_metadata.MinMasteringLuminance / 10000.0f);
                    display_commander::config::set_config_value(
                        "ReShade_HDR_Metadata", "max_cll", static_cast<int32_t>(hdr10_metadata.MaxContentLightLevel));
                    display_commander::config::set_config_value(
                        "ReShade_HDR_Metadata", "max_fall",
                        static_cast<int32_t>(hdr10_metadata.MaxFrameAverageLightLevel));
                    display_commander::config::set_config_value("ReShade_HDR_Metadata", "has_last_metadata", true);

                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
                                       ICON_FK_OK " All HDR values applied and saved to config");
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "✗ Failed to apply HDR values: 0x%08lX",
                                       static_cast<unsigned long>(hr));
                }
            }

            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "?");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Apply all the HDR metadata values above to the swapchain.");
            }

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f),
                               "Note: These controls directly call SetHDRMetaData on the current swapchain.");
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f),
                               "Changes will be visible in the next frame and may affect HDR rendering.");

            // Display last set HDR metadata values
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Last Set HDR Metadata Values:");
            ImGui::Separator();

            // Display the last set HDR metadata values (tracked by our button handlers)

            if (has_last_metadata) {
                ImGui::Text("Source: %s", last_metadata_source.c_str());
                ImGui::Spacing();

                // Color primaries
                ImGui::Text("Color Primaries:");
                ImGui::Text("  Red:   (%.4f, %.4f)", last_hdr_metadata.RedPrimary[0] / 65535.0f,
                            last_hdr_metadata.RedPrimary[1] / 65535.0f);
                ImGui::Text("  Green: (%.4f, %.4f)", last_hdr_metadata.GreenPrimary[0] / 65535.0f,
                            last_hdr_metadata.GreenPrimary[1] / 65535.0f);
                ImGui::Text("  Blue:  (%.4f, %.4f)", last_hdr_metadata.BluePrimary[0] / 65535.0f,
                            last_hdr_metadata.BluePrimary[1] / 65535.0f);
                ImGui::Text("  White: (%.4f, %.4f)", last_hdr_metadata.WhitePoint[0] / 65535.0f,
                            last_hdr_metadata.WhitePoint[1] / 65535.0f);

                ImGui::Spacing();

                // Luminance values
                ImGui::Text("Luminance Range:");
                ImGui::Text("  Max Mastering: %u nits", last_hdr_metadata.MaxMasteringLuminance);
                ImGui::Text("  Min Mastering: %u nits", last_hdr_metadata.MinMasteringLuminance);
                ImGui::Text("  Max Content Light Level: %u nits", last_hdr_metadata.MaxContentLightLevel);
                ImGui::Text("  Max Frame Average Light Level: %u nits", last_hdr_metadata.MaxFrameAverageLightLevel);

                // Color space interpretation
                ImGui::Spacing();
                ImGui::Text("Color Space Interpretation:");
                float red_x = last_hdr_metadata.RedPrimary[0] / 65535.0f;
                float red_y = last_hdr_metadata.RedPrimary[1] / 65535.0f;
                float green_x = last_hdr_metadata.GreenPrimary[0] / 65535.0f;
                float green_y = last_hdr_metadata.GreenPrimary[1] / 65535.0f;
                float blue_x = last_hdr_metadata.BluePrimary[0] / 65535.0f;
                float blue_y = last_hdr_metadata.BluePrimary[1] / 65535.0f;

                // Check if it matches common color spaces
                bool is_rec709 =
                    (abs(red_x - 0.64f) < 0.01f && abs(red_y - 0.33f) < 0.01f && abs(green_x - 0.30f) < 0.01f
                     && abs(green_y - 0.60f) < 0.01f && abs(blue_x - 0.15f) < 0.01f && abs(blue_y - 0.06f) < 0.01f);

                bool is_rec2020 =
                    (abs(red_x - 0.708f) < 0.01f && abs(red_y - 0.292f) < 0.01f && abs(green_x - 0.170f) < 0.01f
                     && abs(green_y - 0.797f) < 0.01f && abs(blue_x - 0.131f) < 0.01f && abs(blue_y - 0.046f) < 0.01f);

                if (is_rec709) {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "  " ICON_FK_OK " Matches Rec. 709 color space");
                } else if (is_rec2020) {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
                                       "  " ICON_FK_OK " Matches Rec. 2020 color space");
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                                       "  " ICON_FK_WARNING " Custom color space (not Rec. 709/2020)");
                }
            } else {
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "No HDR metadata has been set yet.");
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Use the buttons above to set HDR metadata values.");
            }

        } else {
            ImGui::Text("HDR Metadata Support:");
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "  IDXGISwapChain4: Not available");
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "  SetHDRMetaData: Not supported");
        }
    }
}

// Helper functions for string conversion
const char* GetDXGIFormatString(DXGI_FORMAT format) {
    switch (format) {
        case DXGI_FORMAT_R8G8B8A8_UNORM:      return "R8G8B8A8_UNORM";
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return "R8G8B8A8_UNORM_SRGB";
        case DXGI_FORMAT_B8G8R8A8_UNORM:      return "B8G8R8A8_UNORM";
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return "B8G8R8A8_UNORM_SRGB";
        case DXGI_FORMAT_R10G10B10A2_UNORM:   return "R10G10B10A2_UNORM";
        case DXGI_FORMAT_R16G16B16A16_FLOAT:  return "R16G16B16A16_FLOAT";
        case DXGI_FORMAT_R32G32B32A32_FLOAT:  return "R32G32B32A32_FLOAT";
        default:                              return "Unknown Format";
    }
}

const char* GetDXGIScalingString(DXGI_SCALING scaling) {
    switch (scaling) {
        case DXGI_SCALING_STRETCH:              return "Stretch";
        case DXGI_SCALING_NONE:                 return "None";
        case DXGI_SCALING_ASPECT_RATIO_STRETCH: return "Aspect Ratio Stretch";
        default:                                return "Unknown";
    }
}

const char* GetDXGISwapEffectString(DXGI_SWAP_EFFECT effect) {
    switch (effect) {
        case DXGI_SWAP_EFFECT_DISCARD:         return "Discard";
        case DXGI_SWAP_EFFECT_SEQUENTIAL:      return "Sequential";
        case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL: return "Flip Sequential";
        case DXGI_SWAP_EFFECT_FLIP_DISCARD:    return "Flip Discard";
        default:                               return "Unknown";
    }
}

const char* GetDXGIAlphaModeString(DXGI_ALPHA_MODE mode) {
    switch (mode) {
        case DXGI_ALPHA_MODE_UNSPECIFIED:   return "Unspecified";
        case DXGI_ALPHA_MODE_PREMULTIPLIED: return "Premultiplied";
        case DXGI_ALPHA_MODE_STRAIGHT:      return "Straight";
        case DXGI_ALPHA_MODE_IGNORE:        return "Ignore";
        default:                            return "Unknown";
    }
}

const char* GetDXGIColorSpaceString(DXGI_COLOR_SPACE_TYPE color_space) {
    switch (color_space) {
        case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:           return "RGB Full G22 None P709";
        case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:           return "RGB Full G10 None P709";
        case DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709:         return "RGB Studio G22 None P709";
        case DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020:        return "RGB Studio G22 None P2020";
        case DXGI_COLOR_SPACE_RESERVED:                         return "Reserved";
        case DXGI_COLOR_SPACE_YCBCR_FULL_G22_NONE_P709_X601:    return "YCbCr Full G22 None P709 X601";
        case DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601:       return "YCbCr Studio G22 Left P601";
        case DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601:         return "YCbCr Full G22 Left P601";
        case DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709:       return "YCbCr Studio G22 Left P709";
        case DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709:         return "YCbCr Full G22 Left P709";
        case DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020:      return "YCbCr Studio G22 Left P2020";
        case DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020:        return "YCbCr Full G22 Left P2020";
        case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:        return "RGB Full G2084 None P2020 (HDR10)";
        case DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020:    return "YCbCr Studio G2084 Left P2020";
        case DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020:      return "RGB Studio G2084 None P2020";
        case DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_TOPLEFT_P2020:   return "YCbCr Studio G22 TopLeft P2020";
        case DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_TOPLEFT_P2020: return "YCbCr Studio G2084 TopLeft P2020";
        case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020:          return "RGB Full G22 None P2020";
        case DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P709:       return "YCbCr Studio G24 Left P709";
        case DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P2020:      return "YCbCr Studio G24 Left P2020";
        case DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_TOPLEFT_P2020:   return "YCbCr Studio G24 TopLeft P2020";
        case DXGI_COLOR_SPACE_CUSTOM:                           return "Custom";
        default:                                                return "Unknown";
    }
}

void DrawDLSSPresetOverride() {
    // Legacy function - kept for backward compatibility
    // Now shows the old separate header format
    if (ImGui::CollapsingHeader("DLSS Preset Override", ImGuiTreeNodeFlags_None)) {
        ImGui::Indent();
        DrawDLSSPresetOverrideContent();
        ImGui::Unindent();
    }
}

void DrawDLSSPresetOverrideContent() {
    // Content of the DLSS Preset Override section
    // Warning about experimental nature
    ImGui::TextColored(ui::colors::TEXT_WARNING,
                       ICON_FK_WARNING " EXPERIMENTAL FEATURE - May require alt-tab to apply changes!");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "This feature overrides DLSS presets at runtime.\nChanges may require alt-tabbing out and back into "
                "the game to take effect.\nUse with caution as it may cause rendering issues in some games.");
        }

        ImGui::Spacing();

        // Enable/disable checkbox
        if (CheckboxSetting(settings::g_swapchainTabSettings.dlss_preset_override_enabled,
                            "Enable DLSS Preset Override")) {
            LogInfo("DLSS preset override %s",
                    settings::g_swapchainTabSettings.dlss_preset_override_enabled.GetValue() ? "enabled" : "disabled");
            // Reset NGX preset initialization when override is enabled/disabled
            ResetNGXPresetInitialization();
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Override DLSS presets at runtime using NGX parameter interception.\nThis works similar to Special-K's "
                "DLSS preset override feature.");
        }

        // Preset selection (only enabled when override is enabled)
        if (settings::g_swapchainTabSettings.dlss_preset_override_enabled.GetValue()) {
            ImGui::Spacing();

            // DLSS Super Resolution preset - Dynamic based on supported presets
            DLSSGSummary summary = GetDLSSGSummary();
            std::vector<std::string> preset_options = GetDLSSPresetOptions(summary.supported_dlss_presets);

            // Convert to const char* array for ImGui
            std::vector<const char*> preset_cstrs;
            preset_cstrs.reserve(preset_options.size());
            for (const auto& option : preset_options) {
                preset_cstrs.push_back(option.c_str());
            }

            if (!summary.ray_reconstruction_active) {
                // Find current selection
                std::string current_value = settings::g_swapchainTabSettings.dlss_sr_preset_override.GetValue();
                int current_selection = 0;
                for (size_t i = 0; i < preset_options.size(); ++i) {
                    if (current_value == preset_options[i]) {
                        current_selection = static_cast<int>(i);
                        break;
                    }
                }

                if (ImGui::Combo("DLSS Super Resolution Preset", &current_selection, preset_cstrs.data(),
                                 static_cast<int>(preset_cstrs.size()))) {
                    settings::g_swapchainTabSettings.dlss_sr_preset_override.SetValue(
                        preset_options[current_selection]);
                    LogInfo("DLSS SR preset changed to %s (index %d)", preset_options[current_selection].c_str(),
                            current_selection);
                    // Reset NGX preset initialization so new preset will be applied on next initialization
                    ResetNGXPresetInitialization();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Select the DLSS Super Resolution preset to override.\nGame Default = no override (don't "
                        "change anything)\nDLSS Default = set value to 0\nPreset A = 1, Preset B = 2, etc.\nOnly "
                        "presets supported by your DLSS version are shown.");
                }
            } else {
                // DLSS Ray Reconstruction preset - Dynamic based on supported RR presets
                std::vector<std::string> rr_preset_options = GetDLSSPresetOptions(summary.supported_dlss_rr_presets);

                // Convert to const char* array for ImGui
                std::vector<const char*> rr_preset_cstrs;
                rr_preset_cstrs.reserve(rr_preset_options.size());
                for (const auto& option : rr_preset_options) {
                    rr_preset_cstrs.push_back(option.c_str());
                }

                // Find current selection
                std::string current_rr_value = settings::g_swapchainTabSettings.dlss_rr_preset_override.GetValue();
                int current_rr_selection = 0;
                for (size_t i = 0; i < rr_preset_options.size(); ++i) {
                    if (current_rr_value == rr_preset_options[i]) {
                        current_rr_selection = static_cast<int>(i);
                        break;
                    }
                }

                if (ImGui::Combo("DLSS Ray Reconstruction Preset", &current_rr_selection, rr_preset_cstrs.data(),
                                 static_cast<int>(rr_preset_cstrs.size()))) {
                    settings::g_swapchainTabSettings.dlss_rr_preset_override.SetValue(
                        rr_preset_options[current_rr_selection]);
                    LogInfo("DLSS RR preset changed to %s (index %d)", rr_preset_options[current_rr_selection].c_str(),
                            current_rr_selection);
                    // Reset NGX preset initialization so new preset will be applied on next initialization
                    ResetNGXPresetInitialization();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Select the DLSS Ray Reconstruction preset to override.\nGame Default = no override (don't "
                        "change anything)\nDLSS Default = set value to 0\nPreset A = 1, Preset B = 2, Preset C = 3, "
                        "Preset D = 4, Preset E = 5, etc.\nA, B, C, D, E presets are supported for Ray Reconstruction "
                        "(version dependent).");
                }
            }

            ImGui::Spacing();

            // Show current settings summary
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Current Settings:");
            if (!summary.ray_reconstruction_active) {
                ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  DLSS SR Preset: %s",
                                   settings::g_swapchainTabSettings.dlss_sr_preset_override.GetValue().c_str());
            } else {
                ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  DLSS RR Preset: %s",
                                   settings::g_swapchainTabSettings.dlss_rr_preset_override.GetValue().c_str());
            }
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "Note: Preset values are mapped as follows:");
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "  Game Default = no override (don't change anything)");
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "  DLSS Default = set value to 0");
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "  Preset A = 1, Preset B = 2, etc.");
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "  SR presets supported by your DLSS version: %s",
                               summary.supported_dlss_presets.c_str());
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "  RR presets supported by your DLSS version: %s",
                               summary.supported_dlss_rr_presets.c_str());
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                               "  These values override the corresponding NGX parameter values.");
        }

        // DLSS Model Profile display
        DLSSModelProfile model_profile = GetDLSSModelProfile();
        if (model_profile.is_valid) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "DLSS Model Profile:");

            // Get current quality preset to determine which values to show
            DLSSGSummary summary = GetDLSSGSummary();
            std::string current_quality = summary.quality_preset;
            int sr_preset_value = 0;
            int rr_preset_value = 0;

            // Determine which preset values to display based on current quality preset
            if (current_quality == "Quality") {
                sr_preset_value = model_profile.sr_quality_preset;
                rr_preset_value = model_profile.rr_quality_preset;
            } else if (current_quality == "Balanced") {
                sr_preset_value = model_profile.sr_balanced_preset;
                rr_preset_value = model_profile.rr_balanced_preset;
            } else if (current_quality == "Performance") {
                sr_preset_value = model_profile.sr_performance_preset;
                rr_preset_value = model_profile.rr_performance_preset;
            } else if (current_quality == "Ultra Performance") {
                sr_preset_value = model_profile.sr_ultra_performance_preset;
                rr_preset_value = model_profile.rr_ultra_performance_preset;
            } else if (current_quality == "Ultra Quality") {
                sr_preset_value = model_profile.sr_ultra_quality_preset;
                rr_preset_value = model_profile.rr_ultra_quality_preset;
            } else if (current_quality == "DLAA") {
                sr_preset_value = model_profile.sr_dlaa_preset;
                rr_preset_value = 0;  // DLAA doesn't have RR equivalent
            } else {
                // Default to Quality if unknown
                sr_preset_value = model_profile.sr_quality_preset;
                rr_preset_value = model_profile.rr_quality_preset;
            }

            if (!summary.ray_reconstruction_active) {
                // Display current preset values
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "  Super Resolution (%s): %d",
                                   current_quality.c_str(), sr_preset_value);
            } else {
                // Display current preset values
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "  Ray Reconstruction (%s): %d",
                                   current_quality.c_str(), rr_preset_value);
            }

            // Show all values in tooltip
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "All DLSS Model Profile Values:\n"
                    "Super Resolution:\n"
                    "  Quality: %d, Balanced: %d, Performance: %d\n"
                    "  Ultra Performance: %d, Ultra Quality: %d, DLAA: %d\n"
                    "Ray Reconstruction:\n"
                    "  Quality: %d, Balanced: %d, Performance: %d\n"
                    "  Ultra Performance: %d, Ultra Quality: %d",
                    model_profile.sr_quality_preset, model_profile.sr_balanced_preset,
                    model_profile.sr_performance_preset, model_profile.sr_ultra_performance_preset,
                    model_profile.sr_ultra_quality_preset, model_profile.sr_dlaa_preset,
                    model_profile.rr_quality_preset, model_profile.rr_balanced_preset,
                    model_profile.rr_performance_preset, model_profile.rr_ultra_performance_preset,
                    model_profile.rr_ultra_quality_preset);
            }
        }
}

}  // namespace ui::new_ui
