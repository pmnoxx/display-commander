#include "device_info_tab.hpp"
#include "../../addon.hpp"
#include "../../dxgi/dxgi_device_info.hpp"
#include <deps/imgui/imgui.h>
#include <sstream>
#include <iomanip>

namespace renodx::ui::new_ui {

void DrawDeviceInfoTab() {
    ImGui::Text("Device Info Tab - Graphics Device and Display Information");
    ImGui::Separator();
    
    // Draw all device info sections
    DrawBasicDeviceInfo();
    ImGui::Spacing();
    DrawMonitorInfo();
    ImGui::Spacing();
    DrawDeviceRefreshControls();
    ImGui::Spacing();
    DrawHdrAndColorspaceControls();
    ImGui::Spacing();
    DrawDxgiDeviceInfo();
    ImGui::Spacing();
    DrawDxgiDeviceInfoDetailed();
}

void DrawBasicDeviceInfo() {
    if (ImGui::CollapsingHeader("Basic Device Information", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Get current device info
        HWND hwnd = g_last_swapchain_hwnd.load();
        int bb_w = g_last_backbuffer_width.load();
        int bb_h = g_last_backbuffer_height.load();
        reshade::api::color_space colorspace = g_current_colorspace;
        std::string hdr_status = g_hdr10_override_status;
        std::string hdr_timestamp = g_hdr10_override_timestamp;
        
        // Display device information
        ImGui::Text("Current Window: %p", hwnd);
        ImGui::Text("Backbuffer Size: %dx%d", bb_w, bb_h);
        ImGui::Text("Colorspace: %d", static_cast<int>(colorspace));
        ImGui::Text("HDR10 Override: %s", hdr_status.c_str());
        if (hdr_status != "Not applied" && hdr_status != "Never") {
            ImGui::Text("HDR10 Timestamp: %s", hdr_timestamp.c_str());
        }
    }
}

void DrawMonitorInfo() {
    if (ImGui::CollapsingHeader("Monitor Information", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Display monitor information
        if (!g_monitors.empty()) {
            ImGui::Text("Monitors (%zu):", g_monitors.size());
            for (size_t i = 0; i < g_monitors.size(); ++i) {
                const auto& monitor = g_monitors[i];
                ImGui::Text("Monitor %zu: %ldx%ld at (%ld,%ld)", 
                           i + 1, 
                           monitor.info.rcMonitor.right - monitor.info.rcMonitor.left,
                           monitor.info.rcMonitor.bottom - monitor.info.rcMonitor.top,
                           monitor.info.rcMonitor.left, monitor.info.rcMonitor.top);
            }
        } else {
            ImGui::Text("No monitor information available");
        }
    }
}

void DrawDeviceRefreshControls() {
    if (ImGui::CollapsingHeader("Device Refresh Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Refresh Device Info")) {
            if (g_dxgiDeviceInfoManager && g_dxgiDeviceInfoManager->IsInitialized()) {
                g_dxgiDeviceInfoManager->RefreshDeviceInfo();
                LogInfo("Device information refreshed");
            }
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Click to refresh device information");
        
        ImGui::SameLine();
        if (ImGui::Button("Force Re-enumeration")) {
            if (g_dxgiDeviceInfoManager && g_dxgiDeviceInfoManager->IsInitialized()) {
                g_dxgiDeviceInfoManager->RefreshDeviceInfo();
                LogInfo("Device re-enumeration forced");
            }
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Force re-enumeration of all devices");
    }
}

void DrawHdrAndColorspaceControls() {
    if (ImGui::CollapsingHeader("HDR and Colorspace Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Add HDR metadata reset button
        if (ImGui::Button("Reset HDR Metadata")) {
            // Find first HDR10-capable output and reset its metadata
            if (g_dxgiDeviceInfoManager && g_dxgiDeviceInfoManager->IsInitialized()) {
                const auto& adapters = g_dxgiDeviceInfoManager->GetAdapters();
                for (const auto& adapter : adapters) {
                    for (const auto& output : adapter.outputs) {
                        if (output.supports_hdr10) {
                            if (g_dxgiDeviceInfoManager->ResetHDRMetadataOnPresent(output.device_name)) {
                                LogInfo(("HDR metadata reset initiated for: " + output.device_name).c_str());
                            } else {
                                LogWarn(("HDR metadata reset failed for: " + output.device_name).c_str());
                            }
                            break; // Only reset the first HDR10 output found
                        }
                    }
                }
            }
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Reset HDR metadata for HDR10 displays");

        // Colorspace selector
        ImGui::SameLine();
        if (ImGui::Button("Set Colorspace")) {
            // Colorspace selection will be handled in the dropdown below
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "Set swapchain colorspace");
        
        // Colorspace dropdown
        static int selected_colorspace = 0;
        const char* colorspace_names[] = {
            "sRGB (Non-Linear)",
            "Extended sRGB (Linear)", 
            "HDR10 (ST2084/PQ)",
            "HDR10 (HLG)"
        };
        const reshade::api::color_space colorspace_values[] = {
            reshade::api::color_space::srgb_nonlinear,
            reshade::api::color_space::extended_srgb_linear,
            reshade::api::color_space::hdr10_st2084,
            reshade::api::color_space::hdr10_hlg
        };
        
        if (ImGui::Combo("Colorspace", &selected_colorspace, colorspace_names, IM_ARRAYSIZE(colorspace_names))) {
            // Apply the selected colorspace
            if (g_dxgiDeviceInfoManager && g_dxgiDeviceInfoManager->IsInitialized()) {
                bool success = g_dxgiDeviceInfoManager->SetColorspace(colorspace_values[selected_colorspace]);
                
                if (success) {
                    LogInfo(("Colorspace changed to: " + std::string(colorspace_names[selected_colorspace])).c_str());
                } else {
                    LogWarn(("Failed to change colorspace to: " + std::string(colorspace_names[selected_colorspace])).c_str());
                }
            }
        }
    }
}

void DrawDxgiDeviceInfo() {
    if (ImGui::CollapsingHeader("DXGI Device Information", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!g_dxgiDeviceInfoManager || !g_dxgiDeviceInfoManager->IsInitialized()) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "DXGI Device Info Manager not initialized");
            return;
        }

        // Force device enumeration when this tab is first opened
        static bool first_open = true;
        if (first_open) {
            g_dxgiDeviceInfoManager->RefreshDeviceInfo();
            first_open = false;
        }

        const auto& adapters = g_dxgiDeviceInfoManager->GetAdapters();
        if (adapters.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No DXGI adapters found yet. Device enumeration happens automatically during present operations.");
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "If you're still not seeing adapters, try refreshing or check if a game/application is running.");
            return;
        }

        ImGui::Separator();

        // Display adapter information
        for (size_t i = 0; i < adapters.size(); ++i) {
            const auto& adapter = adapters[i];
            
            // Adapter header
            std::string adapter_title = adapter.name + " - " + adapter.description;
            if (ImGui::TreeNodeEx(adapter_title.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                
                // Adapter details
                ImGui::Text("Description: %s", adapter.description.c_str());
                ImGui::Text("Dedicated Video Memory: %.1f GB", adapter.dedicated_video_memory / (1024.0 * 1024.0 * 1024.0));
                ImGui::Text("Dedicated System Memory: %.1f GB", adapter.dedicated_system_memory / (1024.0 * 1024.0 * 1024.0));
                ImGui::Text("Shared System Memory: %.1f GB", adapter.shared_system_memory / (1024.0 * 1024.0 * 1024.0));
                ImGui::Text("Software Adapter: %s", adapter.is_software ? "Yes" : "No");
                
                // LUID info
                std::ostringstream luid_oss;
                luid_oss << "Adapter LUID: 0x" << std::hex << adapter.adapter_luid.HighPart << "_" << adapter.adapter_luid.LowPart;
                ImGui::Text("%s", luid_oss.str().c_str());

                // Outputs
                if (!adapter.outputs.empty()) {
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Outputs (%zu):", adapter.outputs.size());
                    
                    for (size_t j = 0; j < adapter.outputs.size(); ++j) {
                        const auto& output = adapter.outputs[j];
                        std::string output_title = "Output " + std::to_string(j) + " - " + output.device_name;
                        
                        if (ImGui::TreeNodeEx(output_title.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                            
                            // Basic output info
                            ImGui::Text("Device Name: %s", output.device_name.c_str());
                            ImGui::Text("Monitor Name: %s", output.monitor_name.c_str());
                            ImGui::Text("Attached: %s", output.is_attached ? "Yes" : "No");
                            ImGui::Text("Desktop Coordinates: (%ld, %ld) to (%ld, %ld)", 
                                       output.desktop_coordinates.left, output.desktop_coordinates.top,
                                       output.desktop_coordinates.right, output.desktop_coordinates.bottom);
                            
                            // Resolution and refresh rate
                            int width = output.desktop_coordinates.right - output.desktop_coordinates.left;
                            int height = output.desktop_coordinates.bottom - output.desktop_coordinates.top;
                            ImGui::Text("Resolution: %dx%d", width, height);
                            
                            if (output.refresh_rate.Denominator > 0) {
                                float refresh = static_cast<float>(output.refresh_rate.Numerator) / static_cast<float>(output.refresh_rate.Denominator);
                                ImGui::Text("Refresh Rate: %.3f Hz", refresh);
                            }

                            // HDR information
                            if (output.supports_hdr10) {
                                ImGui::Separator();
                                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "HDR10 Support: ✓ Enabled");
                                ImGui::Text("Max Luminance: %.1f nits", output.max_luminance);
                                ImGui::Text("Min Luminance: %.1f nits", output.min_luminance);
                                ImGui::Text("Max Frame Average Light Level: %.1f nits", output.max_frame_average_light_level);
                                ImGui::Text("Max Content Light Level: %.1f nits", output.max_content_light_level);
                            } else {
                                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.0f, 1.0f), "HDR10 Support: ✗ Not Supported");
                            }

                            // Color space information
                            ImGui::Separator();
                            ImGui::Text("Color Space: %s", 
                                       output.color_space == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ? "HDR10" :
                                       output.color_space == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 ? "sRGB" :
                                       "Other");
                            ImGui::Text("Wide Color Gamut: %s", output.supports_wide_color_gamut ? "Yes" : "No");

                            // Supported modes count
                            if (!output.supported_modes.empty()) {
                                ImGui::Text("Supported Modes: %zu", output.supported_modes.size());
                            }

                            ImGui::TreePop();
                        }
                    }
                }

                ImGui::TreePop();
            }
        }
    }
}

void DrawDxgiDeviceInfoDetailed() {
    if (ImGui::CollapsingHeader("Detailed DXGI Device Information", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!g_dxgiDeviceInfoManager || !g_dxgiDeviceInfoManager->IsInitialized()) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "DXGI Device Info Manager not initialized");
            return;
        }

        const auto& adapters = g_dxgiDeviceInfoManager->GetAdapters();
        if (adapters.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No DXGI adapters found");
            return;
        }
        
        // Display detailed adapter information
        for (size_t i = 0; i < adapters.size(); ++i) {
            const auto& adapter = adapters[i];
            
            if (ImGui::CollapsingHeader(("Adapter " + std::to_string(i) + ": " + adapter.name).c_str())) {
                ImGui::Text("Description: %s", adapter.description.c_str());
                ImGui::Text("Dedicated Video Memory: %zu MB", adapter.dedicated_video_memory / (1024 * 1024));
                ImGui::Text("Dedicated System Memory: %zu MB", adapter.dedicated_system_memory / (1024 * 1024));
                ImGui::Text("Shared System Memory: %zu MB", adapter.shared_system_memory / (1024 * 1024));
                ImGui::Text("Software Adapter: %s", adapter.is_software ? "Yes" : "No");
                
                // Display output information
                if (ImGui::TreeNode(("Outputs (" + std::to_string(adapter.outputs.size()) + ")").c_str())) {
                    for (size_t j = 0; j < adapter.outputs.size(); ++j) {
                        const auto& output = adapter.outputs[j];
                        
                        if (ImGui::TreeNode(("Output " + std::to_string(j) + ": " + output.device_name).c_str())) {
                            ImGui::Text("Device Name: %s", output.device_name.c_str());
                            ImGui::Text("Monitor Name: %s", output.monitor_name.c_str());
                            ImGui::Text("Attached to Desktop: %s", output.is_attached ? "Yes" : "No");
                            ImGui::Text("Supports HDR10: %s", output.supports_hdr10 ? "Yes" : "No");
                            
                            if (output.is_attached) {
                                ImGui::Text("Desktop Coordinates: (%ld, %ld) to (%ld, %ld)",
                                           output.desktop_coordinates.left, output.desktop_coordinates.top,
                                           output.desktop_coordinates.right, output.desktop_coordinates.bottom);
                                
                                // Resolution and refresh rate
                                int width = output.desktop_coordinates.right - output.desktop_coordinates.left;
                                int height = output.desktop_coordinates.bottom - output.desktop_coordinates.top;
                                ImGui::Text("Resolution: %dx%d", width, height);
                                
                                if (output.refresh_rate.Denominator > 0) {
                                    float refresh = static_cast<float>(output.refresh_rate.Numerator) / static_cast<float>(output.refresh_rate.Denominator);
                                    ImGui::Text("Refresh Rate: %.3f Hz", refresh);
                                }
                            }

                            // HDR information
                            if (output.supports_hdr10) {
                                ImGui::Separator();
                                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "HDR10 Support: ✓ Enabled");
                                ImGui::Text("Max Luminance: %.1f nits", output.max_luminance);
                                ImGui::Text("Min Luminance: %.1f nits", output.min_luminance);
                                ImGui::Text("Max Frame Average Light Level: %.1f nits", output.max_frame_average_light_level);
                                ImGui::Text("Max Content Light Level: %.1f nits", output.max_content_light_level);
                            } else {
                                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.0f, 1.0f), "HDR10 Support: ✗ Not Supported");
                            }

                            // Color space information
                            ImGui::Separator();
                            ImGui::Text("Color Space: %s", 
                                       output.color_space == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ? "HDR10" :
                                       output.color_space == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 ? "sRGB" :
                                       "Other");
                            ImGui::Text("Wide Color Gamut: %s", output.supports_wide_color_gamut ? "Yes" : "No");

                            // Supported modes count
                            if (!output.supported_modes.empty()) {
                                ImGui::Text("Supported Modes: %zu", output.supported_modes.size());
                            }

                            ImGui::TreePop();
                        }
                    }
                    ImGui::TreePop();
                }
            }
        }
    }
}

} // namespace renodx::ui::new_ui
