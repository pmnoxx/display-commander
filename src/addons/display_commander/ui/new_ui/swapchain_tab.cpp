#include "swapchain_tab.hpp"
#include "../../globals.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../swapchain_events_power_saving.hpp"

#include <dxgi1_6.h>
#include <imgui.h>
#include <wrl/client.h>
#include <reshade.hpp>

namespace ui::new_ui {

void DrawSwapchainTab() {
    ImGui::Text("Swapchain Tab - DXGI Information");
    ImGui::Separator();

    // Draw all swapchain-related sections
    DrawSwapchainEventCounters();
    ImGui::Spacing();
    DrawSwapchainInfo();
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
            "SWAPCHAIN_EVENT_DXGI_SETHDRMETADATA (73)",
            "SWAPCHAIN_EVENT_DX9_PRESENT (74)",
            "SWAPCHAIN_EVENT_NVAPI_GET_HDR_CAPABILITIES (75)",
            // Streamline hooks
            "SWAPCHAIN_EVENT_STREAMLINE_SL_INIT (76)",
            "SWAPCHAIN_EVENT_STREAMLINE_SL_IS_FEATURE_SUPPORTED (77)",
            "SWAPCHAIN_EVENT_STREAMLINE_SL_GET_NATIVE_INTERFACE (78)",
            "SWAPCHAIN_EVENT_STREAMLINE_SL_UPGRADE_INTERFACE (79)"
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
            {"DXGI SwapChain4 Methods (73)", 73, 73, ImVec4(0.8f, 0.8f, 0.8f, 1.0f)},
            {"DirectX 9 Methods (74)", 74, 74, ImVec4(1.0f, 0.6f, 0.6f, 1.0f)},
            {"NVAPI Methods (75)", 75, 75, ImVec4(0.6f, 1.0f, 0.6f, 1.0f)},
            {"Streamline Methods (76-79)", 76, 79, ImVec4(0.6f, 0.8f, 1.0f, 1.0f)}
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
        ImGui::Text("  Active DXGI Methods: %u/34", dxgi_active);

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
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Failed to get swapchain description: 0x%08lX", static_cast<unsigned long>(hr));
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
                    ImGui::Text("  Green Primary: (%.3f, %.3f)", output_desc.GreenPrimary[0], output_desc.GreenPrimary[1]);
                    ImGui::Text("  Blue Primary: (%.3f, %.3f)", output_desc.BluePrimary[0], output_desc.BluePrimary[1]);
                    ImGui::Text("  White Point: (%.3f, %.3f)", output_desc.WhitePoint[0], output_desc.WhitePoint[1]);

                    // HDR support detection
                    bool supports_hdr = (output_desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ||
                                       output_desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
                    ImGui::Text("  HDR Support: %s", supports_hdr ? "Yes" : "No");

                    if (supports_hdr) {
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "  ✓ HDR-capable display detected");
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "  ⚠ Display does not support HDR");
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

        // Static variables to track last set HDR metadata values
        static DXGI_HDR_METADATA_HDR10 last_hdr_metadata = {
            .RedPrimary = {32768, 21634},
            .GreenPrimary= {19661, 39321},
            .BluePrimary = {9830, 3932},
            .WhitePoint = {20493, 21564},
            .MaxMasteringLuminance = 1000,
            .MinMasteringLuminance = 0,
            .MaxContentLightLevel = 1000,
            .MaxFrameAverageLightLevel = 400,
        };
        static DXGI_HDR_METADATA_HDR10 dirty_last_metadata = last_hdr_metadata;

        static bool has_last_metadata = false;
        static std::string last_metadata_source = "None";


        // Try to get HDR metadata information from swapchain
        Microsoft::WRL::ComPtr<IDXGISwapChain4> swapchain4;
        if (SUCCEEDED(dxgi_swapchain->QueryInterface(IID_PPV_ARGS(&swapchain4)))) {
            ImGui::Text("HDR Metadata Support:");
            ImGui::Text("  IDXGISwapChain4: Available");
            ImGui::Text("  SetHDRMetaData: Supported");

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "HDR Metadata Controls:");
            ImGui::Separator();

            // Clear HDR metadata button
            if (ImGui::Button("Clear HDR Metadata")) {
                // Clear HDR10 metadata by setting it to default values
                DXGI_HDR_METADATA_HDR10 hdr10_metadata = {};
                hdr10_metadata.RedPrimary[0] = 32768;   // 0.5 * 65535 (Rec. 709 red x)
                hdr10_metadata.RedPrimary[1] = 21634;   // 0.33 * 65535 (Rec. 709 red y)
                hdr10_metadata.GreenPrimary[0] = 19661; // 0.3 * 65535 (Rec. 709 green x)
                hdr10_metadata.GreenPrimary[1] = 39321; // 0.6 * 65535 (Rec. 709 green y)
                hdr10_metadata.BluePrimary[0] = 9830;   // 0.15 * 65535 (Rec. 709 blue x)
                hdr10_metadata.BluePrimary[1] = 3932;   // 0.06 * 65535 (Rec. 709 blue y)
                hdr10_metadata.WhitePoint[0] = 20493;   // 0.3127 * 65535 (D65 white x)
                hdr10_metadata.WhitePoint[1] = 21564;   // 0.3290 * 65535 (D65 white y)
                 hdr10_metadata.MaxMasteringLuminance = 1000; // 1000 nits
                 hdr10_metadata.MinMasteringLuminance = 0;    // 0 nits
                 hdr10_metadata.MaxContentLightLevel = 1000;  // 1000 nits
                 hdr10_metadata.MaxFrameAverageLightLevel = 400; // 400 nits


                 HRESULT hr = swapchain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(hdr10_metadata), &hdr10_metadata);
                 if (SUCCEEDED(hr)) {
                     // Store the metadata for display
                     last_hdr_metadata = hdr10_metadata;
                     dirty_last_metadata = hdr10_metadata;
                     has_last_metadata = true;
                     last_metadata_source = "Clear HDR Metadata (Rec. 709 defaults)";
                     ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ HDR metadata cleared successfully");
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "✗ Failed to clear HDR metadata: 0x%08lX", static_cast<unsigned long>(hr));
                }
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "?");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Clear HDR metadata by setting it to default Rec. 709 values");
            }

            // Reset to monitor capabilities button
            if (ImGui::Button("Reset to Monitor Capabilities")) {
                // Get monitor capabilities and set HDR metadata accordingly
                Microsoft::WRL::ComPtr<IDXGIOutput> output;
                if (SUCCEEDED(dxgi_swapchain->GetContainingOutput(&output)) && output != nullptr) {
                    Microsoft::WRL::ComPtr<IDXGIOutput6> output6;
                    if (SUCCEEDED(output->QueryInterface(IID_PPV_ARGS(&output6)))) {
                        DXGI_OUTPUT_DESC1 output_desc = {};
                        if (SUCCEEDED(output6->GetDesc1(&output_desc))) {
                            // Create HDR10 metadata based on monitor capabilities
                            DXGI_HDR_METADATA_HDR10 hdr10_metadata = {};

                            // Use monitor's color primaries if available (convert float to UINT16)
                            hdr10_metadata.RedPrimary[0] = static_cast<UINT16>(output_desc.RedPrimary[0] * 65535.0f);
                            hdr10_metadata.RedPrimary[1] = static_cast<UINT16>(output_desc.RedPrimary[1] * 65535.0f);
                            hdr10_metadata.GreenPrimary[0] = static_cast<UINT16>(output_desc.GreenPrimary[0] * 65535.0f);
                            hdr10_metadata.GreenPrimary[1] = static_cast<UINT16>(output_desc.GreenPrimary[1] * 65535.0f);
                            hdr10_metadata.BluePrimary[0] = static_cast<UINT16>(output_desc.BluePrimary[0] * 65535.0f);
                            hdr10_metadata.BluePrimary[1] = static_cast<UINT16>(output_desc.BluePrimary[1] * 65535.0f);
                            hdr10_metadata.WhitePoint[0] = static_cast<UINT16>(output_desc.WhitePoint[0] * 65535.0f);
                            hdr10_metadata.WhitePoint[1] = static_cast<UINT16>(output_desc.WhitePoint[1] * 65535.0f);

                             // Use monitor's luminance range
                             hdr10_metadata.MaxMasteringLuminance = static_cast<UINT>(output_desc.MaxLuminance);
                             hdr10_metadata.MinMasteringLuminance = static_cast<UINT>(output_desc.MinLuminance);

                             // Set content light levels to reasonable values
                             hdr10_metadata.MaxContentLightLevel = static_cast<UINT>(output_desc.MaxLuminance * 0.8f); // 80% of max
                             hdr10_metadata.MaxFrameAverageLightLevel = static_cast<UINT>(output_desc.MaxLuminance * 0.4f); // 40% of max


                             HRESULT hr = swapchain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(hdr10_metadata), &hdr10_metadata);
                             if (SUCCEEDED(hr)) {
                                 // Store the metadata for display
                                 last_hdr_metadata = hdr10_metadata;
                                 dirty_last_metadata = hdr10_metadata;
                                 has_last_metadata = true;
                                 last_metadata_source = "Reset to Monitor Capabilities";
                                 ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ HDR metadata reset to monitor capabilities");
                            } else {
                                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "✗ Failed to reset HDR metadata: 0x%08lX", static_cast<unsigned long>(hr));
                            }
                        } else {
                            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "✗ Failed to get monitor capabilities");
                        }
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "✗ IDXGIOutput6 not available");
                    }
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "✗ Failed to get containing output");
                }
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "?");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Set HDR metadata to match the monitor's color primaries and luminance range");
            }

            // Disable HDR metadata button
            if (ImGui::Button("Disable HDR Metadata")) {
                // Disable HDR metadata by setting it to NULL
                HRESULT hr = swapchain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr);
                if (SUCCEEDED(hr)) {
                    // Clear the stored metadata since it's disabled
                    has_last_metadata = false;
                    last_metadata_source = "Disabled";
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ HDR metadata disabled");
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "✗ Failed to disable HDR metadata: 0x%08lX", static_cast<unsigned long>(hr));
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
            if (ImGui::InputInt("MaxMDL (nits)", &max_mdl_value, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
                dirty_last_metadata.MaxMasteringLuminance = static_cast<UINT>(max_mdl_value);
            }

            // Allow any value, no clamping

            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "?");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Maximum Mastering Display Luminance in nits.\nThis controls the peak brightness of the mastering display.");
            }


            int min_mdl_value = dirty_last_metadata.MinMasteringLuminance;
            if (ImGui::InputInt("MinMDL (nits)", &min_mdl_value, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
                dirty_last_metadata.MinMasteringLuminance = static_cast<UINT>(min_mdl_value);
                // Allow any value, no clamping
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "?");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Minimum Mastering Display Luminance in nits.\nThis controls the minimum brightness of the mastering display.");
            }

            int max_content_light_level = dirty_last_metadata.MaxContentLightLevel;
            if (ImGui::InputInt("Max Content Light Level (nits)", &max_content_light_level, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
                dirty_last_metadata.MaxContentLightLevel = static_cast<UINT>(max_content_light_level);
                // Allow any value, no clamping
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "?");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Maximum Content Light Level in nits.\nThis controls the peak brightness of the content.");
            }

            int max_frame_avg_light_level = dirty_last_metadata.MaxFrameAverageLightLevel;
            if (ImGui::InputInt("Max Frame Avg Light Level (nits)", &max_frame_avg_light_level, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
                dirty_last_metadata.MaxFrameAverageLightLevel = static_cast<UINT>(max_frame_avg_light_level);
                // Allow any value, no clamping
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "?");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Maximum Frame Average Light Level in nits.\nThis controls the average brightness of the content.");
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
                    hdr10_metadata.RedPrimary[0] = 32768;   // 0.5 * 65535
                    hdr10_metadata.RedPrimary[1] = 21634;   // 0.33 * 65535
                    hdr10_metadata.GreenPrimary[0] = 19661; // 0.3 * 65535
                    hdr10_metadata.GreenPrimary[1] = 39321; // 0.6 * 65535
                    hdr10_metadata.BluePrimary[0] = 9830;   // 0.15 * 65535
                    hdr10_metadata.BluePrimary[1] = 3932;   // 0.06 * 65535
                    hdr10_metadata.WhitePoint[0] = 20493;   // 0.3127 * 65535
                    hdr10_metadata.WhitePoint[1] = 21564;   // 0.3290 * 65535
                }

                // Set all the custom values
                hdr10_metadata.MaxMasteringLuminance = static_cast<UINT>(max_mdl_value);
                hdr10_metadata.MinMasteringLuminance = static_cast<UINT>(min_mdl_value);
                hdr10_metadata.MaxContentLightLevel = static_cast<UINT>(max_content_light_level);
                hdr10_metadata.MaxFrameAverageLightLevel = static_cast<UINT>(max_frame_avg_light_level);

                HRESULT hr = swapchain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(hdr10_metadata), &hdr10_metadata);
                if (SUCCEEDED(hr)) {
                    // Store the metadata for display
                    last_hdr_metadata = hdr10_metadata;
                    has_last_metadata = true;
                    last_metadata_source = "Custom HDR Values";
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ All HDR values applied successfully");
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "✗ Failed to apply HDR values: 0x%08lX", static_cast<unsigned long>(hr));
                }
            }

            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "?");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Apply all the HDR metadata values above to the swapchain.");
            }

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Note: These controls directly call SetHDRMetaData on the current swapchain.");
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Changes will be visible in the next frame and may affect HDR rendering.");

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
                ImGui::Text("  Red:   (%.4f, %.4f)",
                           last_hdr_metadata.RedPrimary[0] / 65535.0f,
                           last_hdr_metadata.RedPrimary[1] / 65535.0f);
                ImGui::Text("  Green: (%.4f, %.4f)",
                           last_hdr_metadata.GreenPrimary[0] / 65535.0f,
                           last_hdr_metadata.GreenPrimary[1] / 65535.0f);
                ImGui::Text("  Blue:  (%.4f, %.4f)",
                           last_hdr_metadata.BluePrimary[0] / 65535.0f,
                           last_hdr_metadata.BluePrimary[1] / 65535.0f);
                ImGui::Text("  White: (%.4f, %.4f)",
                           last_hdr_metadata.WhitePoint[0] / 65535.0f,
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
                bool is_rec709 = (abs(red_x - 0.64f) < 0.01f && abs(red_y - 0.33f) < 0.01f &&
                                 abs(green_x - 0.30f) < 0.01f && abs(green_y - 0.60f) < 0.01f &&
                                 abs(blue_x - 0.15f) < 0.01f && abs(blue_y - 0.06f) < 0.01f);

                bool is_rec2020 = (abs(red_x - 0.708f) < 0.01f && abs(red_y - 0.292f) < 0.01f &&
                                  abs(green_x - 0.170f) < 0.01f && abs(green_y - 0.797f) < 0.01f &&
                                  abs(blue_x - 0.131f) < 0.01f && abs(blue_y - 0.046f) < 0.01f);

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
        case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return "R8G8B8A8_UNORM_SRGB";
        case DXGI_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return "B8G8R8A8_UNORM_SRGB";
        case DXGI_FORMAT_R10G10B10A2_UNORM: return "R10G10B10A2_UNORM";
        case DXGI_FORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
        case DXGI_FORMAT_R32G32B32A32_FLOAT: return "R32G32B32A32_FLOAT";
        default: return "Unknown Format";
    }
}

