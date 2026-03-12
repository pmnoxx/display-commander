#include "swapchain_tab.hpp"
#include "../../config/display_commander_config.hpp"
#include "../../globals.hpp"
#include "../../hooks/api_hooks.hpp"
#include "../../hooks/nvidia/ngx_hooks.hpp"
#include "../../res/forkawesome.h"
#include "../../res/ui_colors.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../settings/swapchain_tab_settings.hpp"
#include "../../swapchain_events_power_saving.hpp"
#include "../../utils/dxgi_color_space.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/timing.hpp"
#include "../imgui_wrapper_base.hpp"

#include <dxgi1_6.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <map>
#include <reshade_imgui.hpp>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace ui::new_ui {
bool has_last_metadata = false;
bool auto_apply_hdr_metadata = false;

// CTA-861-G / DXGI HDR10: chromaticity encoded as 0-50000 for 0.00000-0.50000 (0.00001 steps)
constexpr UINT32 HDR10_CHROMATICITY_SCALE = 50000u;

// Static variables to track last set HDR metadata values (Rec. 709 / sRGB defaults)
DXGI_HDR_METADATA_HDR10 last_hdr_metadata = {
    .RedPrimary = {32000, 16500},    // Rec. 709 red (0.64, 0.33)
    .GreenPrimary = {15000, 30000},  // Rec. 709 green (0.30, 0.60)
    .BluePrimary = {7500, 3000},     // Rec. 709 blue (0.15, 0.06)
    .WhitePoint = {15635, 16450},    // D65 (0.3127, 0.3290)
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

    // Initialize HDR metadata with loaded values (CTA-861 scale 50000)
    last_hdr_metadata.RedPrimary[0] = static_cast<UINT16>(std::round(prim_red_x * HDR10_CHROMATICITY_SCALE));
    last_hdr_metadata.RedPrimary[1] = static_cast<UINT16>(std::round(prim_red_y * HDR10_CHROMATICITY_SCALE));
    last_hdr_metadata.GreenPrimary[0] = static_cast<UINT16>(std::round(prim_green_x * HDR10_CHROMATICITY_SCALE));
    last_hdr_metadata.GreenPrimary[1] = static_cast<UINT16>(std::round(prim_green_y * HDR10_CHROMATICITY_SCALE));
    last_hdr_metadata.BluePrimary[0] = static_cast<UINT16>(std::round(prim_blue_x * HDR10_CHROMATICITY_SCALE));
    last_hdr_metadata.BluePrimary[1] = static_cast<UINT16>(std::round(prim_blue_y * HDR10_CHROMATICITY_SCALE));
    last_hdr_metadata.WhitePoint[0] = static_cast<UINT16>(std::round(white_point_x * HDR10_CHROMATICITY_SCALE));
    last_hdr_metadata.WhitePoint[1] = static_cast<UINT16>(std::round(white_point_y * HDR10_CHROMATICITY_SCALE));
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
    if (g_last_reshade_device_api.load() != reshade::api::device_api::d3d12
        && g_last_reshade_device_api.load() != reshade::api::device_api::d3d11
        && g_last_reshade_device_api.load() != reshade::api::device_api::d3d10) {
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
void DrawDLSSGSummaryContent(display_commander::ui::IImGuiWrapper& imgui);
void DrawDLSSPresetOverrideContent(display_commander::ui::IImGuiWrapper& imgui);
void DrawDLSSSettings(display_commander::ui::IImGuiWrapper& imgui);

void DrawSwapchainTab(display_commander::ui::IImGuiWrapper& imgui, reshade::api::effect_runtime* runtime) {
    imgui.Text("Swapchain Tab - DXGI Information");

    // Draw all swapchain-related sections
    DrawSwapchainWrapperStats(imgui);
    imgui.Spacing();
    DrawSwapchainEventCounters(imgui);
    imgui.Spacing();
    DrawDLSSSettings(imgui);
    imgui.Spacing();
    DrawNGXParameters(imgui);
    imgui.Spacing();
    DrawDxgiCompositionInfo(imgui);
}

void DrawSwapchainWrapperStats(display_commander::ui::IImGuiWrapper& imgui) {
    if (imgui.CollapsingHeader("Swapchain Wrapper Statistics",
                               display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        imgui.TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Present/Present1 Calls Per Second & Frame Time Graphs");
        imgui.Separator();

        // Helper function to display stats and frame graph for each swapchain type
        auto displayStatsAndGraph = [&imgui](const char* type_name, SwapChainWrapperStats& stats, ImVec4 color) {
            uint64_t present_calls = stats.total_present_calls.load(std::memory_order_acquire);
            uint64_t present1_calls = stats.total_present1_calls.load(std::memory_order_acquire);
            double present_fps = stats.smoothed_present_fps.load(std::memory_order_acquire);
            double present1_fps = stats.smoothed_present1_fps.load(std::memory_order_acquire);

            imgui.PushStyleColor(ImGuiCol_Text, color);
            imgui.Text("%s Swapchain:", type_name);
            imgui.PopStyleColor();

            imgui.Indent();
            imgui.Text("  Present: %.2f calls/sec (total: %llu)", present_fps, present_calls);
            imgui.Text("  Present1: %.2f calls/sec (total: %llu)", present1_fps, present1_calls);

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
                    imgui.Text("  Frame Time: Min: %.2f ms | Max: %.2f ms | Avg: %.2f ms | FPS: %.1f", min_ft, max_ft,
                               avg_ft, avg_fps);

                    // Create overlay text
                    std::string overlay_text = "Frame Time: " + std::to_string(frame_times.back()).substr(0, 4) + " ms";

                    // Set graph size and scale
                    ImVec2 graph_size = ImVec2(-1.0f, 150.0f);  // Full width, 150px height
                    float scale_min = 0.0f;
                    float scale_max = avg_ft * 4.0f;

                    // Draw the frame time graph
                    std::string graph_label = std::string("##FrameTime") + type_name;
                    imgui.PlotLines(graph_label.c_str(), frame_times.data(), static_cast<int>(frame_times.size()),
                                    0,  // values_offset
                                    overlay_text.c_str(), scale_min, scale_max, graph_size);
                } else {
                    imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "  No frame time data available yet...");
                }
            } else {
                imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "  No frame time data available yet...");
            }

            imgui.Unindent();
        };

        displayStatsAndGraph("Proxy", g_swapchain_wrapper_stats_proxy, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
        imgui.Spacing();
        displayStatsAndGraph("Native", g_swapchain_wrapper_stats_native, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
    }
}

void DrawSwapchainEventCounters(display_commander::ui::IImGuiWrapper& imgui) {
    // Swapchain Event Counters Section (see docs/UI_STYLE_GUIDE.md for depth/indent rules)
    // Depth 1: Nested subsection with indentation and distinct colors
    imgui.Indent();                              // Indent nested header
    ui::colors::PushNestedHeaderColors(&imgui);  // Apply distinct colors for nested header
    if (imgui.CollapsingHeader("Swapchain Event Counters", display_commander::ui::wrapper_flags::TreeNodeFlags_None)) {
        imgui.Indent();  // Indent content inside subsection
        imgui.TextColored(ui::colors::TEXT_INFO, "Event Counters (Green = Working, Red = Not Working)");
        imgui.Separator();

        // Display each event counter with color coding

        uint32_t total_events = 0;

        // Helper function to display event category
        auto displayEventCategory = [&](const char* name, const auto& event_array, const auto& event_names_map,
                                        ImVec4 header_color) {
            ui::colors::PushNestedHeaderColors(&imgui);  // Apply distinct colors for nested category headers
            if (imgui.CollapsingHeader(name, display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
                imgui.Indent();

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
                    imgui.TextColored(color, "%s (%zu): %u", event_name, i, count);
                }

                imgui.TextColored(header_color, "Total %s: %u", name, category_total);
                imgui.Unindent();
            }
            ui::colors::PopNestedHeaderColors(&imgui);  // Restore default header colors
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
            {DXGI_FACTORY_EVENT_CREATEFACTORY1, "DXGI_FACTORY_EVENT_CREATEFACTORY1"},
            {DXGI_FACTORY_EVENT_CREATEFACTORY2, "DXGI_FACTORY_EVENT_CREATEFACTORY2"}};
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
            {STREAMLINE_EVENT_SL_UPGRADE_INTERFACE, "STREAMLINE_EVENT_SL_UPGRADE_INTERFACE"},
            {STREAMLINE_EVENT_SL_DLSS_GET_OPTIMAL_SETTINGS, "STREAMLINE_EVENT_SL_DLSS_GET_OPTIMAL_SETTINGS"}};
        displayEventCategory("Streamline Methods", g_streamline_event_counters, streamline_event_names,
                             ImVec4(0.6f, 0.8f, 1.0f, 1.0f));

        // D3D11 Texture Methods
        static const std::map<D3D11TextureEventIndex, const char*> d3d11_texture_event_names = {
            {D3D11_EVENT_CREATE_TEXTURE2D, "D3D11_EVENT_CREATE_TEXTURE2D"},
            {D3D11_EVENT_UPDATE_SUBRESOURCE, "D3D11_EVENT_UPDATE_SUBRESOURCE"},
            {D3D11_EVENT_UPDATE_SUBRESOURCE1, "D3D11_EVENT_UPDATE_SUBRESOURCE1"}};
        displayEventCategory("D3D11 Texture Methods", g_d3d11_texture_event_counters, d3d11_texture_event_names,
                             ImVec4(1.0f, 0.8f, 0.6f, 1.0f));

        imgui.Separator();
        imgui.TextColored(ui::colors::TEXT_INFO, "Total Events: %u", total_events);

        // NVAPI Event Counters Section
        imgui.Spacing();
        ui::colors::PushNestedHeaderColors(&imgui);  // Apply distinct colors for nested NVAPI header
        if (imgui.CollapsingHeader("NVAPI Event Counters",
                                   display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
            // NVAPI event mapping
            static const std::vector<std::pair<NvapiEventIndex, const char*>> nvapi_event_mapping = {
                {NVAPI_EVENT_GET_HDR_CAPABILITIES, "NVAPI_EVENT_GET_HDR_CAPABILITIES"},
                {NVAPI_EVENT_D3D_SET_LATENCY_MARKER, "NVAPI_EVENT_D3D_SET_LATENCY_MARKER"},
                {NVAPI_EVENT_D3D_SET_SLEEP_MODE, "NVAPI_EVENT_D3D_SET_SLEEP_MODE"},
                {NVAPI_EVENT_D3D_SLEEP, "NVAPI_EVENT_D3D_SLEEP"},
                {NVAPI_EVENT_D3D_GET_LATENCY, "NVAPI_EVENT_D3D_GET_LATENCY"},
                {NVAPI_EVENT_D3D_GET_SLEEP_STATUS, "NVAPI_EVENT_D3D_GET_SLEEP_STATUS"}};

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
                 .end_idx = NVAPI_EVENT_D3D_GET_SLEEP_STATUS,
                 .color = ImVec4(0.6f, 1.0f, 0.8f, 1.0f)}};

            for (const auto& group : nvapi_event_groups) {
                if (imgui.CollapsingHeader(group.name,
                                           display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
                    imgui.Indent();

                    for (int i = static_cast<int>(group.start_idx); i <= static_cast<int>(group.end_idx); ++i) {
                        uint32_t count = g_nvapi_event_counters[i].load();
                        nvapi_total_events += count;

                        imgui.TextColored(group.color, "%s: %u", nvapi_event_mapping[i].second, count);
                    }

                    imgui.Unindent();
                }
            }

            imgui.Separator();
            imgui.TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Total NVAPI Events: %u", nvapi_total_events);

            // Show last sleep timestamp
            uint64_t last_sleep_timestamp = g_nvapi_last_sleep_timestamp_ns.load();
            if (last_sleep_timestamp > 0) {
                uint64_t current_time = utils::get_now_ns();
                uint64_t time_since_sleep = current_time - last_sleep_timestamp;
                double time_since_sleep_ms = static_cast<double>(time_since_sleep) / utils::NS_TO_MS;

                imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "Last Sleep: %.2f ms ago", time_since_sleep_ms);
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx(
                        "Time since the last NVAPI_D3D_Sleep call was made.\nLower values indicate more recent sleep "
                        "calls.");
                }
            } else {
                imgui.TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Last Sleep: Never");
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx("No NVAPI_D3D_Sleep calls have been made yet.");
                }
            }

            // Show NVAPI status message
            if (nvapi_total_events > 0) {
                imgui.TextColored(ui::colors::TEXT_SUCCESS, "Status: NVAPI events are working correctly");
            } else {
                imgui.TextColored(ui::colors::TEXT_WARNING, "Status: No NVAPI events detected");
            }
        }
        ui::colors::PopNestedHeaderColors(&imgui);  // Restore default header colors

        // Show status message
        if (total_events > 0) {
            imgui.TextColored(ui::colors::TEXT_SUCCESS, "Status: Swapchain events are working correctly");
        } else {
            imgui.TextColored(ui::colors::TEXT_ERROR,
                              "Status: No swapchain events detected - check if addon is properly loaded");
        }
        imgui.Unindent();  // Unindent content
    }
    ui::colors::PopNestedHeaderColors(&imgui);  // Restore default header colors
    imgui.Unindent();                           // Unindent nested header section

    // NGX Counters Section (see docs/UI_STYLE_GUIDE.md for depth/indent rules)
    // Depth 0: Main section header
    if (imgui.CollapsingHeader("NGX Counters", display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        imgui.TextColored(ui::colors::TEXT_SUCCESS, "NVIDIA NGX Function Call Counters");
        imgui.Separator();

        // Depth 1: Nested subsections with indentation and distinct colors
        // Parameter functions
        imgui.Indent();                              // Indent nested headers
        ui::colors::PushNestedHeaderColors(&imgui);  // Apply distinct colors for nested headers
        if (imgui.CollapsingHeader("Parameter Functions",
                                   display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
            imgui.Indent();  // Indent content inside subsection
            imgui.TextColored(ui::colors::TEXT_VALUE, "SetF: %u", g_ngx_counters.parameter_setf_count.load());
            imgui.TextColored(ui::colors::TEXT_VALUE, "SetD: %u", g_ngx_counters.parameter_setd_count.load());
            imgui.TextColored(ui::colors::TEXT_VALUE, "SetI: %u", g_ngx_counters.parameter_seti_count.load());
            imgui.TextColored(ui::colors::TEXT_VALUE, "SetUI: %u", g_ngx_counters.parameter_setui_count.load());
            imgui.TextColored(ui::colors::TEXT_VALUE, "SetULL: %u", g_ngx_counters.parameter_setull_count.load());
            imgui.TextColored(ui::colors::TEXT_VALUE, "GetI: %u", g_ngx_counters.parameter_geti_count.load());
            imgui.TextColored(ui::colors::TEXT_VALUE, "GetUI: %u", g_ngx_counters.parameter_getui_count.load());
            imgui.TextColored(ui::colors::TEXT_VALUE, "GetULL: %u", g_ngx_counters.parameter_getull_count.load());
            imgui.TextColored(ui::colors::TEXT_VALUE, "GetVoidPointer: %u",
                              g_ngx_counters.parameter_getvoidpointer_count.load());
            imgui.Unindent();  // Unindent content
        }
        ui::colors::PopNestedHeaderColors(&imgui);  // Restore default header colors

        // D3D12 Feature Management
        ui::colors::PushNestedHeaderColors(&imgui);
        if (imgui.CollapsingHeader("D3D12 Feature Management",
                                   display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
            imgui.Indent();
            imgui.TextColored(ui::colors::TEXT_VALUE, "Init: %u", g_ngx_counters.d3d12_init_count.load());
            imgui.TextColored(ui::colors::TEXT_VALUE, "Init Ext: %u", g_ngx_counters.d3d12_init_ext_count.load());
            imgui.TextColored(ui::colors::TEXT_VALUE, "Init ProjectID: %u",
                              g_ngx_counters.d3d12_init_projectid_count.load());
            imgui.TextColored(ui::colors::TEXT_VALUE, "CreateFeature: %u",
                              g_ngx_counters.d3d12_createfeature_count.load());
            imgui.TextColored(ui::colors::TEXT_VALUE, "ReleaseFeature: %u",
                              g_ngx_counters.d3d12_releasefeature_count.load());
            imgui.TextColored(ui::colors::TEXT_VALUE, "EvaluateFeature: %u",
                              g_ngx_counters.d3d12_evaluatefeature_count.load());
            imgui.TextColored(ui::colors::TEXT_VALUE, "GetParameters: %u",
                              g_ngx_counters.d3d12_getparameters_count.load());
            imgui.TextColored(ui::colors::TEXT_VALUE, "AllocateParameters: %u",
                              g_ngx_counters.d3d12_allocateparameters_count.load());
            imgui.Unindent();
        }
        ui::colors::PopNestedHeaderColors(&imgui);

        // D3D11 Feature Management
        ui::colors::PushNestedHeaderColors(&imgui);
        if (imgui.CollapsingHeader("D3D11 Feature Management",
                                   display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
            imgui.Indent();
            imgui.TextColored(ui::colors::TEXT_VALUE, "Init: %u", g_ngx_counters.d3d11_init_count.load());
            imgui.TextColored(ui::colors::TEXT_VALUE, "Init Ext: %u", g_ngx_counters.d3d11_init_ext_count.load());
            imgui.TextColored(ui::colors::TEXT_VALUE, "Init ProjectID: %u",
                              g_ngx_counters.d3d11_init_projectid_count.load());
            imgui.TextColored(ui::colors::TEXT_VALUE, "CreateFeature: %u",
                              g_ngx_counters.d3d11_createfeature_count.load());
            imgui.TextColored(ui::colors::TEXT_VALUE, "ReleaseFeature: %u",
                              g_ngx_counters.d3d11_releasefeature_count.load());
            imgui.TextColored(ui::colors::TEXT_VALUE, "EvaluateFeature: %u",
                              g_ngx_counters.d3d11_evaluatefeature_count.load());
            imgui.TextColored(ui::colors::TEXT_VALUE, "GetParameters: %u",
                              g_ngx_counters.d3d11_getparameters_count.load());
            imgui.TextColored(ui::colors::TEXT_VALUE, "AllocateParameters: %u",
                              g_ngx_counters.d3d11_allocateparameters_count.load());
            imgui.Unindent();
        }
        ui::colors::PopNestedHeaderColors(&imgui);
        imgui.Unindent();  // Unindent nested headers section

        imgui.Separator();
        imgui.TextColored(ui::colors::TEXT_INFO, "Total NGX Calls: %u", g_ngx_counters.total_count.load());

        // Reset button
        if (imgui.Button("Reset NGX Counters")) {
            g_ngx_counters.reset();
        }

        // Show status message
        uint32_t total_ngx_calls = g_ngx_counters.total_count.load();
        if (total_ngx_calls > 0) {
            imgui.TextColored(ui::colors::TEXT_SUCCESS, "Status: NGX functions are being called");
        } else {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "Status: No NGX calls detected yet");
        }
    }

    // NVAPI SetSleepMode Values Section
    if (imgui.CollapsingHeader("NVAPI SetSleepMode Values", display_commander::ui::wrapper_flags::TreeNodeFlags_None)) {
        imgui.TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Last NVAPI SetSleepMode Parameters");
        imgui.Separator();

        auto params = g_last_nvapi_sleep_mode_params.load();
        if (params) {
            imgui.Text("Low Latency Mode: %s", params->bLowLatencyMode ? "Enabled" : "Disabled");
            imgui.Text("Boost: %s", params->bLowLatencyBoost ? "Enabled" : "Disabled");
            imgui.Text("Use Markers to Optimize: %s", params->bUseMarkersToOptimize ? "Enabled" : "Disabled");
            imgui.Text("Minimum Interval: %u μs", params->minimumIntervalUs);

            // Calculate FPS from interval
            if (params->minimumIntervalUs > 0) {
                float fps = 1000000.0f / params->minimumIntervalUs;
                imgui.Text("Target FPS: %.1f", fps);
            } else {
                imgui.Text("Target FPS: Unlimited");
            }
        } else {
            imgui.TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "No NVAPI SetSleepMode calls detected yet");
        }
    }

    // Power Saving Settings Section
    if (imgui.CollapsingHeader("Power Saving Settings", display_commander::ui::wrapper_flags::TreeNodeFlags_None)) {
        imgui.TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "GPU Power Saving Controls");
        imgui.Separator();

        // Main power saving toggle
        bool main_power_saving = settings::g_mainTabSettings.no_render_in_background.GetValue();
        if (imgui.Checkbox("Enable Power Saving in Background", &main_power_saving)) {
            settings::g_mainTabSettings.no_render_in_background.SetValue(main_power_saving);
        }

        if (main_power_saving) {
            imgui.Indent();

            // Compute shader suppression
            static bool suppress_compute = s_suppress_compute_in_background.load();
            if (imgui.Checkbox("Suppress Compute Shaders (Dispatch)", &suppress_compute)) {
                s_suppress_compute_in_background.store(suppress_compute);
            }
            imgui.SameLine();
            imgui.TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "?");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Skip compute shader dispatches when app is in background");
            }

            // Resource copy suppression
            static bool suppress_copy = s_suppress_copy_in_background.load();
            if (imgui.Checkbox("Suppress Resource Copying", &suppress_copy)) {
                s_suppress_copy_in_background.store(suppress_copy);
            }
            imgui.SameLine();
            imgui.TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "?");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Skip resource copy operations when app is in background");
            }

            // Memory operations suppression
            static bool suppress_memory = s_suppress_memory_ops_in_background.load();
            if (imgui.Checkbox("Suppress Memory Operations", &suppress_memory)) {
                s_suppress_memory_ops_in_background.store(suppress_memory);
            }
            imgui.SameLine();
            imgui.TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "?");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Skip resource mapping operations when app is in background");
            }

            // Resource binding suppression (more conservative)
            static bool suppress_binding = s_suppress_binding_in_background.load();
            if (imgui.Checkbox("Suppress Resource Binding (Experimental)", &suppress_binding)) {
                s_suppress_binding_in_background.store(suppress_binding);
            }
            imgui.SameLine();
            imgui.TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), ICON_FK_WARNING);
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Skip resource binding operations (may cause rendering issues)");
            }

            imgui.Unindent();
        }

        // Power saving status
        imgui.Separator();
        bool is_background = g_app_in_background.load(std::memory_order_acquire);
        imgui.TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Current Status:");
        imgui.Text("  App in Background: %s", is_background ? "Yes" : "No");
        imgui.Text("  Power Saving Active: %s", (main_power_saving && is_background) ? "Yes" : "No");

        if (main_power_saving && is_background) {
            imgui.TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "  " ICON_FK_OK " Power saving is currently active");
        }
    }
}

