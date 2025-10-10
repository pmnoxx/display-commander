#include "swapchain_tab.hpp"
#include "../../globals.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../swapchain_events_power_saving.hpp"
#include "../../utils.hpp"

#include <array>
#include <algorithm>
#include <vector>
#include <map>
#include <cctype>
#include <dxgi1_6.h>
#include <imgui.h>
#include <wrl/client.h>
#include <reshade.hpp>

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

    // Read HDR metadata settings from ReShade config
    reshade::get_config_value(nullptr, "ReShade_HDR_Metadata", "prim_red_x", prim_red_x);
    reshade::get_config_value(nullptr, "ReShade_HDR_Metadata", "prim_red_y", prim_red_y);
    reshade::get_config_value(nullptr, "ReShade_HDR_Metadata", "prim_green_x", prim_green_x);
    reshade::get_config_value(nullptr, "ReShade_HDR_Metadata", "prim_green_y", prim_green_y);
    reshade::get_config_value(nullptr, "ReShade_HDR_Metadata", "prim_blue_x", prim_blue_x);
    reshade::get_config_value(nullptr, "ReShade_HDR_Metadata", "prim_blue_y", prim_blue_y);
    reshade::get_config_value(nullptr, "ReShade_HDR_Metadata", "white_point_x", white_point_x);
    reshade::get_config_value(nullptr, "ReShade_HDR_Metadata", "white_point_y", white_point_y);
    reshade::get_config_value(nullptr, "ReShade_HDR_Metadata", "max_mdl", max_mdl);
    reshade::get_config_value(nullptr, "ReShade_HDR_Metadata", "min_mdl", min_mdl);
    reshade::get_config_value(nullptr, "ReShade_HDR_Metadata", "max_cll", max_cll);
    reshade::get_config_value(nullptr, "ReShade_HDR_Metadata", "max_fall", max_fall);

    // Read has_last_metadata flag from config
    reshade::get_config_value(nullptr, "ReShade_HDR_Metadata", "has_last_metadata", has_last_metadata);
    reshade::get_config_value(nullptr, "ReShade_HDR_Metadata", "auto_apply_hdr_metadata", auto_apply_hdr_metadata);

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
    if (g_last_swapchain_api.load() != static_cast<int>(reshade::api::device_api::d3d12) && g_last_swapchain_api.load() != static_cast<int>(reshade::api::device_api::d3d11) && g_last_swapchain_api.load() != static_cast<int>(reshade::api::device_api::d3d10)) {
        return;
    }
    static bool first_apply = true;

    // Get the current swapchain
    HWND hwnd = g_last_swapchain_hwnd.load();
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return; // No valid swapchain window
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

void DrawSwapchainTab() {
    ImGui::Text("Swapchain Tab - DXGI Information");
    ImGui::Separator();

    // Draw all swapchain-related sections
    DrawSwapchainEventCounters();
    ImGui::Spacing();
    DrawDLSSGSummary();
    ImGui::Spacing();
    DrawNGXParameters();
    ImGui::Spacing();
    DrawSwapchainInfo();
    ImGui::Spacing();
    DrawDxgiCompositionInfo();
}