const char* GetDXGIScalingString(DXGI_SCALING scaling) {
    switch (scaling) {
        case DXGI_SCALING_STRETCH: return "Stretch";
        case DXGI_SCALING_NONE: return "None";
        case DXGI_SCALING_ASPECT_RATIO_STRETCH: return "Aspect Ratio Stretch";
        default: return "Unknown";
    }
}

const char* GetDXGISwapEffectString(DXGI_SWAP_EFFECT effect) {
    switch (effect) {
        case DXGI_SWAP_EFFECT_DISCARD: return "Discard";
        case DXGI_SWAP_EFFECT_SEQUENTIAL: return "Sequential";
        case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL: return "Flip Sequential";
        case DXGI_SWAP_EFFECT_FLIP_DISCARD: return "Flip Discard";
        default: return "Unknown";
    }
}

const char* GetDXGIAlphaModeString(DXGI_ALPHA_MODE mode) {
    switch (mode) {
        case DXGI_ALPHA_MODE_UNSPECIFIED: return "Unspecified";
        case DXGI_ALPHA_MODE_PREMULTIPLIED: return "Premultiplied";
        case DXGI_ALPHA_MODE_STRAIGHT: return "Straight";
        case DXGI_ALPHA_MODE_IGNORE: return "Ignore";
        default: return "Unknown";
    }
}

