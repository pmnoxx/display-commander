#pragma once

#include <windows.h>

#include <nvapi.h>
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

    // Get the last error message
    std::string GetLastError() const;

    // Get driver version
    std::string GetDriverVersion() const;

    // Check if we have NVIDIA hardware
    bool HasNVIDIAHardware() const;


    // Auto-enable functionality
    static bool IsGameInAutoEnableList(const std::string& processName);
    static void CheckAndAutoEnable();

  private:
    bool initialized = false;
    bool failed_to_initialize = false;
    std::string last_error;

    // Disable copy constructor and assignment
    NVAPIFullscreenPrevention(const NVAPIFullscreenPrevention &) = delete;
    NVAPIFullscreenPrevention &operator=(const NVAPIFullscreenPrevention &) = delete;
};

// Global instance
extern NVAPIFullscreenPrevention g_nvapiFullscreenPrevention;
