#include "dxgi_device_info.hpp"
#include "../addon.hpp"
#include <dxgi1_6.h>
#include <d3d11.h>
#include <dbghelp.h>

// External declarations
extern std::atomic<reshade::api::swapchain*> g_last_swapchain_ptr;

#pragma comment (lib, "dxgi.lib")
#pragma comment (lib, "d3d11.lib")
#pragma comment (lib, "dbghelp.lib")

// Stack trace functionality
void DXGIDeviceInfoManager::LogStackTrace(const char* context) {
    LogWarn(("Stack trace requested for context: " + std::string(context)).c_str());
    
    // Capture stack trace
    void* stack[64];
    WORD frames = CaptureStackBackTrace(0, 64, stack, nullptr);
    
    if (frames == 0) {
        LogWarn("Failed to capture stack trace");
        return;
    }
    
    LogWarn(("Stack trace captured " + std::to_string(frames) + " frames:").c_str());
    
    // Get symbol information
    HANDLE process = GetCurrentProcess();
    SymInitialize(process, nullptr, TRUE);
    
    SYMBOL_INFO* symbol = (SYMBOL_INFO*)malloc(sizeof(SYMBOL_INFO) + 256 * sizeof(char));
    symbol->MaxNameLen = 255;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    
    for (WORD i = 0; i < frames; i++) {
        DWORD64 address = (DWORD64)stack[i];
        
        if (SymFromAddr(process, address, nullptr, symbol)) {
            std::string frame_info = "  [" + std::to_string(i) + "] " + symbol->Name + 
                                   " at 0x" + std::format("{:016X}", address);
            LogWarn(frame_info.c_str());
        } else {
            std::string frame_info = "  [" + std::to_string(i) + "] Unknown at 0x" + 
                                   std::format("{:016X}", address);
            LogWarn(frame_info.c_str());
        }
    }
    
    free(symbol);
    SymCleanup(process);
}

LONG WINAPI DXGIDeviceInfoManager::UnhandledExceptionFilter(PEXCEPTION_POINTERS exception_info) {
    LogWarn("=== UNHANDLED EXCEPTION IN DXGI DEVICE INFO MANAGER ===");
    LogWarn(("Exception code: 0x" + std::format("{:08X}", exception_info->ExceptionRecord->ExceptionCode)).c_str());
    LogWarn(("Exception address: 0x" + std::format("{:016X}", (DWORD64)exception_info->ExceptionRecord->ExceptionAddress)).c_str());
    
    // Log stack trace
    void* stack[64];
    WORD frames = CaptureStackBackTrace(0, 64, stack, nullptr);
    
    if (frames > 0) {
        LogWarn("Stack trace at crash:");
        
        HANDLE process = GetCurrentProcess();
        SymInitialize(process, nullptr, TRUE);
        
        SYMBOL_INFO* symbol = (SYMBOL_INFO*)malloc(sizeof(SYMBOL_INFO) + 256 * sizeof(char));
        symbol->MaxNameLen = 255;
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        
        for (WORD i = 0; i < frames; i++) {
            DWORD64 address = (DWORD64)stack[i];
            
            if (SymFromAddr(process, address, nullptr, symbol)) {
                std::string frame_info = "  [" + std::to_string(i) + "] " + symbol->Name + 
                                       " at 0x" + std::format("{:016X}", address);
                LogWarn(frame_info.c_str());
            } else {
                std::string frame_info = " " + std::to_string(i) + "] Unknown at 0x" + 
                                       std::format("{:016X}", address);
                LogWarn(frame_info.c_str());
            }
        }
        
        free(symbol);
        SymCleanup(process);
    }
    
    LogWarn("=== END EXCEPTION REPORT ===");
    
    // Continue execution (don't crash the application)
    return EXCEPTION_CONTINUE_EXECUTION;
}

// Global instance declaration (defined in globals.cpp)
extern std::unique_ptr<DXGIDeviceInfoManager> g_dxgiDeviceInfoManager;

DXGIDeviceInfoManager::DXGIDeviceInfoManager() : initialized_(false) {
    // Install exception handler for crash debugging
    SetUnhandledExceptionFilter(UnhandledExceptionFilter);
}

DXGIDeviceInfoManager::~DXGIDeviceInfoManager() {
    Cleanup();
}

