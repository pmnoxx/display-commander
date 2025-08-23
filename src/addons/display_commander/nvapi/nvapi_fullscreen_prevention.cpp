#include "nvapi_fullscreen_prevention.hpp"
#include "../utils.hpp"
#include "../../external/SpecialK/depends/include/nvapi/NvApiDriverSettings.h"
#include <sstream>

NVAPIFullscreenPrevention::NVAPIFullscreenPrevention() {
}

NVAPIFullscreenPrevention::~NVAPIFullscreenPrevention() {
    Cleanup();
}

bool NVAPIFullscreenPrevention::Initialize() {
    if (initialized) {
        return true;
    }
    
    // Initialize NVAPI using static linking (like SpecialK)
    NvAPI_Status status = NvAPI_Initialize();
    if (status != NVAPI_OK) {
        std::ostringstream oss;
        oss << "Failed to initialize NVAPI. Status: " << status;
        
        // Provide basic status information
        if (status == NVAPI_API_NOT_INITIALIZED) {
            oss << " (API not initialized)";
        } else if (status == NVAPI_LIBRARY_NOT_FOUND) {
            oss << " (Library not found)";
        } else if (status == NVAPI_NO_IMPLEMENTATION) {
            oss << " (No NVIDIA device found)";
        } else if (status == NVAPI_ERROR) {
            oss << " (General error)";
        }
        
        last_error = oss.str();
        return false;
    }
    
    LogInfo("NVAPI initialized successfully");
    initialized = true;
    return true;
}

void NVAPIFullscreenPrevention::Cleanup() {
    if (hSession) {
        NvAPI_DRS_DestroySession(hSession);
        hSession = {0};
    }
    
    if (initialized) {
        NvAPI_Unload();
        initialized = false;
    }
}

bool NVAPIFullscreenPrevention::IsAvailable() const {
    return initialized;
}

