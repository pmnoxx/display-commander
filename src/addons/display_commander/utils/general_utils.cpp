#include "general_utils.hpp"
#include <d3d9.h>
#include <MinHook.h>
#include <algorithm>
#include <cstdio>
#include <reshade.hpp>
#include <sstream>
#include <string>
#include <vector>
#include "../hooks/hook_suppression_manager.hpp"
#include "globals.hpp"
#include "logging.hpp"
#include "settings/developer_tab_settings.hpp"

// Version.dll dynamic loading
namespace {
HMODULE s_version_dll = nullptr;

// Function pointers for version.dll functions
typedef DWORD(WINAPI* PFN_GetFileVersionInfoSizeW)(LPCWSTR lptstrFilename, LPDWORD lpdwHandle);
typedef BOOL(WINAPI* PFN_GetFileVersionInfoW)(LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData);
typedef BOOL(WINAPI* PFN_VerQueryValueW)(LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen);

PFN_GetFileVersionInfoSizeW s_GetFileVersionInfoSizeW = nullptr;
PFN_GetFileVersionInfoW s_GetFileVersionInfoW = nullptr;
PFN_VerQueryValueW s_VerQueryValueW = nullptr;

// Load version.dll and get function pointers
// Always load from System32 to avoid circular dependency when Display Commander is loaded as version.dll
bool LoadVersionDLL() {
    if (s_version_dll != nullptr) {
        return true;  // Already loaded
    }

    // Load from System32 to avoid loading ourselves if we're loaded as version.dll
    WCHAR system_path[MAX_PATH];
    GetSystemDirectoryW(system_path, MAX_PATH);
    std::wstring version_path = std::wstring(system_path) + L"\\version.dll";

    s_version_dll = LoadLibraryW(version_path.c_str());
    if (s_version_dll == nullptr) {
        return false;
    }

    // Get function pointers
    s_GetFileVersionInfoSizeW =
        reinterpret_cast<PFN_GetFileVersionInfoSizeW>(GetProcAddress(s_version_dll, "GetFileVersionInfoSizeW"));
    s_GetFileVersionInfoW =
        reinterpret_cast<PFN_GetFileVersionInfoW>(GetProcAddress(s_version_dll, "GetFileVersionInfoW"));
    s_VerQueryValueW = reinterpret_cast<PFN_VerQueryValueW>(GetProcAddress(s_version_dll, "VerQueryValueW"));

    // Check if all functions were loaded successfully
    if (s_GetFileVersionInfoSizeW == nullptr || s_GetFileVersionInfoW == nullptr || s_VerQueryValueW == nullptr) {
        FreeLibrary(s_version_dll);
        s_version_dll = nullptr;
        s_GetFileVersionInfoSizeW = nullptr;
        s_GetFileVersionInfoW = nullptr;
        s_VerQueryValueW = nullptr;
        return false;
    }

    return true;
}
}  // anonymous namespace

// Constant definitions
const int WIDTH_OPTIONS[] = {0, 1280, 1366, 1600, 1920, 2560, 3440, 3840};  // 0 = current monitor width
const int HEIGHT_OPTIONS[] = {0, 720, 900, 1080, 1200, 1440, 1600, 2160};   // 0 = current monitor height
const AspectRatio ASPECT_OPTIONS[] = {
    {3, 2},     // 1.5:1
    {4, 3},     // 1.333:1
    {16, 10},   // 1.6:1
    {16, 9},    // 1.778:1
    {19, 9},    // 2.111:1
    {195, 90},  // 2.167:1 (19.5:9)
    {21, 9},    // 2.333:1
    {32, 9},    // 3.556:1
};

// Helper function implementations
RECT RectFromWH(int width, int height) {
    RECT rect = {0, 0, width, height};
    return rect;
}

// Utility function implementations

AspectRatio GetAspectByIndex(AspectRatioType aspect_type) {
    int index = static_cast<int>(aspect_type);
    if (index >= 0 && index < 8) {
        return ASPECT_OPTIONS[index];
    }
    return {16, 9};  // Default to 16:9
}