bool DXGIDeviceInfoManager::Initialize() {
    if (initialized_) {
        return true;
    }

    initialized_ = true;
    LogInfo("DXGI Device Info Manager initialized successfully");
    return true;
}

void DXGIDeviceInfoManager::RefreshDeviceInfo() {
    if (!initialized_) {
        return;
    }

    // Always clear and re-enumerate to get fresh data
    adapters_.clear();
    if (GetAdapterFromReShadeDevice()) {
        LogDebug("DXGI device information refreshed successfully");
    } else {
        LogDebug("DXGI device information refresh failed");
    }
}

void DXGIDeviceInfoManager::EnumerateDevicesOnPresent() {
    if (!initialized_) {
        return;
    }

    // Always try to enumerate if we don't have adapter information
    // This ensures we retry if previous attempts failed
    if (adapters_.empty()) {
        if (GetAdapterFromReShadeDevice()) {
            LogDebug("Device information enumerated during present");
        } else {
            LogDebug("Device enumeration attempted during present but failed");
        }
    } else {
        // Even if we have adapters, occasionally refresh to catch any changes
        static int present_counter = 0;
        present_counter++;
        if (present_counter >= 300) { // Refresh every 300 presents (about 5 seconds at 60fps)
            present_counter = 0;
            LogDebug("Periodic device information refresh during present");
            RefreshDeviceInfo();
        }
    }
}

void DXGIDeviceInfoManager::Cleanup() {
    adapters_.clear();
    initialized_ = false;
}

bool DXGIDeviceInfoManager::GetAdapterFromReShadeDevice() {
    try {
        // Get the current swapchain from ReShade
        auto* swapchain = g_last_swapchain_ptr.load();
        if (!swapchain) {
            LogWarn("No ReShade swapchain available");
            return false;
        }

        // Get the device from the swapchain
        auto* device = swapchain->get_device();
        if (!device) {
            LogWarn("No ReShade device available");
            return false;
        }

        // Get the native D3D11 device interface
        ID3D11Device* d3d11_device = reinterpret_cast<ID3D11Device*>(device->get_native());
        if (!d3d11_device) {
            LogWarn("Failed to get native D3D11 device");
            return false;
        }

        // Get the DXGI adapter using the correct method
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
        HRESULT hr = d3d11_device->QueryInterface(IID_PPV_ARGS(&dxgi_device));
        if (FAILED(hr)) {
            LogWarn("Failed to get DXGI device from D3D11 device");
            return false;
        }

        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        hr = dxgi_device->GetAdapter(&adapter);
        if (FAILED(hr)) {
            LogWarn("Failed to get DXGI adapter from DXGI device");
            return false;
        }

    // Get adapter description
    DXGI_ADAPTER_DESC desc = {};
    if (SUCCEEDED(adapter->GetDesc(&desc))) {
        DXGIAdapterInfo adapter_info = {};
        
        // Convert wide string to UTF-8
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, nullptr, 0, nullptr, nullptr);
        if (size_needed > 0) {
            std::string description(size_needed - 1, 0);
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, &description[0], size_needed, nullptr, nullptr);
            adapter_info.description = description;
        }
        
        adapter_info.name = "Primary Adapter";
        adapter_info.dedicated_video_memory = desc.DedicatedVideoMemory;
        adapter_info.dedicated_system_memory = desc.DedicatedSystemMemory;
        adapter_info.shared_system_memory = desc.SharedSystemMemory;
        adapter_info.adapter_luid = desc.AdapterLuid;
        adapter_info.is_software = (desc.DedicatedVideoMemory == 0);

        // Get additional device properties from ReShade
        uint32_t vendor_id = 0, device_id = 0, api_version = 0, driver_version = 0;
        char description_buffer[256] = {};
        
        if (device->get_property(reshade::api::device_properties::vendor_id, &vendor_id)) {
            // Store vendor ID if needed
        }
        if (device->get_property(reshade::api::device_properties::device_id, &device_id)) {
            // Store device ID if needed
        }
        if (device->get_property(reshade::api::device_properties::api_version, &api_version)) {
            // Store API version if needed
        }
        if (device->get_property(reshade::api::device_properties::driver_version, &driver_version)) {
            // Store driver version if needed
        }
        if (device->get_property(reshade::api::device_properties::description, description_buffer)) {
            // Use ReShade's description if available
            if (strlen(description_buffer) > 0) {
                adapter_info.description = description_buffer;
            }
        }

        // Enumerate outputs for this adapter
        EnumerateOutputs(adapter.Get(), adapter_info);
        
        adapters_.push_back(std::move(adapter_info));
    }

    return !adapters_.empty();
    } catch (...) {
        LogWarn("Exception occurred in GetAdapterFromReShadeDevice");
        LogStackTrace("GetAdapterFromReShadeDevice");
        return false;
    }
}