bool NVAPIFullscreenPrevention::SetFullscreenPrevention(bool enable) {
    std::ostringstream oss;
    oss << "SetFullscreenPrevention called with enable=" << (enable ? "true" : "false");
    LogInfo(oss.str().c_str());
    
    if (!initialized) {
        last_error = "NVAPI not initialized";
        LogWarn("SetFullscreenPrevention failed: NVAPI not initialized");
        return false;
    }
    
    // Create DRS session
    LogInfo("Creating DRS session...");
    NvAPI_Status status = NvAPI_DRS_CreateSession(&hSession);
    if (status != NVAPI_OK) {
        last_error = "Failed to create DRS session";
        std::ostringstream oss_err;
        oss_err << "Failed to create DRS session. Status: " << status;
        LogWarn(oss_err.str().c_str());
        return false;
    }
    LogInfo("DRS session created successfully");
    
    // Load settings
    LogInfo("Loading DRS settings...");
    status = NvAPI_DRS_LoadSettings(hSession);
    if (status != NVAPI_OK) {
        last_error = "Failed to load DRS settings";
        std::ostringstream oss_err;
        oss_err << "Failed to load DRS settings. Status: " << status;
        LogWarn(oss_err.str().c_str());
        return false;
    }
    LogInfo("DRS settings loaded successfully");
    
    // Get current executable name
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    char* exeName = strrchr(exePath, '\\');
    if (exeName) exeName++;
    else exeName = exePath;
    
    std::ostringstream oss_exe;
    oss_exe << "Target executable: " << exeName;
    LogInfo(oss_exe.str().c_str());
    
    // Find or create application profile
    NVDRS_APPLICATION app = {0};
    app.version = NVDRS_APPLICATION_VER;
    strcpy_s((char*)app.appName, sizeof(app.appName), exeName);
    
    LogInfo("Searching for existing application profile...");
    status = NvAPI_DRS_FindApplicationByName(hSession, (NvU16*)exeName, &hProfile, &app);
    
    if (status == NVAPI_EXECUTABLE_NOT_FOUND) {
        LogInfo("Application profile not found, creating new one...");
        // Create new profile
        NVDRS_PROFILE profile = {0};
        profile.version = NVDRS_PROFILE_VER;
        profile.isPredefined = FALSE;
        strcpy_s((char*)profile.profileName, sizeof(profile.profileName), "Fullscreen Prevention Profile");
        
        status = NvAPI_DRS_CreateProfile(hSession, &profile, &hProfile);
        if (status != NVAPI_OK) {
            last_error = "Failed to create DRS profile";
            std::ostringstream oss_err;
            oss_err << "Failed to create DRS profile. Status: " << status;
            LogWarn(oss_err.str().c_str());
            return false;
        }
        LogInfo("DRS profile created successfully");
        
        // Add the application to the profile
        app.version = NVDRS_APPLICATION_VER;
        app.isPredefined = FALSE;
        app.isMetro = FALSE;
        strcpy_s((char*)app.appName, sizeof(app.appName), exeName);
        strcpy_s((char*)app.userFriendlyName, sizeof(app.userFriendlyName), exeName);
        
        LogInfo("Adding application to profile...");
        status = NvAPI_DRS_CreateApplication(hSession, hProfile, &app);
        if (status != NVAPI_OK) {
            last_error = "Failed to create application in profile";
            std::ostringstream oss_err;
            oss_err << "Failed to create application in profile. Status: " << status;
            LogWarn(oss_err.str().c_str());
            return false;
        }
        LogInfo("Application added to profile successfully");
    } else if (status == NVAPI_OK) {
        LogInfo("Existing application profile found");
    } else {
        last_error = "Failed to find or create application profile";
        std::ostringstream oss_err;
        oss_err << "Failed to find or create application profile. Status: " << status;
        LogWarn(oss_err.str().c_str());
        return false;
    }
    
    if (status != NVAPI_OK) {
        last_error = "Failed to find or create application profile";
        std::ostringstream oss_err;
        oss_err << "Failed to find or create application profile. Status: " << status;
        LogWarn(oss_err.str().c_str());
        return false;
    }
    
    // Set fullscreen prevention setting using the same approach as SpecialK
    // OGL_DX_PRESENT_DEBUG_ID = 0x20324987
    // DISABLE_FULLSCREEN_OPT = 0x00000001
    // ENABLE_DFLIP_ALWAYS = 0x00000004
    // SIGNAL_PRESENT_END_FROM_CPU = 0x00000020
    // ENABLE_DX_SYNC_INTERVAL = 0x00000080
    // FORCE_INTEROP_GPU_SYNC = 0x00000200
    // ENABLE_DXVK = 0x00080000
    
    NVDRS_SETTING setting = {0};
    setting.version = NVDRS_SETTING_VER;
    setting.settingId = 0x20324987; // OGL_DX_PRESENT_DEBUG_ID from SpecialK
    setting.settingType = NVDRS_DWORD_TYPE;
    setting.settingLocation = NVDRS_CURRENT_PROFILE_LOCATION;
    
    if (enable) {
        // Use the same flags as SpecialK for optimal interop
        setting.u32CurrentValue = 0x00000001 | 0x00000004 | 0x00000020 | 0x00000080 | 0x00000200 | 0x00080000;
        // This includes: DISABLE_FULLSCREEN_OPT | ENABLE_DFLIP_ALWAYS | SIGNAL_PRESENT_END_FROM_CPU | ENABLE_DX_SYNC_INTERVAL | FORCE_INTEROP_GPU_SYNC | ENABLE_DXVK
        std::ostringstream oss_flags;
        oss_flags << "Setting fullscreen prevention flags: 0x" << std::hex << setting.u32CurrentValue << std::dec;
        LogInfo(oss_flags.str().c_str());
    } else {
        setting.u32CurrentValue = 0x00000000; // Disable all flags
        LogInfo("Disabling all fullscreen prevention flags");
    }
    
    LogInfo("Applying DRS setting...");
    status = NvAPI_DRS_SetSetting(hSession, hProfile, &setting);
    if (status != NVAPI_OK) {
        last_error = "Failed to set DRS setting";
        std::ostringstream oss_err;
        oss_err << "Failed to set DRS setting. Status: " << status;
        LogWarn(oss_err.str().c_str());
        return false;
    }
    LogInfo("DRS setting applied successfully");
    
    // Save settings
    LogInfo("Saving DRS settings...");
    status = NvAPI_DRS_SaveSettings(hSession);
    if (status != NVAPI_OK) {
        last_error = "Failed to save DRS settings";
        std::ostringstream oss_err;
        oss_err << "Failed to save DRS settings. Status: " << status;
        LogWarn(oss_err.str().c_str());
        return false;
    }
    LogInfo("DRS settings saved successfully");
    
    fullscreen_prevention_enabled = enable;
    std::ostringstream oss_success;
    oss_success << "Fullscreen prevention " << (enable ? "enabled" : "disabled") << " successfully";
    LogInfo(oss_success.str().c_str());
    return true;
}