// Helper function to get the actual width value based on the dropdown selection
int GetAspectWidthValue(int display_width) {
    const int width_index = s_aspect_width.load();

    // Width options: 0=Display Width, 1=3840, 2=2560, 3=1920, 4=1600, 5=1280, 6=1080, 7=900, 8=720
    int selected_width;
    switch (width_index) {
        case 0:  selected_width = display_width; break;  // Display Width
        case 1:  selected_width = 3840; break;
        case 2:  selected_width = 2560; break;
        case 3:  selected_width = 1920; break;
        case 4:  selected_width = 1600; break;
        case 5:  selected_width = 1280; break;
        case 6:  selected_width = 1080; break;
        case 7:  selected_width = 900; break;
        case 8:  selected_width = 720; break;
        default: selected_width = display_width; break;  // Fallback to display width
    }

    // Ensure the selected width doesn't exceed the display width
    return min(selected_width, display_width);
}

void ComputeDesiredSize(int display_width, int display_height, int& out_w, int& out_h) {
    if (s_window_mode.load() == WindowMode::kFullscreen) {
        // kFullscreen: Borderless Fullscreen - use current monitor dimensions
        out_w = display_width;
        out_h = display_height;
        return;
    }

    // kAspectRatio: Borderless Windowed (Aspect Ratio) - aspect mode
    // Get the selected width from the dropdown
    const int want_w = GetAspectWidthValue(display_width);
    AspectRatio ar = GetAspectByIndex(s_aspect_index.load());
    // height = round(width * h / w)
    // prevent division by zero
    if (ar.w <= 0 || ar.h <= 0) {
        ar.h = 16;
        ar.w = 9;
    }
    out_w = want_w;
    out_h = want_w * ar.h / ar.w;

    // LogInfo("ComputeDesiredSize: out_w=%d, out_h=%d (width_index=%d)", out_w, out_h, s_aspect_width.load());
}

// Monitor enumeration callback
BOOL CALLBACK MonitorEnumProc(HMONITOR hmon, HDC hdc, LPRECT rect, LPARAM lparam) {
    MONITORINFOEXW info;
    info.cbSize = sizeof(MONITORINFOEXW);

    if (GetMonitorInfoW(hmon, &info)) {
        MonitorInfo monitor_info;
        monitor_info.handle = hmon;
        monitor_info.info = info;
        auto* monitors = reinterpret_cast<std::vector<MonitorInfo>*>(lparam);
        if (monitors) {
            monitors->push_back(monitor_info);
        }
    }

    return TRUE;
}

// XInput processing functions
// Process stick input with radial deadzone (preserves direction)
void ProcessStickInputRadial(float& x, float& y, float deadzone, float max_input, float min_output) {
    // Calculate magnitude (distance from center)
    float magnitude = std::sqrt(x * x + y * y);

    // If magnitude is zero, nothing to process
    if (magnitude < 0.0001f) {
        x = 0.0f;
        y = 0.0f;
        return;
    }

    // Calculate normalized direction
    // Step 1: Apply radial deadzone
    if (magnitude < deadzone) {
        // Within deadzone - zero out
        x = 0.0f;
        y = 0.0f;
        return;
    }

    // Step 2: Apply max_input scaling (e.g., 0.7 max input maps to 1.0 max output)
    // Scale magnitude from [deadzone, max_input] to [0, 1]
    float scaled_magnitude = (std::min)(1.0f, max(0.0f, magnitude - deadzone) / (max_input - deadzone));

    // Step 3: Apply min_output mapping (e.g., 0.3 min output maps 0.0-1.0 to 0.3-1.0)
    float output_magnitude = min_output + (scaled_magnitude * (1.0f - min_output));

    // Step 4: Clamp to valid range [0.0, 1.0]
    output_magnitude = std::clamp(output_magnitude, 0.0f, 1.0f);

    // Reconstruct x and y with original direction but new magnitude
    x = x * output_magnitude / magnitude;
    y = y * output_magnitude / magnitude;
}