bool DXGIDeviceInfoManager::SetColorspace(reshade::api::color_space colorspace) {
    try {
        // Get ReShade's existing swapchain
        auto* swapchain = g_last_swapchain_ptr.load();
        if (!swapchain) {
            LogWarn("Colorspace setting: No ReShade swapchain available");
            return false;
        }

        // Get the native DXGI swapchain from ReShade
        IDXGISwapChain* dxgi_swapchain = reinterpret_cast<IDXGISwapChain*>(swapchain->get_native());
        if (!dxgi_swapchain) {
            LogWarn("Colorspace setting: Failed to get native DXGI swapchain from ReShade");
            return false;
        }

        // Get IDXGISwapChain4 for colorspace setting
        Microsoft::WRL::ComPtr<IDXGISwapChain4> swapchain4;
        HRESULT hr = dxgi_swapchain->QueryInterface(IID_PPV_ARGS(&swapchain4));
        if (FAILED(hr)) {
            LogWarn("Colorspace setting: Failed to get IDXGISwapChain4 from ReShade swapchain");
            return false;
        }

        // Convert ReShade colorspace to DXGI colorspace
        DXGI_COLOR_SPACE_TYPE dxgi_colorspace;
        switch (colorspace) {
            case reshade::api::color_space::srgb_nonlinear:
                dxgi_colorspace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
                break;
            case reshade::api::color_space::extended_srgb_linear:
                dxgi_colorspace = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
                break;
            case reshade::api::color_space::hdr10_st2084:
                dxgi_colorspace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
                break;
            case reshade::api::color_space::hdr10_hlg:
                dxgi_colorspace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020; // HLG uses same primaries
                break;
            default:
                LogWarn("Colorspace setting: Unsupported colorspace");
                return false;
        }

        // Set the colorspace
        hr = swapchain4->SetColorSpace1(dxgi_colorspace);
        if (SUCCEEDED(hr)) {
            LogInfo("Colorspace set successfully");
            return true;
        } else {
            LogWarn("Colorspace setting: Failed to set colorspace");
            return false;
        }

    } catch (...) {
        LogWarn("Colorspace setting: Exception occurred during colorspace setting");
        LogStackTrace("SetColorspace");
        return false;
    }
}