bool NVAPIFullscreenPrevention::IsFullscreenPreventionEnabled() const {
    if (!initialized) {
        return false;
    }
    
    // Query the actual DRS setting from the driver instead of returning cached value
    NvAPI_Status status;
    NvDRSSessionHandle hSession = {0};
    NvDRSProfileHandle hProfile = {0};
    
    // Create DRS session
    status = NvAPI_DRS_CreateSession(&hSession);
    if (status != NVAPI_OK) {
        LogDebug("IsFullscreenPreventionEnabled: Failed to create DRS session for query");
        return false;
    }
    
    // Load settings
    status = NvAPI_DRS_LoadSettings(hSession);
    if (status != NVAPI_OK) {
        LogDebug("IsFullscreenPreventionEnabled: Failed to load DRS settings for query");
        NvAPI_DRS_DestroySession(hSession);
        return false;
    }
    
    // Get current executable name
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    char* exeName = strrchr(exePath, '\\');
    if (exeName) exeName++;
    else exeName = exePath;
    
    // Find application profile
    NVDRS_APPLICATION app = {0};
    app.version = NVDRS_APPLICATION_VER;
    strcpy_s((char*)app.appName, sizeof(app.appName), exeName);
    
    status = NvAPI_DRS_FindApplicationByName(hSession, (NvU16*)exeName, &hProfile, &app);
    if (status != NVAPI_OK) {
        LogDebug("IsFullscreenPreventionEnabled: Application profile not found");
        NvAPI_DRS_DestroySession(hSession);
        return false;
    }
    
    // Query the actual setting value
    NVDRS_SETTING setting = {0};
    setting.version = NVDRS_SETTING_VER;
    setting.settingId = 0x20324987; // OGL_DX_PRESENT_DEBUG_ID
    
    status = NvAPI_DRS_GetSetting(hSession, hProfile, setting.settingId, &setting);
    if (status != NVAPI_OK) {
        LogDebug("IsFullscreenPreventionEnabled: Failed to get DRS setting");
        NvAPI_DRS_DestroySession(hSession);
        return false;
    }
    
    // Clean up
    NvAPI_DRS_DestroySession(hSession);
    
    // Check if fullscreen prevention flags are set
    bool is_enabled = (setting.u32CurrentValue & 0x00000001) != 0; // DISABLE_FULLSCREEN_OPT flag
    
    std::ostringstream oss;
    oss << "IsFullscreenPreventionEnabled: Query result - setting value: 0x" << std::hex << setting.u32CurrentValue << ", fullscreen prevention: " << (is_enabled ? "ENABLED" : "DISABLED");
    LogDebug(oss.str().c_str());
    
    return is_enabled;
}

std::string NVAPIFullscreenPrevention::GetLastError() const {
    return last_error;
}

std::string NVAPIFullscreenPrevention::GetDriverVersion() const {
    if (!initialized) {
        return "NVAPI not initialized";
    }
    
    NvU32 driverVersion = 0;
    NvAPI_ShortString branchString = {0};
    NvAPI_Status status = NvAPI_SYS_GetDriverAndBranchVersion(&driverVersion, branchString);
    if (status != NVAPI_OK) {
        return "Failed to get driver version";
    }
    
    // Format the driver version similar to SpecialK
    char ver_str[64];
    snprintf(ver_str, sizeof(ver_str), "%03lu.%02lu", driverVersion / 100, driverVersion % 100);
    return std::string(ver_str);
}

bool NVAPIFullscreenPrevention::HasNVIDIAHardware() const {
    if (!initialized) {
        LogWarn("HasNVIDIAHardware called but NVAPI not initialized");
        return false;
    }
    
    NvU32 gpuCount = 0;
    NvPhysicalGpuHandle gpus[64] = {0};
    NvAPI_Status status = NvAPI_EnumPhysicalGPUs(gpus, &gpuCount);
    
    std::ostringstream oss;
    oss << "NvAPI_EnumPhysicalGPUs returned status: " << status << ", GPU count: " << gpuCount;
    LogInfo(oss.str().c_str());
    
    bool hasHardware = (status == NVAPI_OK && gpuCount > 0);
    std::ostringstream oss_result;
    oss_result << "NVIDIA hardware detection: " << (hasHardware ? "SUCCESS" : "FAILED");
    LogInfo(oss_result.str().c_str());
    
    return hasHardware;
}

