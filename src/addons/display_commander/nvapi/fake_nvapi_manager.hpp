#pragma once

#include <windows.h>
#include <atomic>
#include <mutex>
#include <string>

namespace nvapi {

// Fake NVAPI manager for spoofing NVIDIA detection on non-NVIDIA systems
class FakeNvapiManager {
  public:
    FakeNvapiManager();
    ~FakeNvapiManager();

    // Initialize fake NVAPI if enabled and conditions are met
    bool Initialize();

    // Cleanup fake NVAPI
    void Cleanup();

    // Check if fake NVAPI is currently active
    bool IsActive() const { return is_active_.load(); }

    // Check if fake NVAPI is available (DLL found)
    bool IsAvailable() const { return is_available_.load(); }

    // Get status message for UI display
    std::string GetStatusMessage() const;

    // Get statistics for UI display
    struct Statistics {
        bool was_nvapi64_loaded_before_dc = false;
        bool is_nvapi64_loaded = false;
        bool is_libxell_loaded = false;
        bool fake_nvapi_loaded = false;
        bool override_enabled = false;
        bool fakenvapi_dll_found = false;  // Warning: fakenvapi.dll found (needs renaming)
        std::string last_error;
    };
    Statistics GetStatistics() const;

  private:
    // Detect if real NVIDIA GPU is present
    bool DetectNvidiaGpu();

    // Load fake NVAPI DLL
    bool LoadFakeNvapi();

    // Unload fake NVAPI DLL
    void UnloadFakeNvapi();

    // Check if fake NVAPI DLL exists
    bool CheckFakeNvapiExists();

    // Check if fakenvapi.dll exists (needs renaming)
    bool CheckFakenvapiExists() const;

    // State variables
    std::atomic<bool> is_active_{false};
    std::atomic<bool> is_available_{false};
    std::atomic<bool> nvidia_detected_{false};
    std::atomic<bool> override_enabled_{false};

    // Module handle for fake NVAPI
    HMODULE fake_nvapi_module_{nullptr};

    // Error tracking
    mutable std::string last_error_;
    mutable std::mutex error_mutex_;
};

// Global instance
extern FakeNvapiManager g_fakeNvapiManager;

} // namespace nvapi
