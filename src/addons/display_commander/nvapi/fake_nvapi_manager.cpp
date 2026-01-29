#include "fake_nvapi_manager.hpp"
#include "../globals.hpp"
#include "../settings/advanced_tab_settings.hpp"
#include "../utils.hpp"
#include "../utils/logging.hpp"
#include "../utils/srwlock_wrapper.hpp"

#include <filesystem>

namespace nvapi {

// Global instance
FakeNvapiManager g_fakeNvapiManager;

FakeNvapiManager::FakeNvapiManager() = default;

FakeNvapiManager::~FakeNvapiManager() { Cleanup(); }

bool FakeNvapiManager::Initialize() {
    if (is_active_.load()) {
        LogInfo("Fake NVAPI already active");
        return true;
    }

    if (g_shutdown.load()) {
        LogInfo("Fake NVAPI initialization skipped - shutdown in progress");
        return false;
    }

    // Check if fake NVAPI is enabled in settings
    if (!settings::g_advancedTabSettings.fake_nvapi_enabled.GetValue()) {
        LogInfo("Fake NVAPI disabled in settings");
        return false;
    }

    // Detect if real NVIDIA GPU is present
    bool nvidia_detected = DetectNvidiaGpu();
    nvidia_detected_.store(nvidia_detected);

    if (nvidia_detected) {
        LogInfo("Real NVIDIA GPU detected, disabling fake NVAPI");
        return false;
    }

    // Check if fake NVAPI DLL exists (try both nvapi64.dll and fakenvapi.dll)
    if (!CheckFakeNvapiExists()) {
        utils::SRWLockExclusive lock(error_mutex_);
        last_error_ = "Fake nvapi64.dll or fakenvapi.dll not found in addon directory";
        LogInfo("Fake NVAPI: %s", last_error_.c_str());
        return false;
    }

    // Load fake NVAPI
    if (!LoadFakeNvapi()) {
        return false;
    }

    is_active_.store(true);
    override_enabled_.store(true);
    LogInfo("Fake NVAPI initialized successfully for non-NVIDIA system");
    return true;
}

void FakeNvapiManager::Cleanup() {
    if (g_shutdown.load()) {
        LogInfo("Fake NVAPI cleanup skipped - shutdown in progress");
        return;
    }

    if (is_active_.load()) {
        UnloadFakeNvapi();
        is_active_.store(false);
        override_enabled_.store(false);
        LogInfo("Fake NVAPI cleaned up");
    }
}

std::string FakeNvapiManager::GetStatusMessage() const {
    utils::SRWLockExclusive lock(error_mutex_);

    if (nvidia_detected_.load()) {
        return "Real NVIDIA GPU detected - fake NVAPI disabled";
    }

    if (is_active_.load()) {
        return "Fake NVAPI active - spoofing NVIDIA detection";
    }

    if (!is_available_.load()) {
        return last_error_.empty() ? "Fake NVAPI not available" : last_error_;
    }

    return "Fake NVAPI available but not active";
}

FakeNvapiManager::Statistics FakeNvapiManager::GetStatistics() const {
    Statistics stats;
    stats.was_nvapi64_loaded_before_dc = false;  // Always false as requested
    stats.is_nvapi64_loaded = (GetModuleHandleA("nvapi64.dll") != nullptr);
    stats.is_libxell_loaded = (GetModuleHandleA("libxell.dll") != nullptr);
    stats.fake_nvapi_loaded = is_active_.load();
    stats.override_enabled = override_enabled_.load();
    stats.fakenvapi_dll_found = CheckFakenvapiExists();

    {
        utils::SRWLockExclusive lock(error_mutex_);
        stats.last_error = last_error_;
    }

    return stats;
}

bool FakeNvapiManager::DetectNvidiaGpu() {
    // Try to load real NVAPI to detect NVIDIA GPU
    HMODULE real_nvapi = GetModuleHandleA("nvapi64.dll");

    if (!real_nvapi) {
        LogInfo("Fake NVAPI: Real nvapi64.dll not found");
        return false;
    }

    // Try to get NvAPI_Initialize function
    typedef int (*NvAPI_Initialize_t)();
    NvAPI_Initialize_t pInit = (NvAPI_Initialize_t)GetProcAddress(real_nvapi, "NvAPI_Initialize");

    if (!pInit) {
        LogInfo("Fake NVAPI: NvAPI_Initialize not found in real nvapi64.dll");
        FreeLibrary(real_nvapi);
        return false;
    }

    // Try to initialize NVAPI
    int status = pInit();
    if (status == 0) {
        LogInfo("Fake NVAPI: Real NVIDIA GPU detected via NVAPI");
        FreeLibrary(real_nvapi);
        return true;
    }

    LogInfo("Fake NVAPI: No real NVIDIA GPU detected (NVAPI status: %d)", status);
    FreeLibrary(real_nvapi);
    return false;
}

bool FakeNvapiManager::LoadFakeNvapi() {
    if (fake_nvapi_module_) {
        LogInfo("Fake NVAPI already loaded");
        return true;
    }

    // Get the addon directory path
    std::filesystem::path addon_dir = GetAddonDirectory();

    // Try nvapi64.dll first (preferred)
    std::filesystem::path nvapi64_path = addon_dir / "nvapi64.dll";
    if (std::filesystem::exists(nvapi64_path)) {
        LogInfo("Fake NVAPI: Attempting to load %s", nvapi64_path.string().c_str());
        fake_nvapi_module_ = LoadLibraryA(nvapi64_path.string().c_str());
        if (fake_nvapi_module_) {
            is_available_.store(true);
            LogInfo("Fake NVAPI: Successfully loaded nvapi64.dll");
            return true;
        }
    }

    // Try fakenvapi.dll as fallback
    std::filesystem::path fakenvapi_path = addon_dir / "fakenvapi.dll";
    if (std::filesystem::exists(fakenvapi_path)) {
        LogInfo("Fake NVAPI: Attempting to load %s", fakenvapi_path.string().c_str());
        fake_nvapi_module_ = LoadLibraryA(fakenvapi_path.string().c_str());
        if (fake_nvapi_module_) {
            is_available_.store(true);
            LogInfo("Fake NVAPI: Successfully loaded fakenvapi.dll");
            return true;
        }
    }

    // Both failed
    DWORD error = GetLastError();
    utils::SRWLockExclusive lock(error_mutex_);
    last_error_ = "Failed to load fake nvapi64.dll or fakenvapi.dll (Error: " + std::to_string(error) + ")";
    LogError("Fake NVAPI: %s", last_error_.c_str());
    return false;
}

void FakeNvapiManager::UnloadFakeNvapi() {
    if (fake_nvapi_module_) {
        FreeLibrary(fake_nvapi_module_);
        fake_nvapi_module_ = nullptr;
        is_available_.store(false);
        LogInfo("Fake NVAPI: Unloaded fake NVAPI DLL");
    }
}

bool FakeNvapiManager::CheckFakeNvapiExists() {
    std::filesystem::path addon_dir = GetAddonDirectory();

    // Check for nvapi64.dll first (preferred)
    std::filesystem::path nvapi64_path = addon_dir / "nvapi64.dll";
    if (std::filesystem::exists(nvapi64_path)) {
        LogInfo("Fake NVAPI: Found nvapi64.dll at %s", nvapi64_path.string().c_str());
        return true;
    }

    // Check for fakenvapi.dll (legacy/alternative)
    std::filesystem::path fakenvapi_path = addon_dir / "fakenvapi.dll";
    if (std::filesystem::exists(fakenvapi_path)) {
        LogInfo("Fake NVAPI: Found fakenvapi.dll at %s", fakenvapi_path.string().c_str());
        return true;
    }

    LogInfo("Fake NVAPI: Neither nvapi64.dll nor fakenvapi.dll found in addon directory");
    return false;
}

bool FakeNvapiManager::CheckFakenvapiExists() const {
    std::filesystem::path addon_dir = GetAddonDirectory();
    std::filesystem::path fakenvapi_path = addon_dir / "fakenvapi.dll";

    bool exists = std::filesystem::exists(fakenvapi_path);
    if (exists) {
        LogInfo("Fake NVAPI: Found fakenvapi.dll - needs to be renamed to nvapi64.dll");
    }

    return exists;
}

}  // namespace nvapi
