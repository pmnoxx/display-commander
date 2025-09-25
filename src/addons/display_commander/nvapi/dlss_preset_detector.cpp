#include "dlss_preset_detector.hpp"
#include "../utils.hpp"

// Global instance
DLSSPresetDetector g_dlssPresetDetector;

DLSSPresetDetector::DLSSPresetDetector() {}

DLSSPresetDetector::~DLSSPresetDetector() { Cleanup(); }

bool DLSSPresetDetector::Initialize() {
    if (initialized || failed_to_initialize) {
        return initialized;
    }

    LogInfo("Initializing DLSS Preset Detector...");

    // Try to detect DLSS preset
    if (DetectDLSSPreset()) {
        initialized = true;
        LogInfo("DLSS Preset Detector initialized successfully - Preset: %s, Quality: %s",
                current_preset.getFormattedPreset().c_str(), current_preset.getFormattedQualityMode().c_str());
    } else {
        LogInfo("DLSS Preset Detector initialized - No DLSS preset detected");
        initialized = false;
    }

    return initialized;
}

void DLSSPresetDetector::Cleanup() {
    initialized = false;
    failed_to_initialize = false;
    current_preset = PresetInfo();
    last_error.clear();
}

bool DLSSPresetDetector::IsAvailable() const { return initialized && current_preset.valid; }

const DLSSPresetDetector::PresetInfo &DLSSPresetDetector::GetPreset() const { return current_preset; }

bool DLSSPresetDetector::RefreshPreset() {
    if (!initialized) {
        return Initialize();
    }

    // Reset current preset and re-detect
    current_preset = PresetInfo();
    return DetectDLSSPreset();
}

const std::string &DLSSPresetDetector::GetLastError() const { return last_error; }

bool DLSSPresetDetector::DetectDLSSPreset() {
    // For now, we'll implement a basic detection mechanism
    // In a full implementation, this would query the NGX parameters
    // Similar to how Special-K does it with NVSDK_NGX_Parameter_GetUI_Original

    // Check if DLSS DLLs are loaded
    HMODULE hDLSS = GetModuleHandleW(L"nvngx_dlss.dll");
    if (!hDLSS) {
        hDLSS = GetModuleHandleW(L"nvngx_dlss.bin");
    }

    if (!hDLSS) {
        last_error = "DLSS DLL not found";
        return false;
    }

    // For now, we'll set a default preset since we don't have access to NGX parameters
    // In a real implementation, this would query the actual NGX parameters
    current_preset.preset_name = "Default";
    current_preset.quality_mode = "Unknown";
    current_preset.valid = true;

    LogInfo("DLSS preset detected - Preset: %s, Quality: %s", current_preset.getFormattedPreset().c_str(),
            current_preset.getFormattedQualityMode().c_str());

    return true;
}

std::string DLSSPresetDetector::GetPresetNameFromValue(unsigned int preset_value) const {
    switch (preset_value) {
    case NVSDK_NGX_DLSS_Hint_Render_Preset_Default:
        return "Default";
    case NVSDK_NGX_DLSS_Hint_Render_Preset_A:
        return "A";
    case NVSDK_NGX_DLSS_Hint_Render_Preset_B:
        return "B";
    case NVSDK_NGX_DLSS_Hint_Render_Preset_C:
        return "C";
    case NVSDK_NGX_DLSS_Hint_Render_Preset_D:
        return "D";
    case NVSDK_NGX_DLSS_Hint_Render_Preset_E:
        return "E";
    case NVSDK_NGX_DLSS_Hint_Render_Preset_F:
        return "F";
    case NVSDK_NGX_DLSS_Hint_Render_Preset_G:
        return "G";
    case NVSDK_NGX_DLSS_Hint_Render_Preset_J:
        return "J";
    case NVSDK_NGX_DLSS_Hint_Render_Preset_K:
        return "K";
    default:
        return "Unknown";
    }
}

std::string DLSSPresetDetector::GetQualityModeFromValue(unsigned int quality_value) const {
    switch (quality_value) {
    case NVSDK_NGX_PerfQuality_Value_MaxPerf:
        return "Performance";
    case NVSDK_NGX_PerfQuality_Value_Balanced:
        return "Balanced";
    case NVSDK_NGX_PerfQuality_Value_MaxQuality:
        return "Quality";
    case NVSDK_NGX_PerfQuality_Value_UltraPerformance:
        return "UltraPerformance";
    case NVSDK_NGX_PerfQuality_Value_UltraQuality:
        return "UltraQuality";
    case NVSDK_NGX_PerfQuality_Value_DLAA:
        return "DLAA";
    default:
        return "Unknown";
    }
}