bool DXGIDeviceInfoManager::EnumerateOutputs(IDXGIAdapter* adapter, DXGIAdapterInfo& adapter_info) {
    try {
        if (!adapter) {
            return false;
        }

    int output_idx = 0;
    Microsoft::WRL::ComPtr<IDXGIOutput> output;

    while (SUCCEEDED(adapter->EnumOutputs(output_idx++, &output))) {
        DXGIOutputInfo output_info = {};
        
        // Get basic output description
        DXGI_OUTPUT_DESC desc = {};
        if (SUCCEEDED(output->GetDesc(&desc))) {
            // Convert wide string to UTF-8
            int size_needed = WideCharToMultiByte(CP_UTF8, 0, desc.DeviceName, -1, nullptr, 0, nullptr, nullptr);
            if (size_needed > 0) {
                std::string device_name(size_needed - 1, 0);
                WideCharToMultiByte(CP_UTF8, 0, desc.DeviceName, -1, &device_name[0], size_needed, nullptr, nullptr);
                output_info.device_name = device_name;
            } else {
                output_info.device_name = "Unknown Device";
            }
            output_info.desktop_coordinates = desc.DesktopCoordinates;
            output_info.is_attached = desc.AttachedToDesktop;
            output_info.rotation = desc.Rotation;
            
            // Get monitor name if available
            if (desc.Monitor) {
                MONITORINFOEXW monitor_info = {};
                monitor_info.cbSize = sizeof(MONITORINFOEXW);
                if (GetMonitorInfoW(desc.Monitor, &monitor_info)) {
                    int size_needed = WideCharToMultiByte(CP_UTF8, 0, monitor_info.szDevice, -1, nullptr, 0, nullptr, nullptr);
                    if (size_needed > 0) {
                        std::string monitor_name(size_needed - 1, 0);
                        WideCharToMultiByte(CP_UTF8, 0, monitor_info.szDevice, -1, &monitor_name[0], size_needed, nullptr, nullptr);
                        output_info.monitor_name = monitor_name;
                    }
                }
            }
        }

        // Try to get advanced output information (DXGI 1.6)
        Microsoft::WRL::ComPtr<IDXGIOutput6> output6;
        if (SUCCEEDED(output.As(&output6))) {
            DXGI_OUTPUT_DESC1 desc1 = {};
            if (SUCCEEDED(output6->GetDesc1(&desc1))) {
                // HDR information
                output_info.supports_hdr10 = (desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
                output_info.max_luminance = desc1.MaxLuminance;
                output_info.min_luminance = desc1.MinLuminance;
                // Note: These members don't exist in DXGI_OUTPUT_DESC1, so we'll set them to 0
                output_info.max_frame_average_light_level = 0.0f;
                output_info.max_content_light_level = 0.0f;
                
                // Color space information
                output_info.color_space = static_cast<DXGI_COLOR_SPACE_TYPE>(desc1.ColorSpace);
                output_info.supports_wide_color_gamut = (desc1.ColorSpace != DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
                
                // Get refresh rate from supported modes
                DXGI_MODE_DESC mode_desc = {};
                mode_desc.Width = desc1.DesktopCoordinates.right - desc1.DesktopCoordinates.left;
                mode_desc.Height = desc1.DesktopCoordinates.bottom - desc1.DesktopCoordinates.top;
                mode_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                
                DXGI_MODE_DESC closest_mode = {};
                if (SUCCEEDED(output6->FindClosestMatchingMode(&mode_desc, &closest_mode, nullptr))) {
                    output_info.refresh_rate = closest_mode.RefreshRate;
                }
            }
        }

        // Enumerate supported modes
        UINT num_modes = 0;
        if (SUCCEEDED(output->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &num_modes, nullptr))) {
            if (num_modes > 0) {
                std::vector<DXGI_MODE_DESC> modes(num_modes);
                if (SUCCEEDED(output->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &num_modes, modes.data()))) {
                    output_info.supported_modes = std::move(modes);
                }
            }
        }

        adapter_info.outputs.push_back(std::move(output_info));
        output = nullptr; // Reset for next iteration
    }

    return true;
    } catch (...) {
        LogWarn("Exception occurred in EnumerateOutputs");
        LogStackTrace("EnumerateOutputs");
        return false;
    }
}

bool DXGIDeviceInfoManager::ResetHDRMetadataOnPresent(const std::string& output_device_name, float max_cll) {
    try {
        if (!initialized_) {
            return false;
        }

    // Find the output with the specified device name
    for (const auto& adapter_info : adapters_) {
        for (const auto& output : adapter_info.outputs) {
            if (output.device_name == output_device_name && output.supports_hdr10) {
                // Get the DXGI adapter from ReShade device for HDR reset
                auto* swapchain = g_last_swapchain_ptr.load();
                if (swapchain) {
                    auto* device = swapchain->get_device();
                    if (device) {
                        ID3D11Device* d3d11_device = reinterpret_cast<ID3D11Device*>(device->get_native());
                        if (d3d11_device) {
                            return ResetHDRMetadataForOutput(output, max_cll);
                        }
                    }
                }
                return false;
            }
        }
    }
    
    LogWarn("HDR metadata reset: Output not found or doesn't support HDR10");
    return false;
    } catch (...) {
        LogWarn("Exception occurred in ResetHDRMetadata");
        LogStackTrace("ResetHDRMetadata");
        return false;
    }
}