void DrawNGXParameters(display_commander::ui::IImGuiWrapper& imgui) {
    if (imgui.CollapsingHeader("NGX Parameters", display_commander::ui::wrapper_flags::TreeNodeFlags_None)) {
        imgui.TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "NGX Parameter Values (Live from Game)");
        imgui.Separator();

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
            imgui.TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f),
                              "All Parameters (%zu) - Sorted Alphabetically:", all_params.size());
            imgui.Spacing();

            // Add search filter
            static char search_filter[256] = "";
            imgui.InputTextWithHint("##NGXSearch", "Search parameters...", search_filter, sizeof(search_filter));
            imgui.Spacing();

            // Create a table-like display
            imgui.Columns(5, "NGXParameters", true);
            imgui.SetColumnWidth(0, 500);  // Parameter name
            imgui.SetColumnWidth(1, 80);   // Type
            imgui.SetColumnWidth(2, 150);  // Value (game value)
            imgui.SetColumnWidth(3, 150);  // Override value
            imgui.SetColumnWidth(4, 600);  // Actions (wider for buttons)

            // Header
            imgui.TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "Parameter Name");
            imgui.NextColumn();
            imgui.TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "Type");
            imgui.NextColumn();
            imgui.TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "Game Value");
            imgui.NextColumn();
            imgui.TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "Override");
            imgui.NextColumn();
            imgui.TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "Actions");
            imgui.NextColumn();
            imgui.Separator();

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

                imgui.TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "%s", param.name.c_str());
                imgui.NextColumn();
                imgui.TextColored(param.color, "%s", param.type.c_str());
                imgui.NextColumn();
                imgui.TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s", param.value.c_str());
                imgui.NextColumn();

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
                        default: override_value_str = "unknown"; break;
                    }
                }

                // Display override value (highlighted if active)
                if (has_override) {
                    imgui.TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%s", override_value_str.c_str());
                } else {
                    imgui.TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", override_value_str.c_str());
                }
                imgui.NextColumn();

                // Action buttons
                imgui.PushID(param.name.c_str());

                // Input field for override value
                imgui.SetNextItemWidth(120);
                if (has_override) {
                    imgui.PushStyleColor(ImGuiCol_FrameBg,
                                         ImVec4(0.0f, 0.3f, 0.0f, 1.0f));  // Green tint for active override
                }

                // Create input field based on type
                bool value_updated = false;
                if (param.type == "float" || param.type == "double") {
                    float float_val = has_override
                                          ? (param.type == "float" ? override_value.get_as_float()
                                                                   : static_cast<float>(override_value.get_as_double()))
                                          : 0.0f;
                    if (imgui.InputFloat("##OverrideInput", &float_val, 0.0f, 0.0f, "%.6f",
                                         ImGuiInputTextFlags_EnterReturnsTrue)) {
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
                    if (imgui.InputInt("##OverrideInput", &int_val, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
                        g_ngx_parameter_overrides.update_int(param.name, int_val);
                        LogInfo("NGX Parameter Override Set: %s = %d", param.name.c_str(), int_val);
                        value_updated = true;
                    }
                } else if (param.type == "uint") {
                    unsigned int uint_val = has_override ? override_value.get_as_uint() : 0;
                    int int_val = static_cast<int>(uint_val);
                    if (imgui.InputInt("##OverrideInput", &int_val, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
                        if (int_val >= 0) {
                            g_ngx_parameter_overrides.update_uint(param.name, static_cast<unsigned int>(int_val));
                            LogInfo("NGX Parameter Override Set: %s = %u", param.name.c_str(),
                                    static_cast<unsigned int>(int_val));
                            value_updated = true;
                        }
                    }
                } else if (param.type == "ull") {
                    uint64_t ull_val = has_override ? override_value.get_as_ull() : 0;
                    char ull_str[32];
                    snprintf(ull_str, sizeof(ull_str), "%llu", ull_val);
                    if (imgui.InputText("##OverrideInput", ull_str, sizeof(ull_str),
                                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsDecimal)) {
                        uint64_t new_val = strtoull(ull_str, nullptr, 10);
                        g_ngx_parameter_overrides.update_ull(param.name, new_val);
                        LogInfo("NGX Parameter Override Set: %s = %llu", param.name.c_str(), new_val);
                        value_updated = true;
                    }
                }

                if (has_override) {
                    imgui.PopStyleColor();
                }

                // Buttons on new line for better visibility
                imgui.SameLine();
                if (imgui.Button("Apply", ImVec2(100, 0))) {
                    // Force call NGX API to set the value immediately
                    if (ApplyNGXParameterOverride(param.name.c_str(), param.type.c_str())) {
                        LogInfo("NGX Parameter Applied: %s", param.name.c_str());
                    } else {
                        LogInfo("NGX Parameter Apply failed: %s (no override or parameter object)", param.name.c_str());
                    }
                }
                imgui.SameLine();
                if (imgui.Button("Set", ImVec2(100, 0))) {
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
                imgui.SameLine();
                if (imgui.Button("Clear", ImVec2(100, 0))) {
                    // Remove override
                    g_ngx_parameter_overrides.remove(param.name);
                    LogInfo("NGX Parameter Override Cleared: %s", param.name.c_str());
                }
                imgui.PopID();
                imgui.NextColumn();
                displayed_count++;
            }

            // Show filtered count if search is active
            if (strlen(search_filter) > 0) {
                imgui.Columns(1);
                imgui.Spacing();
                imgui.TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Showing %zu of %zu parameters", displayed_count,
                                  all_params.size());
                imgui.Spacing();
            }

            imgui.Columns(1);  // Reset columns
            imgui.Spacing();

            // Show type legend with counts
            imgui.TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Type Legend:");
            imgui.Indent();

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

            imgui.TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "float (%zu)", float_count);
            imgui.SameLine(100);
            imgui.TextColored(ImVec4(0.0f, 1.0f, 0.8f, 1.0f), "double (%zu)", double_count);
            imgui.SameLine(200);
            imgui.TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "int (%zu)", int_count);
            imgui.SameLine(300);
            imgui.TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "uint (%zu)", uint_count);
            imgui.SameLine(400);
            imgui.TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "ull (%zu)", ull_count);
            imgui.Unindent();
        } else {
            imgui.TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), ICON_FK_WARNING " No NGX parameters detected yet");
        }

        imgui.Separator();
        imgui.TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Total NGX Parameters: %zu", all_params.size());

        if (!all_params.empty()) {
            imgui.TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), ICON_FK_OK " NGX parameter hooks are working correctly");
        }
    }
}