// Process stick input with square deadzone (processes X and Y axes separately)
void ProcessStickInputSquare(float& x, float& y, float deadzone, float max_input, float min_output) {
    // Process X axis independently
    float abs_x = std::abs(x);
    float sign_x = (x >= 0.0f) ? 1.0f : -1.0f;

    // Step 1: Apply square deadzone to X axis
    if (abs_x < deadzone) {
        x = 0.0f;
    } else {
        // Step 2: Apply max_input scaling to X axis
        // Scale from [deadzone, max_input] to [0, 1]
        float scaled_x = (std::min)(1.0f, max(0.0f, (abs_x - deadzone) / (max_input - deadzone)));

        // Step 3: Apply min_output mapping to X axis
        float output_x = min_output + (scaled_x * (1.0f - min_output));

        // Step 4: Clamp and restore sign
        output_x = std::clamp(output_x, 0.0f, 1.0f);
        x = sign_x * output_x;
    }

    // Process Y axis independently
    float abs_y = std::abs(y);
    float sign_y = (y >= 0.0f) ? 1.0f : -1.0f;

    // Step 1: Apply square deadzone to Y axis
    if (abs_y < deadzone) {
        y = 0.0f;
    } else {
        // Step 2: Apply max_input scaling to Y axis
        // Scale from [deadzone, max_input] to [0, 1]
        float scaled_y = (std::min)(1.0f, max(0.0f, (abs_y - deadzone) / (max_input - deadzone)));

        // Step 3: Apply min_output mapping to Y axis
        float output_y = min_output + (scaled_y * (1.0f - min_output));

        // Step 4: Clamp and restore sign
        output_y = std::clamp(output_y, 0.0f, 1.0f);
        y = sign_y * output_y;
    }
}

// XInput thumbstick scaling helpers (handles asymmetric SHORT range: -32768 to 32767)
float ShortToFloat(SHORT value) {
    // Proper linear mapping from [-32768, 32767] to [-1.0f, 1.0f]
    // Using the full range: 32767 - (-32768) = 65535
    // Center point: (32767 + (-32768)) / 2 = -0.5
    // So we map: (value - (-32768)) / 65535 * 2.0f - 1.0f
    return (static_cast<float>(value) - (-32768.0f)) / 65535.0f * 2.0f - 1.0f;
}

SHORT FloatToShort(float value) {
    // Clamp to valid range
    value = max(-1.0f, min(1.0f, value));

    // Inverse mapping from [-1.0f, 1.0f] to [-32768, 32767]
    // (value + 1.0f) / 2.0f * 65535.0f + (-32768.0f)
    return static_cast<SHORT>((value + 1.0f) / 2.0f * 65535.0f + (-32768.0f));
}

// Get DLL version string (e.g., "570.6.2")
std::string GetDLLVersionString(const std::wstring& dllPath) {
    // Load version.dll dynamically if not already loaded
    if (!LoadVersionDLL()) {
        LogWarn("GetDLLVersionString: Failed to load version.dll");
        return "Unknown";
    }

    DWORD versionInfoSize = s_GetFileVersionInfoSizeW(dllPath.c_str(), nullptr);
    if (versionInfoSize == 0) {
        return "Unknown";
    }

    std::vector<BYTE> versionInfo(versionInfoSize);
    if (!s_GetFileVersionInfoW(dllPath.c_str(), 0, versionInfoSize, versionInfo.data())) {
        return "Unknown";
    }

    VS_FIXEDFILEINFO* fileInfo = nullptr;
    UINT fileInfoSize = 0;

    if (!s_VerQueryValueW(versionInfo.data(), L"\\", reinterpret_cast<LPVOID*>(&fileInfo), &fileInfoSize)) {
        return "Unknown";
    }

    if (fileInfo == nullptr || fileInfoSize == 0) {
        return "Unknown";
    }

    // Extract version numbers
    DWORD major = HIWORD(fileInfo->dwFileVersionMS);
    DWORD minor = LOWORD(fileInfo->dwFileVersionMS);
    DWORD build = HIWORD(fileInfo->dwFileVersionLS);
    DWORD revision = LOWORD(fileInfo->dwFileVersionLS);

    // Format as "major.minor.build.revision" (similar to Special-K)
    char versionStr[64];
    snprintf(versionStr, sizeof(versionStr), "%lu.%lu.%lu.%lu", major, minor, build, revision);

    return std::string(versionStr);
}