bool DXGIDeviceInfoManager::ResetHDRMetadataForOutput(const DXGIOutputInfo& output, float max_cll) {
    try {
        // Get ReShade's existing swapchain
        auto* swapchain = g_last_swapchain_ptr.load();
        if (!swapchain) {
            LogWarn("HDR metadata reset: No ReShade swapchain available");
            return false;
        }

        // Get the native DXGI swapchain from ReShade
        IDXGISwapChain* dxgi_swapchain = reinterpret_cast<IDXGISwapChain*>(swapchain->get_native());
        if (!dxgi_swapchain) {
            LogWarn("HDR metadata reset: Failed to get native DXGI swapchain from ReShade");
            return false;
        }

        // Get IDXGISwapChain4 for HDR metadata
        Microsoft::WRL::ComPtr<IDXGISwapChain4> swapchain4;
        HRESULT hr = dxgi_swapchain->QueryInterface(IID_PPV_ARGS(&swapchain4));
        if (FAILED(hr)) {
            LogWarn("HDR metadata reset: Failed to get IDXGISwapChain4 from ReShade swapchain");
            return false;
        }

        // Note: We don't change colorspace during HDR metadata reset
        // Colorspace should be set separately if needed

        // Create HDR metadata
        DXGI_HDR_METADATA_HDR10 metadata = {};
        
        // Use provided max_cll or get from output info
        float max_luminance = (max_cll > 0.0f) ? max_cll : output.max_luminance;
        
        metadata.MinMasteringLuminance = 0;
        metadata.MaxMasteringLuminance = static_cast<UINT>(max_luminance * 10000);
        metadata.MaxFrameAverageLightLevel = static_cast<UINT16>(max_luminance * 10000);
        metadata.MaxContentLightLevel = static_cast<UINT16>(max_luminance * 10000);
        
        // Set color primaries (using standard HDR10 values if not available)
        metadata.WhitePoint[0] = 3127;   // 0.3127 * 10000
        metadata.WhitePoint[1] = 3290;   // 0.3290 * 10000
        metadata.RedPrimary[0] = 6800;    // 0.68 * 10000
        metadata.RedPrimary[1] = 3200;    // 0.32 * 10000
        metadata.GreenPrimary[0] = 2650;  // 0.265 * 10000
        metadata.GreenPrimary[1] = 6900;  // 0.69 * 10000
        metadata.BluePrimary[0] = 1500;   // 0.15 * 10000
        metadata.BluePrimary[1] = 600;    // 0.06 * 10000
        
        // Set HDR metadata
        hr = swapchain4->SetHDRMetaData(
            DXGI_HDR_METADATA_TYPE_HDR10, 
            sizeof(metadata), 
            &metadata
        );
        
        if (SUCCEEDED(hr)) {
            // Present a few frames to ensure metadata is applied
            swapchain4->Present(1, 0);
            Sleep(100);
            swapchain4->Present(1, 0);
            Sleep(100);
            swapchain4->Present(1, 0);
            
            LogInfo(("HDR metadata reset successful for output: " + output.device_name).c_str());
            return true;
        } else {
            LogWarn("HDR metadata reset: Failed to set HDR metadata");
            return false;
        }
        
    } catch (...) {
        LogWarn("HDR metadata reset: Exception occurred during reset");
        LogStackTrace("ResetHDRMetadataForOutput");
        return false;
    }
}

bool DXGIDeviceInfoManager::SetScRGBColorspace() {
    try {
        // Get ReShade's existing swapchain
        auto* swapchain = g_last_swapchain_ptr.load();
        if (!swapchain) {
            LogWarn("scRGB colorspace setting: No ReShade swapchain available");
            return false;
        }

        // Get the native DXGI swapchain from ReShade
        IDXGISwapChain* dxgi_swapchain = reinterpret_cast<IDXGISwapChain*>(swapchain->get_native());
        if (!dxgi_swapchain) {
            LogWarn("scRGB colorspace setting: Failed to get native DXGI swapchain from ReShade");
            return false;
        }

        // Get IDXGISwapChain4 for colorspace setting
        Microsoft::WRL::ComPtr<IDXGISwapChain4> swapchain4;
        HRESULT hr = dxgi_swapchain->QueryInterface(IID_PPV_ARGS(&swapchain4));
        if (FAILED(hr)) {
            LogWarn("scRGB colorspace setting: Failed to get IDXGISwapChain4 from ReShade swapchain");
            return false;
        }

        // Set scRGB colorspace (16-bit linear RGB with extended range)
        hr = swapchain4->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
        if (SUCCEEDED(hr)) {
            LogInfo("scRGB colorspace set successfully");
            return true;
        } else {
            LogWarn("scRGB colorspace setting: Failed to set scRGB colorspace");
            return false;
        }

    } catch (...) {
        LogWarn("scRGB colorspace setting: Exception occurred during colorspace setting");
        LogStackTrace("SetScRGBColorspace");
        return false;
    }
}
