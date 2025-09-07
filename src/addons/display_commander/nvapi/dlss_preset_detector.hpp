#pragma once

#include <windows.h>
#include <string>

// DLSS Preset Detection Module
// Based on Special-K's implementation for detecting DLSS preset information
class DLSSPresetDetector {
public:
    // Constructor
    DLSSPresetDetector();

    // Destructor
    ~DLSSPresetDetector();

    // Initialize the detector
    bool Initialize();

    // Cleanup resources
    void Cleanup();

    // Check if DLSS preset detection is available
    bool IsAvailable() const;

    // Get current DLSS preset information
    struct PresetInfo {
        std::string preset_name;      // A, B, C, D, E, F, G, J, K, Default
        std::string quality_mode;     // Performance, Balanced, Quality, UltraPerformance, UltraQuality, DLAA
        bool valid = false;

        // Get formatted preset string
        std::string getFormattedPreset() const {
            if (!valid) return "Unknown";
            return preset_name;
        }

        // Get formatted quality mode string
        std::string getFormattedQualityMode() const {
            if (!valid) return "Unknown";
            return quality_mode;
        }
    };

    // Get current DLSS preset
    const PresetInfo& GetPreset() const;

    // Force re-detection of DLSS preset
    bool RefreshPreset();

    // Get last error message
    const std::string& GetLastError() const;

private:
    // Internal state
    bool initialized = false;
    bool failed_to_initialize = false;
    PresetInfo current_preset;
    std::string last_error;

    // DLSS preset detection functions
    bool DetectDLSSPreset();
    std::string GetPresetNameFromValue(unsigned int preset_value) const;
    std::string GetQualityModeFromValue(unsigned int quality_value) const;

    // NGX parameter names (based on Special-K's implementation)
    static constexpr const char* NVSDK_NGX_Parameter_PerfQualityValue = "PerfQualityValue";
    static constexpr const char* NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA = "DLSS.Hint.Render.Preset.DLAA";
    static constexpr const char* NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality = "DLSS.Hint.Render.Preset.Quality";
    static constexpr const char* NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced = "DLSS.Hint.Render.Preset.Balanced";
    static constexpr const char* NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance = "DLSS.Hint.Render.Preset.Performance";
    static constexpr const char* NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance = "DLSS.Hint.Render.Preset.UltraPerformance";
    static constexpr const char* NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality = "DLSS.Hint.Render.Preset.UltraQuality";

    // DLSS preset values (based on Special-K's implementation)
    static constexpr unsigned int NVSDK_NGX_DLSS_Hint_Render_Preset_Default = 0;
    static constexpr unsigned int NVSDK_NGX_DLSS_Hint_Render_Preset_A = 1;
    static constexpr unsigned int NVSDK_NGX_DLSS_Hint_Render_Preset_B = 2;
    static constexpr unsigned int NVSDK_NGX_DLSS_Hint_Render_Preset_C = 3;
    static constexpr unsigned int NVSDK_NGX_DLSS_Hint_Render_Preset_D = 4;
    static constexpr unsigned int NVSDK_NGX_DLSS_Hint_Render_Preset_E = 5;
    static constexpr unsigned int NVSDK_NGX_DLSS_Hint_Render_Preset_F = 6;
    static constexpr unsigned int NVSDK_NGX_DLSS_Hint_Render_Preset_G = 7;
    static constexpr unsigned int NVSDK_NGX_DLSS_Hint_Render_Preset_J = 8;
    static constexpr unsigned int NVSDK_NGX_DLSS_Hint_Render_Preset_K = 9;

    // DLSS quality values (based on Special-K's implementation)
    static constexpr unsigned int NVSDK_NGX_PerfQuality_Value_MaxPerf = 0;
    static constexpr unsigned int NVSDK_NGX_PerfQuality_Value_Balanced = 1;
    static constexpr unsigned int NVSDK_NGX_PerfQuality_Value_MaxQuality = 2;
    static constexpr unsigned int NVSDK_NGX_PerfQuality_Value_UltraPerformance = 3;
    static constexpr unsigned int NVSDK_NGX_PerfQuality_Value_UltraQuality = 4;
    static constexpr unsigned int NVSDK_NGX_PerfQuality_Value_DLAA = 5;
};

// Global instance
extern DLSSPresetDetector g_dlssPresetDetector;