// Convert device API enum to readable string
const char* GetDeviceApiString(reshade::api::device_api api) {
    switch (api) {
        case reshade::api::device_api::d3d9:   return "Direct3D 9";
        case reshade::api::device_api::d3d10:  return "Direct3D 10";
        case reshade::api::device_api::d3d11:  return "Direct3D 11";
        case reshade::api::device_api::d3d12:  return "Direct3D 12";
        case reshade::api::device_api::opengl: return "OpenGL";
        case reshade::api::device_api::vulkan: return "Vulkan";
        default:                               return "Unknown";
    }
}

// Convert device API version to readable string with feature level
std::string GetDeviceApiVersionString(reshade::api::device_api api, uint32_t api_version) {
    if (api_version == 0) {
        return GetDeviceApiString(api);
    }

    char buffer[128];

    switch (api) {
        case reshade::api::device_api::d3d9:
            // Check if D3D9 was upgraded to D3D9Ex
            if (s_d3d9e_upgrade_successful.load()) {
                snprintf(buffer, sizeof(buffer), "Direct3D 9Ex");
            } else {
                snprintf(buffer, sizeof(buffer), "Direct3D 9");
            }
            break;
        case reshade::api::device_api::d3d10:
        case reshade::api::device_api::d3d11:
        case reshade::api::device_api::d3d12: {
            // D3D feature levels are encoded as hex values
            // D3D_FEATURE_LEVEL_10_0 = 0xa000 (10.0)
            // D3D_FEATURE_LEVEL_10_1 = 0xa100 (10.1)
            // D3D_FEATURE_LEVEL_11_0 = 0xb000 (11.0)
            // D3D_FEATURE_LEVEL_11_1 = 0xb100 (11.1)
            // D3D_FEATURE_LEVEL_12_0 = 0xc000 (12.0)
            // D3D_FEATURE_LEVEL_12_1 = 0xc100 (12.1)
            // D3D_FEATURE_LEVEL_12_2 = 0xc200 (12.2)
            int major = (api_version >> 12) & 0xF;
            int minor = (api_version >> 8) & 0xF;

            if (api == reshade::api::device_api::d3d10) {
                snprintf(buffer, sizeof(buffer), "Direct3D 10.%d", minor);
            } else if (api == reshade::api::device_api::d3d11) {
                snprintf(buffer, sizeof(buffer), "Direct3D 11.%d", minor);
            } else {
                snprintf(buffer, sizeof(buffer), "Direct3D 12.%d", minor);
            }
            break;
        }
        case reshade::api::device_api::opengl: {
            // OpenGL version is encoded as major << 12 | minor << 8
            int major = (api_version >> 12) & 0xF;
            int minor = (api_version >> 8) & 0xF;
            snprintf(buffer, sizeof(buffer), "OpenGL %d.%d", major, minor);
            break;
        }
        case reshade::api::device_api::vulkan: {
            // Vulkan version is encoded as major << 12 | minor << 8
            int major = (api_version >> 12) & 0xF;
            int minor = (api_version >> 8) & 0xF;
            snprintf(buffer, sizeof(buffer), "Vulkan %d.%d", major, minor);
            break;
        }
        default: snprintf(buffer, sizeof(buffer), "Unknown"); break;
    }

    return std::string(buffer);
}

