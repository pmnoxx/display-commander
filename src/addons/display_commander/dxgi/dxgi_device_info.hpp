#pragma once

#include <windows.h>
#include <dxgi1_6.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <memory>
#include <sstream>
#include <iomanip>
#include <include/reshade.hpp>

// Forward declarations
struct DXGIAdapterInfo;
struct DXGIOutputInfo;

// DXGI Device Information Manager
class DXGIDeviceInfoManager {
public:
    DXGIDeviceInfoManager();
    ~DXGIDeviceInfoManager();

    // Initialize and cleanup
    bool Initialize();
    void Cleanup();

    // Device enumeration and refresh (called during present)
    void EnumerateDevicesOnPresent();
    void RefreshDeviceInfo();
    
    // Get device information
    const std::vector<DXGIAdapterInfo>& GetAdapters() const { return adapters_; }
    bool IsInitialized() const { return initialized_; }
    
    // HDR metadata reset functionality (called during present)
    bool ResetHDRMetadataOnPresent(const std::string& output_device_name, float max_cll = 0.0f);
    
    // Colorspace setting functionality
    bool SetColorspace(reshade::api::color_space colorspace);
    bool SetScRGBColorspace(); // Special method for scRGB 16-bit

private:
    bool EnumerateOutputs(IDXGIAdapter* adapter, DXGIAdapterInfo& adapter_info);
    bool ResetHDRMetadataForOutput(const DXGIOutputInfo& output, float max_cll);
    bool GetAdapterFromReShadeDevice();
    
    // Stack trace functionality for crash debugging
    void LogStackTrace(const char* context);
    static LONG WINAPI UnhandledExceptionFilter(PEXCEPTION_POINTERS exception_info);

private:
    std::vector<DXGIAdapterInfo> adapters_;
    bool initialized_;
};

// DXGI Adapter Information
struct DXGIAdapterInfo {
    std::string name;
    std::string description;
    uint64_t dedicated_video_memory;
    uint64_t dedicated_system_memory;
    uint64_t shared_system_memory;
    LUID adapter_luid;
    std::vector<DXGIOutputInfo> outputs;
    bool is_software;
};

// DXGI Output Information
struct DXGIOutputInfo {
    std::string device_name;
    RECT desktop_coordinates;
    bool is_attached;
    DXGI_MODE_ROTATION rotation;
    std::string monitor_name;
    
    // HDR information (if available)
    bool supports_hdr10;
    float max_luminance;
    float min_luminance;
    float max_frame_average_light_level;
    float max_content_light_level;
    
    // Color space information
    DXGI_COLOR_SPACE_TYPE color_space;
    bool supports_wide_color_gamut;
    
    // Refresh rate information
    DXGI_RATIONAL refresh_rate;
    std::vector<DXGI_MODE_DESC> supported_modes;
};

// Global instance
extern std::unique_ptr<DXGIDeviceInfoManager> g_dxgiDeviceInfoManager;