void DrawSwapchainEventCounters() {
    if (ImGui::CollapsingHeader("Swapchain Event Counters", ImGuiTreeNodeFlags_None)) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Event Counters (Green = Working, Red = Not Working)");
        ImGui::Separator();

        // Display each event counter with color coding
        static const std::map<SwapchainEventIndex, const char*> event_names = {
            {SWAPCHAIN_EVENT_BEGIN_RENDER_PASS, "SWAPCHAIN_EVENT_BEGIN_RENDER_PASS"},
            {SWAPCHAIN_EVENT_END_RENDER_PASS, "SWAPCHAIN_EVENT_END_RENDER_PASS"},
            {SWAPCHAIN_EVENT_CREATE_SWAPCHAIN_CAPTURE, "SWAPCHAIN_EVENT_CREATE_SWAPCHAIN_CAPTURE"},
            {SWAPCHAIN_EVENT_INIT_SWAPCHAIN, "SWAPCHAIN_EVENT_INIT_SWAPCHAIN"},
            {SWAPCHAIN_EVENT_PRESENT_UPDATE_AFTER, "SWAPCHAIN_EVENT_PRESENT_UPDATE_AFTER"},
            {SWAPCHAIN_EVENT_PRESENT_UPDATE_BEFORE, "SWAPCHAIN_EVENT_PRESENT_UPDATE_BEFORE"},
            {SWAPCHAIN_EVENT_PRESENT_UPDATE_BEFORE2_UNUSED, "SWAPCHAIN_EVENT_PRESENT_UPDATE_BEFORE2"},
            {SWAPCHAIN_EVENT_INIT_COMMAND_LIST, "SWAPCHAIN_EVENT_INIT_COMMAND_LIST"},
            {SWAPCHAIN_EVENT_EXECUTE_COMMAND_LIST, "SWAPCHAIN_EVENT_EXECUTE_COMMAND_LIST"},
            {SWAPCHAIN_EVENT_BIND_PIPELINE, "SWAPCHAIN_EVENT_BIND_PIPELINE"},
            {SWAPCHAIN_EVENT_INIT_COMMAND_QUEUE, "SWAPCHAIN_EVENT_INIT_COMMAND_QUEUE"},
            {SWAPCHAIN_EVENT_RESET_COMMAND_LIST, "SWAPCHAIN_EVENT_RESET_COMMAND_LIST"},
            {SWAPCHAIN_EVENT_PRESENT_FLAGS, "SWAPCHAIN_EVENT_PRESENT_FLAGS"},
            {SWAPCHAIN_EVENT_DRAW, "SWAPCHAIN_EVENT_DRAW"},
            {SWAPCHAIN_EVENT_DRAW_INDEXED, "SWAPCHAIN_EVENT_DRAW_INDEXED"},
            {SWAPCHAIN_EVENT_DRAW_OR_DISPATCH_INDIRECT, "SWAPCHAIN_EVENT_DRAW_OR_DISPATCH_INDIRECT"},
            // New power saving events
            {SWAPCHAIN_EVENT_DISPATCH, "SWAPCHAIN_EVENT_DISPATCH"},
            {SWAPCHAIN_EVENT_DISPATCH_MESH, "SWAPCHAIN_EVENT_DISPATCH_MESH"},
            {SWAPCHAIN_EVENT_DISPATCH_RAYS, "SWAPCHAIN_EVENT_DISPATCH_RAYS"},
            {SWAPCHAIN_EVENT_COPY_RESOURCE, "SWAPCHAIN_EVENT_COPY_RESOURCE"},
            {SWAPCHAIN_EVENT_UPDATE_BUFFER_REGION, "SWAPCHAIN_EVENT_UPDATE_BUFFER_REGION"},
            {SWAPCHAIN_EVENT_UPDATE_BUFFER_REGION_COMMAND, "SWAPCHAIN_EVENT_UPDATE_BUFFER_REGION_COMMAND"},
            {SWAPCHAIN_EVENT_BIND_RESOURCE, "SWAPCHAIN_EVENT_BIND_RESOURCE"},
            {SWAPCHAIN_EVENT_MAP_RESOURCE, "SWAPCHAIN_EVENT_MAP_RESOURCE"},
            // Additional frame-specific GPU operations for power saving
            {SWAPCHAIN_EVENT_COPY_BUFFER_REGION, "SWAPCHAIN_EVENT_COPY_BUFFER_REGION"},
            {SWAPCHAIN_EVENT_COPY_BUFFER_TO_TEXTURE, "SWAPCHAIN_EVENT_COPY_BUFFER_TO_TEXTURE"},
            {SWAPCHAIN_EVENT_COPY_TEXTURE_TO_BUFFER, "SWAPCHAIN_EVENT_COPY_TEXTURE_TO_BUFFER"},
            {SWAPCHAIN_EVENT_COPY_TEXTURE_REGION, "SWAPCHAIN_EVENT_COPY_TEXTURE_REGION"},
            {SWAPCHAIN_EVENT_RESOLVE_TEXTURE_REGION, "SWAPCHAIN_EVENT_RESOLVE_TEXTURE_REGION"},
            {SWAPCHAIN_EVENT_CLEAR_RENDER_TARGET_VIEW, "SWAPCHAIN_EVENT_CLEAR_RENDER_TARGET_VIEW"},
            {SWAPCHAIN_EVENT_CLEAR_DEPTH_STENCIL_VIEW, "SWAPCHAIN_EVENT_CLEAR_DEPTH_STENCIL_VIEW"},
            {SWAPCHAIN_EVENT_CLEAR_UNORDERED_ACCESS_VIEW_UINT, "SWAPCHAIN_EVENT_CLEAR_UNORDERED_ACCESS_VIEW_UINT"},
            {SWAPCHAIN_EVENT_CLEAR_UNORDERED_ACCESS_VIEW_FLOAT, "SWAPCHAIN_EVENT_CLEAR_UNORDERED_ACCESS_VIEW_FLOAT"},
            {SWAPCHAIN_EVENT_GENERATE_MIPMAPS, "SWAPCHAIN_EVENT_GENERATE_MIPMAPS"},
            {SWAPCHAIN_EVENT_BLIT, "SWAPCHAIN_EVENT_BLIT"},
            {SWAPCHAIN_EVENT_BEGIN_QUERY, "SWAPCHAIN_EVENT_BEGIN_QUERY"},
            {SWAPCHAIN_EVENT_END_QUERY, "SWAPCHAIN_EVENT_END_QUERY"},
            {SWAPCHAIN_EVENT_RESOLVE_QUERY_DATA, "SWAPCHAIN_EVENT_RESOLVE_QUERY_DATA"},
            // DXGI Present hooks
            {SWAPCHAIN_EVENT_DXGI_PRESENT, "SWAPCHAIN_EVENT_DXGI_PRESENT"},
            {SWAPCHAIN_EVENT_DXGI_GETBUFFER, "SWAPCHAIN_EVENT_DXGI_GETBUFFER"},
            {SWAPCHAIN_EVENT_DXGI_SETFULLSCREENSTATE, "SWAPCHAIN_EVENT_DXGI_SETFULLSCREENSTATE"},
            {SWAPCHAIN_EVENT_DXGI_GETFULLSCREENSTATE, "SWAPCHAIN_EVENT_DXGI_GETFULLSCREENSTATE"},
            {SWAPCHAIN_EVENT_DXGI_GETDESC, "SWAPCHAIN_EVENT_DXGI_GETDESC"},
            {SWAPCHAIN_EVENT_DXGI_RESIZEBUFFERS, "SWAPCHAIN_EVENT_DXGI_RESIZEBUFFERS"},
            {SWAPCHAIN_EVENT_DXGI_RESIZETARGET, "SWAPCHAIN_EVENT_DXGI_RESIZETARGET"},
            {SWAPCHAIN_EVENT_DXGI_GETCONTAININGOUTPUT, "SWAPCHAIN_EVENT_DXGI_GETCONTAININGOUTPUT"},
            {SWAPCHAIN_EVENT_DXGI_GETFRAMESTATISTICS, "SWAPCHAIN_EVENT_DXGI_GETFRAMESTATISTICS"},
            {SWAPCHAIN_EVENT_DXGI_GETLASTPRESENTCOUNT, "SWAPCHAIN_EVENT_DXGI_GETLASTPRESENTCOUNT"},
            {SWAPCHAIN_EVENT_DXGI_GETDESC1, "SWAPCHAIN_EVENT_DXGI_GETDESC1"},
            {SWAPCHAIN_EVENT_DXGI_GETFULLSCREENDESC, "SWAPCHAIN_EVENT_DXGI_GETFULLSCREENDESC"},
            {SWAPCHAIN_EVENT_DXGI_GETHWND, "SWAPCHAIN_EVENT_DXGI_GETHWND"},
            {SWAPCHAIN_EVENT_DXGI_GETCOREWINDOW, "SWAPCHAIN_EVENT_DXGI_GETCOREWINDOW"},
            {SWAPCHAIN_EVENT_DXGI_PRESENT1, "SWAPCHAIN_EVENT_DXGI_PRESENT1"},
            {SWAPCHAIN_EVENT_DXGI_ISTEMPORARYMONOSUPPORTED, "SWAPCHAIN_EVENT_DXGI_ISTEMPORARYMONOSUPPORTED"},
            {SWAPCHAIN_EVENT_DXGI_GETRESTRICTTOOUTPUT, "SWAPCHAIN_EVENT_DXGI_GETRESTRICTTOOUTPUT"},
            {SWAPCHAIN_EVENT_DXGI_SETBACKGROUNDCOLOR, "SWAPCHAIN_EVENT_DXGI_SETBACKGROUNDCOLOR"},
            {SWAPCHAIN_EVENT_DXGI_GETBACKGROUNDCOLOR, "SWAPCHAIN_EVENT_DXGI_GETBACKGROUNDCOLOR"},
            {SWAPCHAIN_EVENT_DXGI_SETROTATION, "SWAPCHAIN_EVENT_DXGI_SETROTATION"},
            {SWAPCHAIN_EVENT_DXGI_GETROTATION, "SWAPCHAIN_EVENT_DXGI_GETROTATION"},
            {SWAPCHAIN_EVENT_DXGI_SETSOURCESIZE, "SWAPCHAIN_EVENT_DXGI_SETSOURCESIZE"},
            {SWAPCHAIN_EVENT_DXGI_GETSOURCESIZE, "SWAPCHAIN_EVENT_DXGI_GETSOURCESIZE"},
            {SWAPCHAIN_EVENT_DXGI_SETMAXIMUMFRAMELATENCY, "SWAPCHAIN_EVENT_DXGI_SETMAXIMUMFRAMELATENCY"},
            {SWAPCHAIN_EVENT_DXGI_GETMAXIMUMFRAMELATENCY, "SWAPCHAIN_EVENT_DXGI_GETMAXIMUMFRAMELATENCY"},
            {SWAPCHAIN_EVENT_DXGI_GETFRAMELATENCYWAIABLEOBJECT, "SWAPCHAIN_EVENT_DXGI_GETFRAMELATENCYWAIABLEOBJECT"},
            {SWAPCHAIN_EVENT_DXGI_SETMATRIXTRANSFORM, "SWAPCHAIN_EVENT_DXGI_SETMATRIXTRANSFORM"},
            {SWAPCHAIN_EVENT_DXGI_GETMATRIXTRANSFORM, "SWAPCHAIN_EVENT_DXGI_GETMATRIXTRANSFORM"},
            {SWAPCHAIN_EVENT_DXGI_GETCURRENTBACKBUFFERINDEX, "SWAPCHAIN_EVENT_DXGI_GETCURRENTBACKBUFFERINDEX"},
            {SWAPCHAIN_EVENT_DXGI_CHECKCOLORSPACESUPPORT, "SWAPCHAIN_EVENT_DXGI_CHECKCOLORSPACESUPPORT"},
            {SWAPCHAIN_EVENT_DXGI_SETCOLORSPACE1, "SWAPCHAIN_EVENT_DXGI_SETCOLORSPACE1"},
            {SWAPCHAIN_EVENT_DXGI_RESIZEBUFFERS1, "SWAPCHAIN_EVENT_DXGI_RESIZEBUFFERS1"},
            {SWAPCHAIN_EVENT_DXGI_FACTORY_CREATESWAPCHAIN, "SWAPCHAIN_EVENT_DXGI_FACTORY_CREATESWAPCHAIN"},
            {SWAPCHAIN_EVENT_DXGI_CREATEFACTORY, "SWAPCHAIN_EVENT_DXGI_CREATEFACTORY"},
            {SWAPCHAIN_EVENT_DXGI_CREATEFACTORY1, "SWAPCHAIN_EVENT_DXGI_CREATEFACTORY1"},
            {SWAPCHAIN_EVENT_DXGI_SETHDRMETADATA, "SWAPCHAIN_EVENT_DXGI_SETHDRMETADATA"},
            {SWAPCHAIN_EVENT_DX9_PRESENT, "SWAPCHAIN_EVENT_DX9_PRESENT"},
            {SWAPCHAIN_EVENT_NVAPI_GET_HDR_CAPABILITIES, "SWAPCHAIN_EVENT_NVAPI_GET_HDR_CAPABILITIES"},
            // NVAPI Reflex hooks
            {SWAPCHAIN_EVENT_NVAPI_D3D_SET_LATENCY_MARKER, "SWAPCHAIN_EVENT_NVAPI_D3D_SET_LATENCY_MARKER"},
            {SWAPCHAIN_EVENT_NVAPI_D3D_SET_SLEEP_MODE, "SWAPCHAIN_EVENT_NVAPI_D3D_SET_SLEEP_MODE"},
            {SWAPCHAIN_EVENT_NVAPI_D3D_SLEEP, "SWAPCHAIN_EVENT_NVAPI_D3D_SLEEP"},
            {SWAPCHAIN_EVENT_NVAPI_D3D_GET_LATENCY, "SWAPCHAIN_EVENT_NVAPI_D3D_GET_LATENCY"},
            // NGX Parameter hooks
            {SWAPCHAIN_EVENT_NGX_PARAMETER_SETF, "SWAPCHAIN_EVENT_NGX_PARAMETER_SETF"},
            {SWAPCHAIN_EVENT_NGX_PARAMETER_SETD, "SWAPCHAIN_EVENT_NGX_PARAMETER_SETD"},
            {SWAPCHAIN_EVENT_NGX_PARAMETER_SETI, "SWAPCHAIN_EVENT_NGX_PARAMETER_SETI"},
            {SWAPCHAIN_EVENT_NGX_PARAMETER_SETUI, "SWAPCHAIN_EVENT_NGX_PARAMETER_SETUI"},
            {SWAPCHAIN_EVENT_NGX_PARAMETER_SETULL, "SWAPCHAIN_EVENT_NGX_PARAMETER_SETULL"},
            // NGX Parameter getter hooks
            {SWAPCHAIN_EVENT_NGX_PARAMETER_GETI, "SWAPCHAIN_EVENT_NGX_PARAMETER_GETI"},
            {SWAPCHAIN_EVENT_NGX_PARAMETER_GETUI, "SWAPCHAIN_EVENT_NGX_PARAMETER_GETUI"},
            {SWAPCHAIN_EVENT_NGX_PARAMETER_GETULL, "SWAPCHAIN_EVENT_NGX_PARAMETER_GETULL"},
            {SWAPCHAIN_EVENT_NGX_PARAMETER_GETVOIDPOINTER, "SWAPCHAIN_EVENT_NGX_PARAMETER_GETVOIDPOINTER"},
            // Streamline hooks
            {SWAPCHAIN_EVENT_STREAMLINE_SL_INIT, "SWAPCHAIN_EVENT_STREAMLINE_SL_INIT"},
            {SWAPCHAIN_EVENT_STREAMLINE_SL_IS_FEATURE_SUPPORTED, "SWAPCHAIN_EVENT_STREAMLINE_SL_IS_FEATURE_SUPPORTED"},
            {SWAPCHAIN_EVENT_STREAMLINE_SL_GET_NATIVE_INTERFACE, "SWAPCHAIN_EVENT_STREAMLINE_SL_GET_NATIVE_INTERFACE"},
            {SWAPCHAIN_EVENT_STREAMLINE_SL_UPGRADE_INTERFACE, "SWAPCHAIN_EVENT_STREAMLINE_SL_UPGRADE_INTERFACE"}
        };

        uint32_t total_events = 0;

        // Group events by category for better organization
        struct EventGroup {
            const char* name;
            int start_idx;
            int end_idx;
            ImVec4 color;
        };

        static const std::array<EventGroup, 13> event_groups = {{{   .name="ReShade Events", .start_idx=SWAPCHAIN_EVENT_BEGIN_RENDER_PASS, .end_idx=SWAPCHAIN_EVENT_RESOLVE_QUERY_DATA, .color=ImVec4(0.8f, 0.8f, 1.0f, 1.0f)},
                                            {   .name="DXGI Core Methods", .start_idx=SWAPCHAIN_EVENT_DXGI_PRESENT, .end_idx=SWAPCHAIN_EVENT_DXGI_GETFRAMESTATISTICS, .color=ImVec4(0.8f, 1.0f, 0.8f, 1.0f)},
                                            {   .name="DXGI SwapChain1 Methods", .start_idx=SWAPCHAIN_EVENT_DXGI_GETDESC1, .end_idx=SWAPCHAIN_EVENT_DXGI_GETROTATION, .color=ImVec4(1.0f, 0.8f, 0.8f, 1.0f)},
                                            {   .name="DXGI SwapChain2 Methods", .start_idx=SWAPCHAIN_EVENT_DXGI_SETSOURCESIZE, .end_idx=SWAPCHAIN_EVENT_DXGI_GETMATRIXTRANSFORM, .color=ImVec4(1.0f, 1.0f, 0.8f, 1.0f)},
                                            {   .name="DXGI SwapChain3 Methods", .start_idx=SWAPCHAIN_EVENT_DXGI_GETCURRENTBACKBUFFERINDEX, .end_idx=SWAPCHAIN_EVENT_DXGI_RESIZEBUFFERS1, .color=ImVec4(0.8f, 1.0f, 1.0f, 1.0f)},
                                            {   .name="DXGI Factory Methods", .start_idx=SWAPCHAIN_EVENT_DXGI_FACTORY_CREATESWAPCHAIN, .end_idx=SWAPCHAIN_EVENT_DXGI_CREATEFACTORY1, .color=ImVec4(1.0f, 0.8f, 1.0f, 1.0f)},
                                            {   .name="DXGI SwapChain4 Methods", .start_idx=SWAPCHAIN_EVENT_DXGI_SETHDRMETADATA, .end_idx=SWAPCHAIN_EVENT_DXGI_SETHDRMETADATA, .color=ImVec4(0.8f, 0.8f, 0.8f, 1.0f)},
                                            {   .name="DirectX 9 Methods", .start_idx=SWAPCHAIN_EVENT_DX9_PRESENT, .end_idx=SWAPCHAIN_EVENT_DX9_PRESENT, .color=ImVec4(1.0f, 0.6f, 0.6f, 1.0f)},
                                            {   .name="NVAPI HDR Methods", .start_idx=SWAPCHAIN_EVENT_NVAPI_GET_HDR_CAPABILITIES, .end_idx=SWAPCHAIN_EVENT_NVAPI_GET_HDR_CAPABILITIES, .color=ImVec4(0.6f, 1.0f, 0.6f, 1.0f)},
                                            {   .name="NVAPI Reflex Methods", .start_idx=SWAPCHAIN_EVENT_NVAPI_D3D_SET_LATENCY_MARKER, .end_idx=SWAPCHAIN_EVENT_NVAPI_D3D_GET_LATENCY, .color=ImVec4(0.6f, 1.0f, 0.8f, 1.0f)},
                                            {   .name="NGX Parameter Set Methods", .start_idx=SWAPCHAIN_EVENT_NGX_PARAMETER_SETF, .end_idx=SWAPCHAIN_EVENT_NGX_PARAMETER_SETULL, .color=ImVec4(1.0f, 0.8f, 0.6f, 1.0f)},
                                            {   .name="NGX Parameter Get Methods", .start_idx=SWAPCHAIN_EVENT_NGX_PARAMETER_GETI, .end_idx=SWAPCHAIN_EVENT_NGX_PARAMETER_GETVOIDPOINTER, .color=ImVec4(1.0f, 0.6f, 0.8f, 1.0f)},
                                            {   .name="Streamline Methods", .start_idx=SWAPCHAIN_EVENT_STREAMLINE_SL_INIT, .end_idx=SWAPCHAIN_EVENT_STREAMLINE_SL_UPGRADE_INTERFACE, .color=ImVec4(0.6f, 0.8f, 1.0f, 1.0f)}}};

        for (const auto& group : event_groups) {
            if (ImGui::CollapsingHeader(group.name, ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();

                for (int i = group.start_idx; i <= group.end_idx && i < NUM_EVENTS; i++) {
                    uint32_t count = g_swapchain_event_counters[i].load();
                    total_events += count;

                    // Get the event name from the map
                    auto it = event_names.find(static_cast<SwapchainEventIndex>(i));
                    const char* event_name = (it != event_names.end()) ? it->second : "UNKNOWN_EVENT";

                    // Green if > 0, red if 0
                    ImVec4 color = (count > 0) ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
                    ImGui::TextColored(color, "%s (%d): %u", event_name, i, count);
                }

                ImGui::Unindent();
            }
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Total Events: %u", total_events);

        // Show status message
        if (total_events > 0) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Status: Swapchain events are working correctly");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
                               "Status: No swapchain events detected - check if addon is properly loaded");
        }
    }

    // NVAPI SetSleepMode Values Section
    if (ImGui::CollapsingHeader("NVAPI SetSleepMode Values", ImGuiTreeNodeFlags_None)) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Last NVAPI SetSleepMode Parameters");
        ImGui::Separator();

        auto params = g_last_nvapi_sleep_mode_params.load();
        if (params) {
            ImGui::Text("Low Latency Mode: %s", params->bLowLatencyMode ? "Enabled" : "Disabled");
            ImGui::Text("Low Latency Boost: %s", params->bLowLatencyBoost ? "Enabled" : "Disabled");
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
                        color = ImVec4(0.0f, 1.0f, 1.0f, 1.0f); // Cyan
                        break;
                    }
                    case ParameterValue::DOUBLE: {
                        char buffer[32];
                        snprintf(buffer, sizeof(buffer), "%.6f", value.get_as_double());
                        value_str = std::string(buffer);
                        type_str = "double";
                        color = ImVec4(0.0f, 1.0f, 0.8f, 1.0f); // Light cyan
                        break;
                    }
                    case ParameterValue::INT: {
                        value_str = std::to_string(value.get_as_int());
                        type_str = "int";
                        color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); // Yellow
                        break;
                    }
                    case ParameterValue::UINT: {
                        value_str = std::to_string(value.get_as_uint());
                        type_str = "uint";
                        color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f); // Orange
                        break;
                    }
                    case ParameterValue::ULL: {
                        value_str = std::to_string(value.get_as_ull());
                        type_str = "ull";
                        color = ImVec4(1.0f, 0.6f, 0.0f, 1.0f); // Dark orange
                        break;
                    }
                    default:
                        value_str = "unknown";
                        type_str = "unknown";
                        color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); // Gray
                        break;
                }

                all_params.push_back({
                    key,
                    value_str,
                    type_str,
                    color
                });
            }
        }

        // Sort parameters alphabetically by name
        std::sort(all_params.begin(), all_params.end(),
                  [](const ParameterEntry& a, const ParameterEntry& b) {
                      return a.name < b.name;
                  });

        // Display unified parameter list
        if (!all_params.empty()) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "All Parameters (%zu) - Sorted Alphabetically:", all_params.size());
            ImGui::Spacing();

            // Add search filter
            static char search_filter[256] = "";
            ImGui::InputTextWithHint("##NGXSearch", "Search parameters...", search_filter, sizeof(search_filter));
            ImGui::Spacing();

            // Create a table-like display
            ImGui::Columns(3, "NGXParameters", true);
            ImGui::SetColumnWidth(0, 750); // Parameter name (increased for long names)
            ImGui::SetColumnWidth(1, 80);  // Type
            ImGui::SetColumnWidth(2, 150); // Value

            // Header
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "Parameter Name");
            ImGui::NextColumn();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "Type");
            ImGui::NextColumn();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "Value");
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
                        continue; // Skip this parameter if it doesn't match the filter
                    }
                }

                ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "%s", param.name.c_str());
                ImGui::NextColumn();
                ImGui::TextColored(param.color, "%s", param.type.c_str());
                ImGui::NextColumn();
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s", param.value.c_str());
                ImGui::NextColumn();
                displayed_count++;
            }

            // Show filtered count if search is active
            if (strlen(search_filter) > 0) {
                ImGui::Columns(1);
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Showing %zu of %zu parameters", displayed_count, all_params.size());
                ImGui::Spacing();
            }

            ImGui::Columns(1); // Reset columns
            ImGui::Spacing();

            // Show type legend with counts
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Type Legend:");
            ImGui::Indent();

            // Count parameters by type
            size_t float_count = 0, double_count = 0, int_count = 0, uint_count = 0, ull_count = 0;
            for (const auto& param : all_params) {
                if (param.type == "float") float_count++;
                else if (param.type == "double") double_count++;
                else if (param.type == "int") int_count++;
                else if (param.type == "uint") uint_count++;
                else if (param.type == "ull") ull_count++;
            }

            ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "float (%zu)", float_count); ImGui::SameLine(100);
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.8f, 1.0f), "double (%zu)", double_count); ImGui::SameLine(200);
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "int (%zu)", int_count); ImGui::SameLine(300);
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "uint (%zu)", uint_count); ImGui::SameLine(400);
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "ull (%zu)", ull_count);
            ImGui::Unindent();
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "⚠ No NGX parameters detected yet");
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Total NGX Parameters: %zu", all_params.size());

        if (!all_params.empty()) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ NGX parameter hooks are working correctly");
        }
    }
}