// MinHook wrapper function that combines CreateHook and EnableHook with proper error handling
bool CreateAndEnableHook(LPVOID ptarget, LPVOID pdetour, LPVOID* ppOriginal, const char* hookName) {
    if (ptarget == nullptr || pdetour == nullptr) {
        LogError("CreateAndEnableHook: Invalid parameters for hook '%s' ptarget: %p, pdetour: %p",
                 hookName != nullptr ? hookName : "Unknown", ptarget, pdetour);
        return false;
    }

    // Create the hook
    MH_STATUS create_result = MH_CreateHook(ptarget, pdetour, ppOriginal);
    if (create_result != MH_OK) {
        LogError("CreateAndEnableHook: Failed to create hook '%s' (status: %s)",
                 hookName != nullptr ? hookName : "Unknown", MH_StatusToString(create_result));
        return false;
    }

    // Enable the hook
    MH_STATUS enable_result = MH_EnableHook(ptarget);
    if (enable_result != MH_OK) {
        LogError("CreateAndEnableHook: Failed to enable hook '%s' (status: %s), removing hook",
                 hookName != nullptr ? hookName : "Unknown", MH_StatusToString(enable_result));

        // Clean up the hook if enabling failed
        MH_STATUS remove_result = MH_RemoveHook(ptarget);
        if (remove_result != MH_OK) {
            LogError("CreateAndEnableHook: Failed to remove hook '%s' after enable failure (status: %s)",
                     hookName != nullptr ? hookName : "Unknown", MH_StatusToString(remove_result));
        }
        return false;
    }

    LogInfo("CreateAndEnableHook: Successfully created and enabled hook '%s'",
            hookName != nullptr ? hookName : "Unknown");
    return true;
}

// MinHook initialization wrapper that checks suppress_minhook setting
MH_STATUS SafeInitializeMinHook(display_commanderhooks::HookType hookType) {
    // Check if MinHook initialization is suppressed
    if (settings::g_developerTabSettings.suppress_minhook.GetValue()) {
        LogInfo("MinHook initialization suppressed by suppress_minhook setting for %s hooks",
                display_commanderhooks::HookSuppressionManager::GetInstance().GetHookTypeName(hookType).c_str());
        return MH_ERROR_ALREADY_INITIALIZED;  // Return this to indicate "already initialized" to avoid errors
    }
    static bool minhook_initialized = false;
    if (minhook_initialized) {
        LogInfo("MinHook already initialized, proceeding with %s hooks",
                display_commanderhooks::HookSuppressionManager::GetInstance().GetHookTypeName(hookType).c_str());
        return MH_OK;
    }
    minhook_initialized = true;

    // Initialize MinHook (only if not already initialized)
    MH_STATUS init_status = MH_Initialize();
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for %s hooks - Status: %d",
                 display_commanderhooks::HookSuppressionManager::GetInstance().GetHookTypeName(hookType).c_str(),
                 init_status);
        return init_status;
    }

    LogInfo("MinHook initialized successfully for %s hooks",
            display_commanderhooks::HookSuppressionManager::GetInstance().GetHookTypeName(hookType).c_str());
    return init_status;
}

// Get the directory where the addon is located
std::filesystem::path GetAddonDirectory() {
    char module_path[MAX_PATH];

    // Try to get the module handle using a static variable address
    static int dummy = 0;
    HMODULE hModule = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&dummy), &hModule)
        != 0) {
        GetModuleFileNameA(hModule, module_path, MAX_PATH);
    } else {
        // Fallback to current directory
        GetCurrentDirectoryA(MAX_PATH, module_path);
    }

    return std::filesystem::path(module_path).parent_path();
}

// Helper function to check if a version is between two version ranges (inclusive)
bool isBetween(int major, int minor, int patch, int minMajor, int minMinor, int minPatch, int maxMajor, int maxMinor,
               int maxPatch) {
    // Convert version to comparable integer (major * 10000 + minor * 100 + patch)
    int version = (major * 10000) + (minor * 100) + patch;
    int minVersion = (minMajor * 10000) + (minMinor * 100) + minPatch;
    int maxVersion = (maxMajor * 10000) + (maxMinor * 100) + maxPatch;

    return version >= minVersion && version <= maxVersion;
}