std::string NVAPIFullscreenPrevention::GetLibraryPath() const {
    return "Static linking - no DLL path";
}

std::string NVAPIFullscreenPrevention::GetFunctionStatus() const {
    if (!initialized) {
        return "NVAPI not initialized";
    }
    
    std::ostringstream oss;
    oss << "Library: Static linking (nvapi64.lib)\n";
    oss << "Core Functions: ✓ Initialize, ✓ Unload\n";
    oss << "DRS Functions: ✓ CreateSession, ✓ DestroySession, ✓ LoadSettings, ✓ SaveSettings\n";
    oss << "Profile Functions: ✓ FindApp, ✓ CreateProfile, ✓ SetSetting\n";
    oss << "System Functions: ✓ GetDriverVersion, ✓ EnumGPUs";
    
    return oss.str();
}

std::string NVAPIFullscreenPrevention::GetDetailedStatus() const {
    std::ostringstream oss;
    
    oss << "=== NVAPI Detailed Status ===\n";
    oss << "Initialized: " << (initialized ? "Yes" : "No") << "\n";
    oss << "Library: Static linking (nvapi64.lib)\n";
    oss << "Function Status:\n" << GetFunctionStatus() << "\n";
    
    if (initialized) {
        oss << "Session Handle: " << (hSession ? "Valid" : "Invalid") << "\n";
        oss << "Profile Handle: " << (hProfile ? "Valid" : "Invalid") << "\n";
        oss << "Fullscreen Prevention: " << (fullscreen_prevention_enabled ? "Enabled" : "Disabled") << "\n";
        
        if (hSession) {
            oss << "DRS Session: Active\n";
        }
        if (hProfile) {
            oss << "DRS Profile: Active\n";
        }
    }
    
    if (!last_error.empty()) {
        oss << "Last Error: " << last_error << "\n";
    }
    
    return oss.str();
}

std::string NVAPIFullscreenPrevention::GetDllVersionInfo() const {
    if (!initialized) {
        return "NVAPI not initialized";
    }
    
    std::ostringstream oss;
    oss << "Static linking with nvapi64.lib\n";
    oss << "No DLL path - functions resolved at link time\n";
    oss << "Architecture: 64-bit (x64)";
    
    return oss.str();
}

// Global instance
NVAPIFullscreenPrevention g_nvapiFullscreenPrevention;

bool NVAPIFullscreenPrevention::QueryHdrStatus(bool& out_hdr_enabled, std::string& out_colorspace, std::string& out_output_name) const {
    out_hdr_enabled = false;
    out_colorspace.clear();
    out_output_name.clear();
    if (!initialized) {
        return false;
    }

    NvU32 gpuCount = 0;
    NvPhysicalGpuHandle gpus[64] = {0};
    NvAPI_Status status = NvAPI_EnumPhysicalGPUs(gpus, &gpuCount);
    if (status != NVAPI_OK || gpuCount == 0) {
        return false;
    }

    for (NvU32 g = 0; g < gpuCount; ++g) {
        // First query how many display IDs exist
        NvU32 count = 0;
        status = NvAPI_GPU_GetAllDisplayIds(gpus[g], nullptr, &count);
        if (status != NVAPI_OK || count == 0) {
            continue;
        }
        std::vector<NV_GPU_DISPLAYIDS> displayIds(count);
        for (NvU32 i = 0; i < count; ++i) displayIds[i].version = NV_GPU_DISPLAYIDS_VER;
        status = NvAPI_GPU_GetAllDisplayIds(gpus[g], displayIds.data(), &count);
        if (status != NVAPI_OK || count == 0) {
            continue;
        }

        for (NvU32 i = 0; i < count; ++i) {
            const NV_GPU_DISPLAYIDS& did = displayIds[i];
            if (did.isConnected == 0) continue;

            // Read current HDR mode
            NV_HDR_COLOR_DATA color = {};
            color.version = NV_HDR_COLOR_DATA_VER;
            color.cmd = NV_HDR_CMD_GET;
            status = NvAPI_Disp_HdrColorControl(did.displayId, &color);
            if (status != NVAPI_OK) {
                continue;
            }

            bool hdr_enabled = (color.hdrMode != NV_HDR_MODE_OFF);

            // Read HDR capabilities to report color space tastefully
            NV_HDR_CAPABILITIES caps = {};
            caps.version = NV_HDR_CAPABILITIES_VER;
            if (NvAPI_Disp_GetHdrCapabilities(did.displayId, &caps) == NVAPI_OK) {
                if (caps.isST2084EotfSupported) out_colorspace = "HDR10 ST2084";
                else if (caps.isTraditionalHdrGammaSupported) out_colorspace = "HDR (Traditional)";
                else if (caps.isHdr10PlusSupported) out_colorspace = "HDR10+";
                else if (caps.isHdr10PlusGamingSupported) out_colorspace = "HDR10+ Gaming";
                else out_colorspace = "SDR/sRGB";
            } else {
                out_colorspace = hdr_enabled ? "HDR" : "SDR";
            }

            out_hdr_enabled = hdr_enabled;
            out_output_name = std::string("DisplayId=") + std::to_string(did.displayId);
            return true;
        }
    }

    return false;
}