void DrawDLSSGSummary() {
    if (ImGui::CollapsingHeader("DLSS/DLSS-G Summary", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "DLSS/DLSS-G Status Overview");
        ImGui::Separator();

        DLSSGSummary summary = GetDLSSGSummary();

        // Create a two-column layout for the summary
        ImGui::Columns(2, "DLSSGSummaryColumns", false);
        ImGui::SetColumnWidth(0, 300); // Label column
        ImGui::SetColumnWidth(1, 350); // Value column

        // Status indicators
        ImGui::Text("DLSS Active:");
        ImGui::NextColumn();
        ImGui::TextColored(summary.dlss_active ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
                          "%s", summary.dlss_active ? "Yes" : "No");
        ImGui::NextColumn();

        ImGui::Text("DLSS-G Active:");
        ImGui::NextColumn();
        ImGui::TextColored(summary.dlss_g_active ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
                          "%s", summary.dlss_g_active ? "Yes" : "No");
        ImGui::NextColumn();

        ImGui::Text("FG Mode:");
        ImGui::NextColumn();
        // Color code based on FG mode
        ImVec4 fg_color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f); // Default gray
        if (summary.fg_mode == "2x") {
            fg_color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); // Green for 2x
        } else if (summary.fg_mode == "3x") {
            fg_color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); // Yellow for 3x
        } else if (summary.fg_mode == "4x") {
            fg_color = ImVec4(1.0f, 0.5f, 0.0f, 1.0f); // Orange for 4x
        } else if (summary.fg_mode.find("x") != std::string::npos) {
            fg_color = ImVec4(1.0f, 0.0f, 1.0f, 1.0f); // Magenta for higher modes
        } else if (summary.fg_mode == "Disabled") {
            fg_color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // Red for disabled
        } else if (summary.fg_mode == "Unknown") {
            fg_color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); // Gray for unknown
        }
        ImGui::TextColored(fg_color, "%s", summary.fg_mode.c_str());
        ImGui::NextColumn();

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
        ImGui::TextColored(summary.depth_inverted == "Yes" ? ImVec4(1.0f, 0.5f, 0.0f, 1.0f) : ImVec4(0.5f, 1.0f, 0.5f, 1.0f),
                          "%s", summary.depth_inverted.c_str());
        ImGui::NextColumn();

        ImGui::Text("HDR Enabled:");
        ImGui::NextColumn();
        ImGui::TextColored(summary.hdr_enabled == "Yes" ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(0.8f, 0.8f, 0.8f, 1.0f),
                          "%s", summary.hdr_enabled.c_str());
        ImGui::NextColumn();

        ImGui::Text("Motion Vectors:");
        ImGui::NextColumn();
        ImGui::TextColored(summary.motion_vectors_included == "Yes" ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
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
        ImGui::TextColored(summary.ofa_enabled == "Yes" ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
                          "%s", summary.ofa_enabled.c_str());
        ImGui::NextColumn();

        ImGui::Columns(1); // Reset columns

        // Add some helpful information
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Note: Values update in real-time as the game calls NGX functions");

        if (summary.dlss_g_active) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "DLSS Frame Generation is currently active!");
        }

        if (summary.ofa_enabled == "Yes") {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "NVIDIA Optical Flow Accelerator (OFA) is enabled!");
        }
    }
}