// Get supported DLSS Super Resolution presets based on DLL version
std::string GetSupportedDLSSSRPresets(int major, int minor, int patch) {
    std::string supported_presets;

    // 3.8.10-3.8.10
    if (isBetween(major, minor, patch, 3, 8, 10, 3, 8, 10)) {
        return "E,F";
    }

    // 3.1.30-310.4.0
    if (isBetween(major, minor, patch, 3, 1, 30, 310, 3, 999)) {
        supported_presets += "A,B,C,D";
    }

    // 3.7.0-310.3.999
    if (isBetween(major, minor, patch, 3, 7, 0, 310, 3, 999)) {
        if (!supported_presets.empty()) supported_presets += ",";
        supported_presets += "E";
    }
    // 3.7.0-999.999.999
    if (isBetween(major, minor, patch, 3, 7, 0, 999, 999, 999)) {
        if (!supported_presets.empty()) supported_presets += ",";
        supported_presets += "F";
    }

    // 310.2.0-999.999.999
    if (isBetween(major, minor, patch, 310, 2, 0, 999, 999, 999)) {
        if (!supported_presets.empty()) supported_presets += ",";
        supported_presets += "J,K";
    }
    return supported_presets;
}

// Get supported DLSS Ray Reconstruction presets based on DLL version
// RR supports A, B, C, D, E presets depending on version (A, B, C added in newer versions)
std::string GetSupportedDLSSRRPresets(int major, int minor, int patch) {
    // Ray Reconstruction was introduced in DLSS 3.5.0+
    if (!isBetween(major, minor, patch, 3, 5, 0, 999, 999, 999)) {
        return "";  // For older versions, RR is not supported
    }

    // 3.5.0-310.3.999: RR supports A, B, C, D, E presets
    if (isBetween(major, minor, patch, 3, 5, 0, 310, 3, 999)) {
        return "A,B,C,D,E";
    }

    // 310.4.0-999.999.999: RR supports A, B, C, D, E presets (and potentially more)
    if (isBetween(major, minor, patch, 310, 4, 0, 999, 999, 999)) {
        return "A,B,C,D,E";
    }

    // Fallback for other versions that support RR but with limited presets
    return "D,E";
}

// Parse version string and return supported SR presets
std::string GetSupportedDLSSSRPresetsFromVersionString(const std::string& versionString) {
    // Handle "Not loaded" or "Unknown" cases
    if (versionString == "Not loaded" || versionString == "Unknown" || versionString == "N/A") {
        return "N/A";
    }

    // Parse version string (format: "major.minor.build.revision" or "major.minor.patch")
    int major = 0, minor = 0, patch = 0;

    // Try to parse the version string
    size_t first_dot = versionString.find('.');
    if (first_dot != std::string::npos) {
        major = std::stoi(versionString.substr(0, first_dot));

        size_t second_dot = versionString.find('.', first_dot + 1);
        if (second_dot != std::string::npos) {
            minor = std::stoi(versionString.substr(first_dot + 1, second_dot - first_dot - 1));

            // Look for third dot (build.revision format) or use as patch
            size_t third_dot = versionString.find('.', second_dot + 1);
            if (third_dot != std::string::npos) {
                // Format: major.minor.build.revision - use build as patch
                patch = std::stoi(versionString.substr(second_dot + 1, third_dot - second_dot - 1));
            } else {
                // Format: major.minor.patch
                patch = std::stoi(versionString.substr(second_dot + 1));
            }
        }
    }

    return GetSupportedDLSSSRPresets(major, minor, patch);
}

// Parse version string and return supported RR presets
std::string GetSupportedDLSSRRPresetsFromVersionString(const std::string& versionString) {
    // Handle "Not loaded" or "Unknown" cases
    if (versionString == "Not loaded" || versionString == "Unknown" || versionString == "N/A") {
        return "N/A";
    }

    // Parse version string (format: "major.minor.build.revision" or "major.minor.patch")
    int major = 0, minor = 0, patch = 0;

    // Try to parse the version string
    size_t first_dot = versionString.find('.');
    if (first_dot != std::string::npos) {
        major = std::stoi(versionString.substr(0, first_dot));

        size_t second_dot = versionString.find('.', first_dot + 1);
        if (second_dot != std::string::npos) {
            minor = std::stoi(versionString.substr(first_dot + 1, second_dot - first_dot - 1));

            // Look for third dot (build.revision format) or use as patch
            size_t third_dot = versionString.find('.', second_dot + 1);
            if (third_dot != std::string::npos) {
                // Format: major.minor.build.revision - use build as patch
                patch = std::stoi(versionString.substr(second_dot + 1, third_dot - second_dot - 1));
            } else {
                // Format: major.minor.patch
                patch = std::stoi(versionString.substr(second_dot + 1));
            }
        }
    }

    return GetSupportedDLSSRRPresets(major, minor, patch);
}