bool NVAPIFullscreenPrevention::QueryHdrDetails(std::string& out_details) const {
    out_details.clear();
    if (!initialized) return false;

    NvU32 gpuCount = 0;
    NvPhysicalGpuHandle gpus[64] = {0};
    if (NvAPI_EnumPhysicalGPUs(gpus, &gpuCount) != NVAPI_OK || gpuCount == 0) {
        return false;
    }

    std::ostringstream oss;
    oss << "=== NVAPI HDR Details ===\n";

    for (NvU32 g = 0; g < gpuCount; ++g) {
        NvU32 count = 0;
        if (NvAPI_GPU_GetAllDisplayIds(gpus[g], nullptr, &count) != NVAPI_OK || count == 0) continue;
        std::vector<NV_GPU_DISPLAYIDS> displayIds(count);
        for (NvU32 i = 0; i < count; ++i) displayIds[i].version = NV_GPU_DISPLAYIDS_VER;
        if (NvAPI_GPU_GetAllDisplayIds(gpus[g], displayIds.data(), &count) != NVAPI_OK || count == 0) continue;

        for (NvU32 i = 0; i < count; ++i) {
            const auto& did = displayIds[i];
            if (did.isConnected == 0) continue;

            NV_HDR_COLOR_DATA color = {};
            color.version = NV_HDR_COLOR_DATA_VER;
            color.cmd = NV_HDR_CMD_GET;
            NvAPI_Status sc = NvAPI_Disp_HdrColorControl(did.displayId, &color);

            NV_HDR_CAPABILITIES caps = {};
            caps.version = NV_HDR_CAPABILITIES_VER;
            NvAPI_Status sc2 = NvAPI_Disp_GetHdrCapabilities(did.displayId, &caps);

            oss << "DisplayId=" << did.displayId << "\n";
            if (sc == NVAPI_OK) {
                oss << "  HdrMode=" << (int)color.hdrMode << " (0=OFF,2=UHDA)\n";
                oss << "  StaticMetadataId=" << (int)color.static_metadata_descriptor_id << "\n";
                const auto& md = color.mastering_display_data;
                oss << "  MasteringPrimaries: R(" << md.displayPrimary_x0 << "," << md.displayPrimary_y0
                    << ") G(" << md.displayPrimary_x1 << "," << md.displayPrimary_y1
                    << ") B(" << md.displayPrimary_x2 << "," << md.displayPrimary_y2 << ")\n";
                oss << "  MasteringWhite: (" << md.displayWhitePoint_x << "," << md.displayWhitePoint_y << ")\n";
                oss << "  MaxMasteringLuminance=" << md.max_display_mastering_luminance
                    << "  MinMasteringLuminance=" << md.min_display_mastering_luminance << "\n";
                oss << "  MaxCLL=" << md.max_content_light_level
                    << "  MaxFALL=" << md.max_frame_average_light_level << "\n";
            } else {
                oss << "  HdrColorControl: FAILED (" << sc << ")\n";
            }

            if (sc2 == NVAPI_OK) {
                oss << "  Caps: ST2084Supported=" << (caps.isST2084EotfSupported ? 1 : 0)
                    << " TraditionalHdrGamma=" << (caps.isTraditionalHdrGammaSupported ? 1 : 0)
                    << " SDRGamma=" << (caps.isTraditionalSdrGammaSupported ? 1 : 0)
                    << " DolbyVision=" << (caps.isDolbyVisionSupported ? 1 : 0)
                    << " HDR10+=" << (caps.isHdr10PlusSupported ? 1 : 0)
                    << " HDR10+Gaming=" << (caps.isHdr10PlusGamingSupported ? 1 : 0) << "\n";
                const auto& sd = caps.display_data;
                oss << "  StaticMetadata(ST2086): R(" << sd.displayPrimary_x0 << "," << sd.displayPrimary_y0
                    << ") G(" << sd.displayPrimary_x1 << "," << sd.displayPrimary_y1
                    << ") B(" << sd.displayPrimary_x2 << "," << sd.displayPrimary_y2 << ")\n";
                oss << "  WhitePoint(" << sd.displayWhitePoint_x << "," << sd.displayWhitePoint_y << ")\n";
                oss << "  DesiredContent: MaxLum=" << sd.desired_content_max_luminance
                    << " MinLum=" << sd.desired_content_min_luminance
                    << " MaxFALL=" << sd.desired_content_max_frame_average_luminance << "\n";
            } else {
                oss << "  GetHdrCapabilities: FAILED (" << sc2 << ")\n";
            }
        }
    }

    out_details = oss.str();
    return true;
}