// Draw DLSS Settings section with nested subheaders (see docs/UI_STYLE_GUIDE.md for depth/indent rules)
void DrawDLSSSettings(display_commander::ui::IImGuiWrapper& imgui) {
    // Depth 0: Main section header
    if (imgui.CollapsingHeader("DLSS Settings", display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        imgui.TextColored(ui::colors::TEXT_DEFAULT, "DLSS/DLSS-G/Ray Reconstruction Configuration");
        imgui.Separator();

        // Depth 1: Nested subsections with indentation and distinct colors
        imgui.Indent();  // Indent nested headers

        // DLSS/DLSS-G/RR Summary subsection
        ui::colors::PushNestedHeaderColors(&imgui);  // Apply distinct colors for nested header
        if (imgui.CollapsingHeader("DLSS/DLSS-G/RR Summary",
                                   display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
            imgui.Indent();  // Indent content inside subsection
            DrawDLSSGSummaryContent(imgui);
            imgui.Unindent();  // Unindent content
        }
        ui::colors::PopNestedHeaderColors(&imgui);  // Restore default header colors

        imgui.Spacing();

        // DLSS Preset Override subsection
        ui::colors::PushNestedHeaderColors(&imgui);  // Apply distinct colors for nested header
        if (imgui.CollapsingHeader("DLSS Preset Override", display_commander::ui::wrapper_flags::TreeNodeFlags_None)) {
            imgui.Indent();  // Indent content inside subsection
            DrawDLSSPresetOverrideContent(imgui);
            imgui.Unindent();  // Unindent content
        }
        ui::colors::PopNestedHeaderColors(&imgui);  // Restore default header colors

        imgui.Unindent();  // Unindent nested headers section
    }
}

void DrawDLSSGSummary(display_commander::ui::IImGuiWrapper& imgui) {
    // Legacy function - kept for backward compatibility
    // Now shows the old separate header format
    if (imgui.CollapsingHeader("DLSS/DLSS-G/RR Summary",
                               display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        imgui.Indent();
        DrawDLSSGSummaryContent(imgui);
        imgui.Unindent();
    }
}

void DrawDLSSGSummaryContent(display_commander::ui::IImGuiWrapper& imgui) {
    // Content of the DLSS/DLSS-G/RR Summary section
    imgui.TextColored(ui::colors::TEXT_INFO, "DLSS/DLSS-G/Ray Reconstruction Status Overview");
    imgui.Separator();

    DLSSGSummary summary = GetDLSSGSummary();

    // Create a two-column layout for the summary
    imgui.Columns(2, "DLSSGSummaryColumns", false);
    imgui.SetColumnWidth(0, 300);  // Label column
    imgui.SetColumnWidth(1, 350);  // Value column

    // Status indicators
    imgui.Text("DLSS Active:");
    imgui.NextColumn();
    imgui.TextColored(summary.dlss_active ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s",
                      summary.dlss_active ? "Yes" : "No");
    imgui.NextColumn();

    imgui.Text("DLSS-G Active:");
    imgui.NextColumn();
    imgui.TextColored(summary.dlss_g_active ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s",
                      summary.dlss_g_active ? "Yes" : "No");
    imgui.NextColumn();

    imgui.Text("Ray Reconstruction:");
    imgui.NextColumn();
    imgui.TextColored(
        summary.ray_reconstruction_active ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s",
        summary.ray_reconstruction_active ? "Yes" : "No");
    imgui.NextColumn();

    imgui.Text("FG Mode:");
    imgui.NextColumn();
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
    imgui.TextColored(fg_color, "%s", summary.fg_mode.c_str());
    imgui.NextColumn();

    // DLL Version information
    imgui.Text("DLSS DLL Version:");
    imgui.NextColumn();
    imgui.TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "%s", summary.dlss_dll_version.c_str());
    imgui.SameLine();
    imgui.TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), " [%s]", summary.supported_dlss_presets.c_str());
    imgui.NextColumn();

    imgui.Text("DLSS-G DLL Version:");
    imgui.NextColumn();
    imgui.TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "%s", summary.dlssg_dll_version.c_str());
    imgui.NextColumn();

    if (summary.dlssd_dll_version != "Not loaded") {
        imgui.Text("DLSS-D DLL Version:");
        imgui.NextColumn();
        imgui.TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "%s", summary.dlssd_dll_version.c_str());
        imgui.NextColumn();
    }

    imgui.Separator();

    // Resolution information
    imgui.Text("Internal Resolution:");
    imgui.NextColumn();
    imgui.Text("%s", summary.internal_resolution.c_str());
    imgui.NextColumn();

    imgui.Text("Output Resolution:");
    imgui.NextColumn();
    imgui.Text("%s", summary.output_resolution.c_str());
    imgui.NextColumn();

    imgui.Text("Scaling Ratio:");
    imgui.NextColumn();
    imgui.TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "%s", summary.scaling_ratio.c_str());
    imgui.NextColumn();

    imgui.Text("Quality Preset:");
    imgui.NextColumn();
    imgui.TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s", summary.quality_preset.c_str());
    imgui.NextColumn();

    imgui.Separator();

    // Camera and rendering settings
    imgui.Text("Aspect Ratio:");
    imgui.NextColumn();
    imgui.Text("%s", summary.aspect_ratio.c_str());
    imgui.NextColumn();

    imgui.Text("FOV:");
    imgui.NextColumn();
    imgui.Text("%s", summary.fov.c_str());
    imgui.NextColumn();

    imgui.Text("Jitter Offset:");
    imgui.NextColumn();
    imgui.Text("%s", summary.jitter_offset.c_str());
    imgui.NextColumn();

    imgui.Text("Exposure:");
    imgui.NextColumn();
    imgui.Text("%s", summary.exposure.c_str());
    imgui.NextColumn();

    imgui.Text("Sharpness:");
    imgui.NextColumn();
    imgui.Text("%s", summary.sharpness.c_str());
    imgui.NextColumn();

    imgui.Separator();

    // Technical settings
    imgui.Text("Depth Inverted:");
    imgui.NextColumn();
    imgui.TextColored(summary.depth_inverted == "Yes" ? ImVec4(1.0f, 0.5f, 0.0f, 1.0f) : ImVec4(0.5f, 1.0f, 0.5f, 1.0f),
                      "%s", summary.depth_inverted.c_str());
    imgui.NextColumn();

    imgui.Text("HDR Enabled:");
    imgui.NextColumn();
    imgui.TextColored(summary.hdr_enabled == "Yes" ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(0.8f, 0.8f, 0.8f, 1.0f),
                      "%s", summary.hdr_enabled.c_str());
    imgui.NextColumn();

    imgui.Text("Motion Vectors:");
    imgui.NextColumn();
    imgui.TextColored(
        summary.motion_vectors_included == "Yes" ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
        "%s", summary.motion_vectors_included.c_str());
    imgui.NextColumn();

    imgui.Text("Frame Time Delta:");
    imgui.NextColumn();
    imgui.TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "%s", summary.frame_time_delta.c_str());
    imgui.NextColumn();

    imgui.Text("Tonemapper Type:");
    imgui.NextColumn();
    imgui.Text("%s", summary.tonemapper_type.c_str());
    imgui.NextColumn();

    imgui.Text("Optical Flow Accelerator:");
    imgui.NextColumn();
    imgui.TextColored(summary.ofa_enabled == "Yes" ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
                      "%s", summary.ofa_enabled.c_str());
    imgui.NextColumn();

    imgui.Columns(1);  // Reset columns

    // Add some helpful information
    imgui.Spacing();
    imgui.TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f),
                      "Note: Values update in real-time as the game calls NGX functions");

    if (summary.dlss_g_active) {
        imgui.TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "DLSS Frame Generation is currently active!");
    }

    if (summary.ray_reconstruction_active) {
        imgui.TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Ray Reconstruction is currently active!");
    }

    if (summary.ofa_enabled == "Yes") {
        imgui.TextColored(ui::colors::TEXT_SUCCESS, "NVIDIA Optical Flow Accelerator (OFA) is enabled!");
    }
}