// Generate DLSS preset options based on supported presets
std::vector<std::string> GetDLSSPresetOptions(const std::string& supportedPresets) {
    std::vector<std::string> options;

    // Always include Game Default and DLSS Default
    options.push_back("Game Default");
    options.push_back("DLSS Default");

    // Parse supported presets string (e.g., "A,B,C,D" or "E,F")
    if (supportedPresets != "N/A" && !supportedPresets.empty()) {
        std::stringstream ss(supportedPresets);
        std::string preset;

        while (std::getline(ss, preset, ',')) {
            // Trim whitespace
            preset.erase(0, preset.find_first_not_of(" \t"));
            preset.erase(preset.find_last_not_of(" \t") + 1);

            if (!preset.empty()) {
                options.push_back("Preset " + preset);
            }
        }
    }

    return options;
}

// Convert DLSS preset string to integer value
int GetDLSSPresetValue(const std::string& presetString) {
    if (presetString == "Game Default") {
        return -1;  // No override - don't change anything
    } else if (presetString == "DLSS Default") {
        return 0;  // Use DLSS default (value 0)
    } else if (presetString.substr(0, 7) == "Preset ") {
        // Extract the preset letter (e.g., "Preset A" -> "A")
        std::string presetLetter = presetString.substr(7);
        if (presetLetter.length() == 1) {
            char letter = presetLetter[0];
            if (letter >= 'A' && letter <= 'Z') {
                return letter - 'A' + 1;  // A=1, B=2, C=3, etc.
            }
        }
    }

    // Default to no override if string doesn't match expected format
    return -1;
}

// Test function to demonstrate DLSS preset support (can be called for debugging)
void TestDLSSPresetSupport() {
    LogInfo("=== DLSS Preset Support Test ===");

    // Test various versions
    struct TestVersion {
        int major, minor, patch;
        const char* description;
    };

    TestVersion test_versions[] = {{3, 1, 29, "Before preset E introduction"},   {3, 1, 30, "Preset E introduced"},
                                   {3, 6, 99, "Before preset F introduction"},   {3, 7, 0, "Preset F introduced"},
                                   {3, 8, 10, "Special case: only E,F"},         {3, 8, 11, "After special case"},
                                   {310, 1, 99, "Before preset K introduction"}, {310, 2, 0, "Preset K introduced"},
                                   {310, 3, 0, "Latest with all presets"}};

    for (const auto& test : test_versions) {
        std::string presets = GetSupportedDLSSSRPresets(test.major, test.minor, test.patch);
        LogInfo("Version %d.%d.%d (%s): Presets [%s]", test.major, test.minor, test.patch, test.description,
                presets.c_str());
    }

    LogInfo("=== End DLSS Preset Support Test ===");
}

// D3D9 present mode and flags string conversion functions
const char* D3DSwapEffectToString(uint32_t swapEffect) {
    switch (swapEffect) {
        case 1:  return "D3DSWAPEFFECT_DISCARD";
        case 2:  return "D3DSWAPEFFECT_FLIP";
        case 3:  return "D3DSWAPEFFECT_COPY";
        case 4:  return "D3DSWAPEFFECT_OVERLAY";
        case 5:  return "D3DSWAPEFFECT_FLIPEX";
        default: return "UNKNOWN_SWAP_EFFECT";
    }
}