// Enable/disable HDR10 (UHDA) on all connected displays for this GPU set
bool NVAPIFullscreenPrevention::SetHdr10OnAll(bool enable) {
    if (!initialized) return false;

    NvU32 gpuCount = 0;
    NvPhysicalGpuHandle gpus[64] = {0};
    if (NvAPI_EnumPhysicalGPUs(gpus, &gpuCount) != NVAPI_OK || gpuCount == 0) {
        return false;
    }

    bool any_ok = false;
    for (NvU32 g = 0; g < gpuCount; ++g) {
        NvU32 count = 0;
        if (NvAPI_GPU_GetAllDisplayIds(gpus[g], nullptr, &count) != NVAPI_OK || count == 0) continue;
        std::vector<NV_GPU_DISPLAYIDS> displayIds(count);
        for (NvU32 i = 0; i < count; ++i) displayIds[i].version = NV_GPU_DISPLAYIDS_VER;
        if (NvAPI_GPU_GetAllDisplayIds(gpus[g], displayIds.data(), &count) != NVAPI_OK || count == 0) continue;

        for (NvU32 i = 0; i < count; ++i) {
            const auto& did = displayIds[i];
            if (did.isConnected == 0) continue;

            NV_HDR_COLOR_DATA color = {};
            color.version = NV_HDR_COLOR_DATA_VER;
            color.cmd = NV_HDR_CMD_SET;
            color.hdrMode = enable ? NV_HDR_MODE_UHDA : NV_HDR_MODE_OFF;

            // Fill minimal valid static metadata. Using conservative BT.2020 primaries and sane luminance if needed.
            color.static_metadata_descriptor_id = NV_STATIC_METADATA_TYPE_1;
            auto& md = color.mastering_display_data;
            // Defaults based on typical HDR10 metadata (values in NVAPI units)
            md.displayPrimary_x0 = 34000; md.displayPrimary_y0 = 16000; // R
            md.displayPrimary_x1 = 13250; md.displayPrimary_y1 = 34500; // G
            md.displayPrimary_x2 = 7500;  md.displayPrimary_y2 = 3000;  // B
            md.displayWhitePoint_x = 15635; md.displayWhitePoint_y = 16450; // D65 approx
            md.max_display_mastering_luminance = 1000; // nits
            md.min_display_mastering_luminance = 1;    // 0.0001 cd/m^2 in units? NVAPI uses direct integers; device will clamp
            md.max_content_light_level = 1000;         // MaxCLL
            md.max_frame_average_light_level = 400;    // MaxFALL

            NvAPI_Status sc = NvAPI_Disp_HdrColorControl(did.displayId, &color);
            if (sc == NVAPI_OK) any_ok = true;
        }
    }

    return any_ok;
}