const char* GetDXGIColorSpaceString(DXGI_COLOR_SPACE_TYPE color_space) {
    switch (color_space) {
        case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709: return "RGB Full G22 None P709";
        case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709: return "RGB Full G10 None P709";
        case DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709: return "RGB Studio G22 None P709";
        case DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020: return "RGB Studio G22 None P2020";
        case DXGI_COLOR_SPACE_RESERVED: return "Reserved";
        case DXGI_COLOR_SPACE_YCBCR_FULL_G22_NONE_P709_X601: return "YCbCr Full G22 None P709 X601";
        case DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601: return "YCbCr Studio G22 Left P601";
        case DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601: return "YCbCr Full G22 Left P601";
        case DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709: return "YCbCr Studio G22 Left P709";
        case DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709: return "YCbCr Full G22 Left P709";
        case DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020: return "YCbCr Studio G22 Left P2020";
        case DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020: return "YCbCr Full G22 Left P2020";
        case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020: return "RGB Full G2084 None P2020 (HDR10)";
        case DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020: return "YCbCr Studio G2084 Left P2020";
        case DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020: return "RGB Studio G2084 None P2020";
        case DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_TOPLEFT_P2020: return "YCbCr Studio G22 TopLeft P2020";
        case DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_TOPLEFT_P2020: return "YCbCr Studio G2084 TopLeft P2020";
        case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020: return "RGB Full G22 None P2020";
        case DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P709: return "YCbCr Studio G24 Left P709";
        case DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P2020: return "YCbCr Studio G24 Left P2020";
        case DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_TOPLEFT_P2020: return "YCbCr Studio G24 TopLeft P2020";
        case DXGI_COLOR_SPACE_CUSTOM: return "Custom";
        default: return "Unknown";
    }
}

}  // namespace ui::new_ui