std::string D3DPresentFlagsToString(uint32_t presentFlags) {
    if (presentFlags == 0) {
        return "NONE";
    }

    std::string result;

    // D3DPRESENT flags (using actual D3D9 constants)
    if (presentFlags & D3DPRESENT_DONOTWAIT) result += "D3DPRESENT_DONOTWAIT | ";
    if (presentFlags & D3DPRESENT_LINEAR_CONTENT) result += "D3DPRESENT_LINEAR_CONTENT | ";
    if (presentFlags & D3DPRESENT_DONOTFLIP) result += "D3DPRESENT_DONOTFLIP | ";
    if (presentFlags & D3DPRESENT_FLIPRESTART) result += "D3DPRESENT_FLIPRESTART | ";
    if (presentFlags & D3DPRESENT_VIDEO_RESTRICT_TO_MONITOR) result += "D3DPRESENT_VIDEO_RESTRICT_TO_MONITOR | ";
    if (presentFlags & D3DPRESENT_UPDATEOVERLAYONLY) result += "D3DPRESENT_UPDATEOVERLAYONLY | ";
    if (presentFlags & D3DPRESENT_HIDEOVERLAY) result += "D3DPRESENT_HIDEOVERLAY | ";
    if (presentFlags & D3DPRESENT_UPDATECOLORKEY) result += "D3DPRESENT_UPDATECOLORKEY | ";
    if (presentFlags & D3DPRESENT_FORCEIMMEDIATE) result += "D3DPRESENT_FORCEIMMEDIATE | ";
    // Remove trailing " | " if present
    if (!result.empty() && result.length() >= 3) {
        result.erase(result.length() - 3);
    }

    // If no known flags were found, show the raw value
    if (result.empty()) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "0x%08X", presentFlags);
        result = buffer;
    }

    return result;
}

// Game detection utilities
std::string GetCurrentProcessName() {
    char processPath[MAX_PATH];
    DWORD pathLength = GetModuleFileNameA(nullptr, processPath, MAX_PATH);

    if (pathLength == 0) {
        return "Unknown";
    }

    std::string fullPath(processPath);
    size_t lastSlash = fullPath.find_last_of("\\/");

    if (lastSlash != std::string::npos) {
        std::string fileName = fullPath.substr(lastSlash + 1);
        // Remove .exe extension if present
        size_t dotPos = fileName.find_last_of('.');
        if (dotPos != std::string::npos && fileName.substr(dotPos) == ".exe") {
            fileName = fileName.substr(0, dotPos);
        }
        return fileName;
    }

    return fullPath;
}

bool IsGameInNvapiAutoEnableList(const std::string& processName) {
    // List of supported games for NVAPI auto-enable
    static const std::vector<std::string> supportedGames = {"armoredcore6",
                                                            "ac6",
                                                            "armoredcorevi",
                                                            "acvi",
                                                            "dmc5",
                                                            "devilmaycry5",
                                                            "devilmaycryv",
                                                            "eldenring",
                                                            "elden_ring",
                                                            "hitman",
                                                            "hitman3",
                                                            "hitmaniii",
                                                            "re2",
                                                            "residentevil2",
                                                            "resident_evil_2",
                                                            "re2remake",
                                                            "re3",
                                                            "residentevil3",
                                                            "resident_evil_3",
                                                            "re3remake",
                                                            "re7",
                                                            "residentevil7",
                                                            "resident_evil_7",
                                                            "re7biohazard",
                                                            "re8",
                                                            "residentevil8",
                                                            "resident_evil_8",
                                                            "re8village",
                                                            "sekiro",
                                                            "sekiroshadowsdietwice",
                                                            "sekiro_shadows_die_twice"};

    // Convert process name to lowercase for case-insensitive comparison
    std::string lowerProcessName = processName;
    std::transform(lowerProcessName.begin(), lowerProcessName.end(), lowerProcessName.begin(), ::tolower);

    // Check if the process name matches any supported game
    for (const auto& game : supportedGames) {
        if (lowerProcessName.find(game) != std::string::npos) {
            return true;
        }
    }

    return false;
}

std::string GetNvapiAutoEnableGameStatus() {
    std::string processName = GetCurrentProcessName();
    bool isSupported = IsGameInNvapiAutoEnableList(processName);

    if (isSupported) {
        return "✓ " + processName + " (Supported)";
    } else {
        return "✗ " + processName + " (Not Supported)";
    }
}

// GetCallingDLL implementation (similar to Special-K's SK_GetCallingDLL)
HMODULE GetCallingDLL(LPCVOID pReturn) {
    HMODULE hCallingMod = nullptr;

    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                       static_cast<const wchar_t*>(pReturn), &hCallingMod);

    return hCallingMod;
}
