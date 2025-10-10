#include "utils.hpp"

#include "globals.hpp"
#include <algorithm>
#include <utility>
#include <vector>
#include <cstdio>
#include <string>
#include <reshade.hpp>

// Constant definitions
const int WIDTH_OPTIONS[] = {0, 1280, 1366, 1600, 1920, 2560, 3440, 3840}; // 0 = current monitor width
const int HEIGHT_OPTIONS[] = {0, 720, 900, 1080, 1200, 1440, 1600, 2160};  // 0 = current monitor height
const AspectRatio ASPECT_OPTIONS[] = {
    {3, 2},    // 1.5:1
    {4, 3},    // 1.333:1
    {16, 10},  // 1.6:1
    {16, 9},   // 1.778:1
    {19, 9},   // 2.111:1
    {195, 90}, // 2.167:1 (19.5:9)
    {21, 9},   // 2.333:1
    {32, 9},   // 3.556:1
};

// Helper function implementations
RECT RectFromWH(int width, int height) {
    RECT rect = {0, 0, width, height};
    return rect;
}

// Utility function implementations
void LogInfo(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), msg, args);
    va_end(args);
    reshade::log::message(reshade::log::level::info, buffer);
}

void LogWarn(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), msg, args);
    va_end(args);
    reshade::log::message(reshade::log::level::warning, buffer);
}

void LogError(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), msg, args);
    va_end(args);
    reshade::log::message(reshade::log::level::error, buffer);
}
void LogDebug(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), msg, args);
    va_end(args);
    reshade::log::message(reshade::log::level::debug, buffer);
}

AspectRatio GetAspectByIndex(AspectRatioType aspect_type) {
    int index = static_cast<int>(aspect_type);
    if (index >= 0 && index < 8) {
        return ASPECT_OPTIONS[index];
    }
    return {16, 9}; // Default to 16:9
}

// Helper function to get the actual width value based on the dropdown selection
int GetAspectWidthValue(int display_width) {
    const int width_index = s_aspect_width.load();

    // Width options: 0=Display Width, 1=3840, 2=2560, 3=1920, 4=1600, 5=1280, 6=1080, 7=900, 8=720
    int selected_width;
    switch (width_index) {
        case 0: selected_width = display_width; break;  // Display Width
        case 1: selected_width = 3840; break;
        case 2: selected_width = 2560; break;
        case 3: selected_width = 1920; break;
        case 4: selected_width = 1600; break;
        case 5: selected_width = 1280; break;
        case 6: selected_width = 1080; break;
        case 7: selected_width = 900; break;
        case 8: selected_width = 720; break;
        default: selected_width = display_width; break; // Fallback to display width
    }

    // Ensure the selected width doesn't exceed the display width
    return min(selected_width, display_width);
}

void ComputeDesiredSize(int display_width, int display_height, int &out_w, int &out_h) {
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

    LogInfo("ComputeDesiredSize: out_w=%d, out_h=%d (width_index=%d)", out_w, out_h, s_aspect_width.load());
}

// Monitor enumeration callback
BOOL CALLBACK MonitorEnumProc(HMONITOR hmon, HDC hdc, LPRECT rect, LPARAM lparam) {
    MONITORINFOEXW info;
    info.cbSize = sizeof(MONITORINFOEXW);

    if (GetMonitorInfoW(hmon, &info)) {
        MonitorInfo monitor_info;
        monitor_info.handle = hmon;
        monitor_info.info = info;
        auto *monitors = reinterpret_cast<std::vector<MonitorInfo> *>(lparam);
        if (monitors) {
            monitors->push_back(monitor_info);
        }
    }

    return TRUE;
}

// XInput processing functions
// Process stick input with radial deadzone (preserves direction)
void ProcessStickInputRadial(float &x, float &y, float deadzone, float max_input, float min_output) {
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

// Legacy per-axis function (deprecated - kept for compatibility)
float ApplyDeadzone(float value, float deadzone, float max_input) {
    if (deadzone <= 0.0f) {
        return value; // No deadzone applied
    }

    float abs_value = std::abs(value);
    float sign = (value >= 0.0f) ? 1.0f : -1.0f;

    // Traditional deadzone behavior
    if (abs_value < deadzone) {
        return 0.0f;
    }
    // Scale the value to remove the deadzone
    float scaled = min(1.0f, (abs_value - deadzone) / (max_input - deadzone));
    return sign * scaled;
}

float ProcessStickInput(float value, float deadzone, float max_input, float min_output) {
    // Step 1: Apply deadzone processing
    float processed_value = ApplyDeadzone(value, deadzone, max_input);

    // If within deadzone, return 0 (deadzone should always be 0)
    if (processed_value == 0.0f) {
        return 0.0f;
    }

    // Step 2: Apply max_input mapping (e.g., 0.7 max input maps to 1.0 max output)
    float mapped_value = processed_value;

    // Step 3: Apply min_output mapping (e.g., 0.3 min output maps 0.0-1.0 to 0.3-1.0)
    float sign = (mapped_value >= 0.0f) ? 1.0f : -1.0f;
    float abs_mapped = std::abs(mapped_value);
    float output_mapped = min_output + (abs_mapped * (1.0f - min_output));
    float final_value = sign * output_mapped;

    // Step 4: Clamp to valid range [-1.0, 1.0]
    float clamped_value = std::clamp(final_value, -1.0f, 1.0f);

    return clamped_value;
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
    DWORD versionInfoSize = GetFileVersionInfoSizeW(dllPath.c_str(), nullptr);
    if (versionInfoSize == 0) {
        return "Unknown";
    }

    std::vector<BYTE> versionInfo(versionInfoSize);
    if (!GetFileVersionInfoW(dllPath.c_str(), 0, versionInfoSize, versionInfo.data())) {
        return "Unknown";
    }

    VS_FIXEDFILEINFO* fileInfo = nullptr;
    UINT fileInfoSize = 0;

    if (!VerQueryValueW(versionInfo.data(), L"\\", reinterpret_cast<LPVOID*>(&fileInfo), &fileInfoSize)) {
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
        case reshade::api::device_api::d3d9:
            return "Direct3D 9";
        case reshade::api::device_api::d3d10:
            return "Direct3D 10";
        case reshade::api::device_api::d3d11:
            return "Direct3D 11";
        case reshade::api::device_api::d3d12:
            return "Direct3D 12";
        case reshade::api::device_api::opengl:
            return "OpenGL";
        case reshade::api::device_api::vulkan:
            return "Vulkan";
        default:
            return "Unknown";
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
            if (s_d3d9_upgrade_successful.load()) {
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
        default:
            snprintf(buffer, sizeof(buffer), "Unknown");
            break;
    }

    return std::string(buffer);
}