#pragma once

#include <windows.h>
#include <nvapi.h>
#include <string>

// DLSS Preset Manager
// Manages DLSS preset overrides using NVAPI DRS
class DLSSPresetManager {
public:
    // Constructor
    DLSSPresetManager();

    // Destructor
    ~DLSSPresetManager();

    // Initialize NVAPI library
    bool Initialize();

    // Cleanup NVAPI library
    void Cleanup();

    // Check if NVAPI is available
    bool IsAvailable() const;

    // Get current DLSS-SR preset for current application
    int GetCurrentDLSSSRPreset() const;

    // Get current DLSS-RR preset for current application
    int GetCurrentDLSSRRPreset() const;

    // Set DLSS-SR preset for current application
    bool SetDLSSSRPreset(int preset);

    // Set DLSS-RR preset for current application
    bool SetDLSSRRPreset(int preset);

    // Get preset name from preset number
    static std::string GetPresetName(int preset);

    // Get the last error message
    std::string GetLastError() const;

    // Check if we have NVIDIA hardware
    bool HasNVIDIAHardware() const;

private:
    bool initialized = false;
    bool failed_to_initialize = false;
    mutable std::string last_error;

    // Helper function to get current preset
    int GetCurrentPreset(NvU32 settingId) const;

    // Helper function to set preset
    bool SetPreset(NvU32 settingId, int preset);

    // Disable copy constructor and assignment
    DLSSPresetManager(const DLSSPresetManager &) = delete;
    DLSSPresetManager &operator=(const DLSSPresetManager &) = delete;
};

// Global instance
extern DLSSPresetManager g_dlssPresetManager;