void DrawDxgiCompositionInfo() {
    if (ImGui::CollapsingHeader("DXGI Composition Information", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* mode_str = "Unknown";
        int current_api = g_last_swapchain_api.load();
        DxgiBypassMode flip_state = GetFlipStateForAPI(current_api);

        switch (flip_state) {
            case DxgiBypassMode::kComposed:      mode_str = "Composed Flip"; break;
            case DxgiBypassMode::kOverlay:       mode_str = "Modern Independent Flip"; break;
            case DxgiBypassMode::kIndependentFlip: mode_str = "Legacy Independent Flip"; break;
            case DxgiBypassMode::kUnknown:
            default:                             mode_str = "Unknown"; break;
        }

        // Get backbuffer format
        std::string format_str = "Unknown";

        // Get colorspace string directly from swapchain
        std::string colorspace_str = "Unknown";
        if (auto *swapchain_ptr = g_last_swapchain_ptr.load()) {
            auto *swapchain = static_cast<reshade::api::swapchain*>(swapchain_ptr);
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

void DrawSwapchainInfo() {

    if (ImGui::CollapsingHeader("Swapchain Information", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Get the current swapchain from global variable
        void* swapchain_ptr = g_last_swapchain_ptr.load();
        if (swapchain_ptr == nullptr) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No active swapchain detected");
            return;
        }

        auto* swapchain = static_cast<reshade::api::swapchain*>(swapchain_ptr);
        if (swapchain == nullptr) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No swapchain available");
            return;
        }

        // Try to get DXGI swapchain interface
        Microsoft::WRL::ComPtr<IDXGISwapChain1> dxgi_swapchain;
        auto* native_swapchain = reinterpret_cast<IDXGISwapChain*>(swapchain->get_native());
        if (FAILED(native_swapchain->QueryInterface(IID_PPV_ARGS(&dxgi_swapchain)))) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Failed to get DXGI swapchain interface");
            return;
        }

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
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "  ✓ HDR-capable display detected");
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "  ⚠ Display does not support HDR");
                    }

                    // VRR support detection using CheckHardwareCompositionSupport
                    UINT support_flags = 0;
                    bool supports_vrr = false;
                    if (SUCCEEDED(output6->CheckHardwareCompositionSupport(&support_flags))) {
                        // Check for VRR support flag (0x1 = DXGI_HARDWARE_COMPOSITION_SUPPORT_FLAG_VARIABLE_REFRESH_RATE)
                        supports_vrr = (support_flags & 0x1) != 0;
                    }
                    ImGui::Text("  VRR Support: %s", supports_vrr ? "Yes" : "No");

                    if (supports_vrr) {
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "  ✓ Variable Refresh Rate (VRR) supported");
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "  ⚠ Display does not support VRR");
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
                reshade::set_config_value(nullptr, "ReShade_HDR_Metadata", "auto_apply_hdr_metadata", auto_apply_hdr_metadata);
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
                hdr10_metadata.RedPrimary[0] = 0.708 * 65535; //(Rec. 2020 red x)
                hdr10_metadata.RedPrimary[1] = 0.292 * 65535; //(Rec. 2020 red y)
                hdr10_metadata.GreenPrimary[0] = 0.170 * 65535;  //(Rec. 2020 green x)
                hdr10_metadata.GreenPrimary[1] = 0.797 * 65535; //(Rec. 2020 green y)
                hdr10_metadata.BluePrimary[0] = 0.131 * 65535; //(Rec. 2020 blue x)
                hdr10_metadata.BluePrimary[1] = 0.046 * 65535; //(Rec. 2020 blue y)
                hdr10_metadata.WhitePoint[0] = 0.3127 * 65535; //(D65 white x)
                hdr10_metadata.WhitePoint[1] = 0.3290 * 65535; //(D65 white y)
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

                    // Save HDR metadata to ReShade config
                    reshade::set_config_value(nullptr, "ReShade_HDR_Metadata", "prim_red_x", 0.708);
                    reshade::set_config_value(nullptr, "ReShade_HDR_Metadata", "prim_red_y", 0.292);
                    reshade::set_config_value(nullptr, "ReShade_HDR_Metadata", "prim_green_x", 0.170);
                    reshade::set_config_value(nullptr, "ReShade_HDR_Metadata", "prim_green_y", 0.797);
                    reshade::set_config_value(nullptr, "ReShade_HDR_Metadata", "prim_blue_x", 0.131);
                    reshade::set_config_value(nullptr, "ReShade_HDR_Metadata", "prim_blue_y", 0.046);
                    reshade::set_config_value(nullptr, "ReShade_HDR_Metadata", "white_point_x", 0.3127);
                    reshade::set_config_value(nullptr, "ReShade_HDR_Metadata", "white_point_y", 0.3290);
                    reshade::set_config_value(nullptr, "ReShade_HDR_Metadata", "max_mdl", 1000);
                    reshade::set_config_value(nullptr, "ReShade_HDR_Metadata", "min_mdl", 0.0f);
                    reshade::set_config_value(nullptr, "ReShade_HDR_Metadata", "max_cll", 1000);
                    reshade::set_config_value(nullptr, "ReShade_HDR_Metadata", "max_fall", 100);
                    reshade::set_config_value(nullptr, "ReShade_HDR_Metadata", "has_last_metadata", true);

                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ HDR metadata set to Rec. 2020 defaults and saved to config");
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
                    reshade::set_config_value(nullptr, "ReShade_HDR_Metadata", "has_last_metadata", false);
                    last_metadata_source = "Disabled";
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ HDR metadata disabled");
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

                    // Save HDR metadata to ReShade config
                    reshade::set_config_value(nullptr, "ReShade_HDR_Metadata", "prim_red_x", hdr10_metadata.RedPrimary[0] / 65535.0);
                    reshade::set_config_value(nullptr, "ReShade_HDR_Metadata", "prim_red_y", hdr10_metadata.RedPrimary[1] / 65535.0);
                    reshade::set_config_value(nullptr, "ReShade_HDR_Metadata", "prim_green_x", hdr10_metadata.GreenPrimary[0] / 65535.0);
                    reshade::set_config_value(nullptr, "ReShade_HDR_Metadata", "prim_green_y", hdr10_metadata.GreenPrimary[1] / 65535.0);
                    reshade::set_config_value(nullptr, "ReShade_HDR_Metadata", "prim_blue_x", hdr10_metadata.BluePrimary[0] / 65535.0);
                    reshade::set_config_value(nullptr, "ReShade_HDR_Metadata", "prim_blue_y", hdr10_metadata.BluePrimary[1] / 65535.0);
                    reshade::set_config_value(nullptr, "ReShade_HDR_Metadata", "white_point_x", hdr10_metadata.WhitePoint[0] / 65535.0);
                    reshade::set_config_value(nullptr, "ReShade_HDR_Metadata", "white_point_y", hdr10_metadata.WhitePoint[1] / 65535.0);
                    reshade::set_config_value(nullptr, "ReShade_HDR_Metadata", "max_mdl", static_cast<int32_t>(hdr10_metadata.MaxMasteringLuminance));
                    reshade::set_config_value(nullptr, "ReShade_HDR_Metadata", "min_mdl", hdr10_metadata.MinMasteringLuminance / 10000.0f);
                    reshade::set_config_value(nullptr, "ReShade_HDR_Metadata", "max_cll", static_cast<int32_t>(hdr10_metadata.MaxContentLightLevel));
                    reshade::set_config_value(nullptr, "ReShade_HDR_Metadata", "max_fall", static_cast<int32_t>(hdr10_metadata.MaxFrameAverageLightLevel));
                    reshade::set_config_value(nullptr, "ReShade_HDR_Metadata", "has_last_metadata", true);

                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ All HDR values applied and saved to config");
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
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "  ✓ Matches Rec. 709 color space");
                } else if (is_rec2020) {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "  ✓ Matches Rec. 2020 color space");
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "  ⚠ Custom color space (not Rec. 709/2020)");
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

}  // namespace ui::new_ui