void DrawDxgiCompositionInfo(display_commander::ui::IImGuiWrapper& imgui) {
    if (imgui.CollapsingHeader("DXGI Composition Information",
                               display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        // Get backbuffer format
        std::string format_str = "Unknown";

        imgui.Text("Backbuffer: %dx%d", g_last_backbuffer_width.load(), g_last_backbuffer_height.load());
        imgui.Text("Format: %s", format_str.c_str());

        // Display HDR10 override status
        imgui.Text("HDR10 Colorspace Override: %s (Last: %s)", g_hdr10_override_status.load()->c_str(),
                   g_hdr10_override_timestamp.load()->c_str());
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
    return utils::GetDXGIColorSpaceString(color_space);
}

void DrawDLSSPresetOverride(display_commander::ui::IImGuiWrapper& imgui) {
    // Legacy function - kept for backward compatibility
    // Now shows the old separate header format
    if (imgui.CollapsingHeader("DLSS Preset Override", display_commander::ui::wrapper_flags::TreeNodeFlags_None)) {
        imgui.Indent();
        DrawDLSSPresetOverrideContent(imgui);
        imgui.Unindent();
    }
}

void DrawDLSSPresetOverrideContent(display_commander::ui::IImGuiWrapper& imgui) {
    // Content of the DLSS Preset Override section
    // Warning about experimental nature
    imgui.TextColored(ui::colors::TEXT_WARNING,
                      ICON_FK_WARNING " EXPERIMENTAL FEATURE - May require alt-tab to apply changes!");
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "This feature overrides DLSS presets at runtime.\nChanges may require alt-tabbing out and back into "
            "the game to take effect.\nUse with caution as it may cause rendering issues in some games.");
    }

    imgui.Spacing();

    // Enable/disable checkbox
    if (CheckboxSetting(settings::g_swapchainTabSettings.dlss_preset_override_enabled, "Enable DLSS Preset Override",
                        imgui)) {
        LogInfo("DLSS preset override %s",
                settings::g_swapchainTabSettings.dlss_preset_override_enabled.GetValue() ? "enabled" : "disabled");
        // Reset NGX preset initialization when override is enabled/disabled
        ResetNGXPresetInitialization();
    }

    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Override DLSS presets at runtime using NGX parameter interception.\nThis works similar to Special-K's "
            "DLSS preset override feature.");
    }

    // Preset selection (only enabled when override is enabled)
    if (settings::g_swapchainTabSettings.dlss_preset_override_enabled.GetValue()) {
        imgui.Spacing();

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

            if (imgui.Combo("DLSS Super Resolution Preset", &current_selection, preset_cstrs.data(),
                            static_cast<int>(preset_cstrs.size()))) {
                settings::g_swapchainTabSettings.dlss_sr_preset_override.SetValue(preset_options[current_selection]);
                LogInfo("DLSS SR preset changed to %s (index %d)", preset_options[current_selection].c_str(),
                        current_selection);
                // Reset NGX preset initialization so new preset will be applied on next initialization
                ResetNGXPresetInitialization();
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
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

            if (imgui.Combo("DLSS Ray Reconstruction Preset", &current_rr_selection, rr_preset_cstrs.data(),
                            static_cast<int>(rr_preset_cstrs.size()))) {
                settings::g_swapchainTabSettings.dlss_rr_preset_override.SetValue(
                    rr_preset_options[current_rr_selection]);
                LogInfo("DLSS RR preset changed to %s (index %d)", rr_preset_options[current_rr_selection].c_str(),
                        current_rr_selection);
                // Reset NGX preset initialization so new preset will be applied on next initialization
                ResetNGXPresetInitialization();
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "Select the DLSS Ray Reconstruction preset to override.\nGame Default = no override (don't "
                    "change anything)\nDLSS Default = set value to 0\nPreset A = 1, Preset B = 2, Preset C = 3, "
                    "Preset D = 4, Preset E = 5, etc.\nA, B, C, D, E presets are supported for Ray Reconstruction "
                    "(version dependent).");
            }
        }

        if (g_dlss_from_nvidia_app_bin.load()) {
            imgui.Spacing();
            imgui.TextColored(
                ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                "NVIDIA App DLSS override detected (.bin). Version and presets are controlled by the NVIDIA app.");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "DLSS was loaded from a .bin bundle (Streamline/NVIDIA App). Preset override may have limited "
                    "effect.");
            }
        }

        imgui.Spacing();

        // Show current settings summary
        imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Current Settings:");
        if (!summary.ray_reconstruction_active) {
            imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  DLSS SR Preset: %s",
                              settings::g_swapchainTabSettings.dlss_sr_preset_override.GetValue().c_str());
        } else {
            imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  DLSS RR Preset: %s",
                              settings::g_swapchainTabSettings.dlss_rr_preset_override.GetValue().c_str());
        }
        imgui.Spacing();
        imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "Note: Preset values are mapped as follows:");
        imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "  Game Default = no override (don't change anything)");
        imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "  DLSS Default = set value to 0");
        imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "  Preset A = 1, Preset B = 2, etc.");
        imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "  SR presets supported by your DLSS version: %s",
                          summary.supported_dlss_presets.c_str());
        imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "  RR presets supported by your DLSS version: %s",
                          summary.supported_dlss_rr_presets.c_str());
        imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                          "  These values override the corresponding NGX parameter values.");
    }

    // DLSS Model Profile display
    DLSSModelProfile model_profile = GetDLSSModelProfile();
    if (model_profile.is_valid) {
        imgui.Spacing();
        imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "DLSS Model Profile:");

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
            imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "  Super Resolution (%s): %d", current_quality.c_str(),
                              sr_preset_value);
        } else {
            // Display current preset values
            imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "  Ray Reconstruction (%s): %d", current_quality.c_str(),
                              rr_preset_value);
        }

        // Show all values in tooltip
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "All DLSS Model Profile Values:\n"
                "Super Resolution:\n"
                "  Quality: %d, Balanced: %d, Performance: %d\n"
                "  Ultra Performance: %d, Ultra Quality: %d, DLAA: %d\n"
                "Ray Reconstruction:\n"
                "  Quality: %d, Balanced: %d, Performance: %d\n"
                "  Ultra Performance: %d, Ultra Quality: %d",
                model_profile.sr_quality_preset, model_profile.sr_balanced_preset, model_profile.sr_performance_preset,
                model_profile.sr_ultra_performance_preset, model_profile.sr_ultra_quality_preset,
                model_profile.sr_dlaa_preset, model_profile.rr_quality_preset, model_profile.rr_balanced_preset,
                model_profile.rr_performance_preset, model_profile.rr_ultra_performance_preset,
                model_profile.rr_ultra_quality_preset);
        }
    }
}

}  // namespace ui::new_ui
