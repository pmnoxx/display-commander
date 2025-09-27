#include "utils.hpp"

#include "globals.hpp"
#include <algorithm>
#include <utility>

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