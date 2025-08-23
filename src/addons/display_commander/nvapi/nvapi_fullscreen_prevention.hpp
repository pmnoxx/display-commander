#pragma once

#include <windows.h>
#include "../../external/SpecialK/depends/include/nvapi/nvapi.h"
#include <string>

// NVAPI Fullscreen Prevention Module
// Based on SpecialK's implementation using static linking
class NVAPIFullscreenPrevention {
public:
    // Constructor
    NVAPIFullscreenPrevention();
    
    // Destructor
    ~NVAPIFullscreenPrevention();
    
    // Initialize NVAPI library
    bool Initialize();
    
    // Cleanup NVAPI library
    void Cleanup();
    
    // Check if NVAPI is available
    bool IsAvailable() const;
    
    // Enable/disable fullscreen prevention
    bool SetFullscreenPrevention(bool enable);
    
    // Check if fullscreen prevention is enabled
    bool IsFullscreenPreventionEnabled() const;
    
    // Get the last error message
    std::string GetLastError() const;
    
    // Get driver version
    std::string GetDriverVersion() const;
    
    // Check if we have NVIDIA hardware
    bool HasNVIDIAHardware() const;
    
    // Debug information methods
    std::string GetLibraryPath() const;
    std::string GetFunctionStatus() const;
    std::string GetDetailedStatus() const;
    std::string GetDllVersionInfo() const;

    // HDR related helpers
    bool QueryHdrStatus(bool& out_hdr_enabled, std::string& out_colorspace, std::string& out_output_name) const;
    bool QueryHdrDetails(std::string& out_details) const;
    bool SetHdr10OnAll(bool enable);

private:
    bool initialized = false;
    bool fullscreen_prevention_enabled = false;
    std::string last_error;
    NvDRSSessionHandle hSession = {0};
    NvDRSProfileHandle hProfile = {0};
    
    // Disable copy constructor and assignment
    NVAPIFullscreenPrevention(const NVAPIFullscreenPrevention&) = delete;
    NVAPIFullscreenPrevention& operator=(const NVAPIFullscreenPrevention&) = delete;
};

// Global instance
extern NVAPIFullscreenPrevention g_nvapiFullscreenPrevention;
