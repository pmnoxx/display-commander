#include "utils.hpp"

#include "globals.hpp"
#include <algorithm>


// Constant definitions
const int WIDTH_OPTIONS[] = {0, 1280, 1366, 1600, 1920, 2560, 3440, 3840}; // 0 = current monitor width
const int HEIGHT_OPTIONS[] = {0, 720, 900, 1080, 1200, 1440, 1600, 2160}; // 0 = current monitor height
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

std::vector<std::string> MakeAspectLabels() {
    std::vector<std::string> labels;
    labels.reserve(8);
    labels.push_back("3:2");
    labels.push_back("4:3");
    labels.push_back("16:10");
    labels.push_back("16:9");
    labels.push_back("19:9");
    labels.push_back("19.5:9");
    labels.push_back("21:9");
    labels.push_back("32:9");
    return labels;
}

// Helper functions to get current monitor dimensions
int GetCurrentMonitorWidth() {
    // Get the current monitor where the game window is located
    HWND hwnd = g_last_swapchain_hwnd.load();
    if (hwnd == nullptr) {
        // Fallback to primary monitor
        return GetSystemMetrics(SM_CXSCREEN);
    }

    HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hmon, &mi)) {
        return mi.rcMonitor.right - mi.rcMonitor.left;
    }

    // Fallback to primary monitor
    return GetSystemMetrics(SM_CXSCREEN);
}

int GetCurrentMonitorHeight() {
    // Get the current monitor where the game window is located
    HWND hwnd = g_last_swapchain_hwnd.load();
    if (hwnd == nullptr) {
        // Fallback to primary monitor
        return GetSystemMetrics(SM_CYSCREEN);
    }

    HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hmon, &mi)) {
        return mi.rcMonitor.bottom - mi.rcMonitor.top;
    }

    // Fallback to primary monitor
        return GetSystemMetrics(SM_CYSCREEN);
}

// Helper function implementations
RECT RectFromWH(int width, int height) {
    RECT rect = {0, 0, width, height};
    return rect;
}

// Utility function implementations
void LogInfo(const char* msg, ...) {
    va_list args;
    va_start(args, msg);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), msg, args);
    va_end(args);
    reshade::log::message(reshade::log::level::info, buffer);
}

void LogWarn(const char* msg, ...) {
    va_list args;
    va_start(args, msg);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), msg, args);
    va_end(args);
    reshade::log::message(reshade::log::level::warning, buffer);
}

void LogError(const char* msg, ...) {
    va_list args;
    va_start(args, msg);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), msg, args);
    va_end(args);
    reshade::log::message(reshade::log::level::error, buffer);
}

void LogDebug(const std::string& s) {
    reshade::log::message(reshade::log::level::debug, s.c_str());
}


std::string FormatLastError() {
    DWORD error = GetLastError();
    if (error == 0) return "No error";

    LPSTR messageBuffer = nullptr;
    size_t size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

    if (size == 0) return "Unknown error";

    std::string result(messageBuffer, size);
    LocalFree(messageBuffer);
    return result;
}

bool IsExclusiveFullscreen(HWND hwnd) {
    if (!hwnd) return false;

    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    if (style & WS_POPUP) return true;

    RECT window_rect, client_rect;
    GetWindowRect(hwnd, &window_rect);
    GetClientRect(hwnd, &client_rect);

    // Convert client rect to screen coordinates
    POINT client_tl = {client_rect.left, client_rect.top};
    POINT client_br = {client_rect.right, client_rect.bottom};
    ClientToScreen(hwnd, &client_tl);
    ClientToScreen(hwnd, &client_br);

    // Check if window rect matches client rect (fullscreen indicator)
    return (window_rect.left == client_tl.x &&
            window_rect.top == client_tl.y &&
            window_rect.right == client_br.x &&
            window_rect.bottom == client_br.y);
}

// Spoof fullscreen state detection based on user settings
bool GetSpoofedFullscreenState(HWND hwnd) {

    // If spoofing is disabled, return actual state
    if (s_spoof_fullscreen_state.load() == 0) {
        return IsExclusiveFullscreen(hwnd);
    }

    // Spoof as fullscreen (value 1)
    if (s_spoof_fullscreen_state.load() == 1) {
        return true;
    }

    // Spoof as windowed (value 2)
    return false;
}

// Get the current spoofing setting value (0=disabled, 1=spoof as fullscreen, 2=spoof as windowed)
int GetFullscreenSpoofingMode() {
    return s_spoof_fullscreen_state.load();
}


bool IsBorderlessStyleBits(LONG_PTR style) {
    // A window is borderless if it lacks the main window decoration styles
    return !(style & (WS_CAPTION | WS_THICKFRAME | WS_DLGFRAME));
}

UINT ComputeSWPFlags(HWND hwnd, bool style_changed) {
    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
    if (style_changed) flags |= SWP_FRAMECHANGED;
    return flags;
}

std::vector<std::string> MakeLabels(const int* values, size_t count) {
    std::vector<std::string> labels;
    labels.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        labels.push_back(std::to_string(values[i]));
    }
    return labels;
}

int FindClosestIndex(int value, const int* values, size_t count) {
    if (count == 0) return -1;

    int closest_index = 0;
    int min_diff = abs(value - values[0]);

    for (size_t i = 1; i < count; ++i) {
        int diff = abs(value - values[i]);
        if (diff < min_diff) {
            min_diff = diff;
            closest_index = static_cast<int>(i);
        }
    }

    return closest_index;
}


AspectRatio GetAspectByIndex(AspectRatioType aspect_type) {
    int index = static_cast<int>(aspect_type);
    if (index >= 0 && index < 8) {
        return ASPECT_OPTIONS[index];
    }
    return {16, 9}; // Default to 16:9
}

void ComputeDesiredSize(int& out_w, int& out_h) {
    if (s_window_mode.load() == WindowMode::kFullscreen) {
        // kFullscreen: Borderless Fullscreen - use current monitor dimensions
        out_w = GetCurrentMonitorWidth();
        out_h = GetCurrentMonitorHeight();
        return;
    }

    // kAspectRatio: Borderless Windowed (Aspect Ratio) - aspect mode
    // For aspect ratio mode, we need to get the width from the resolution widget
    // For now, use current monitor width as default
    const int want_w = GetCurrentMonitorWidth();
    AspectRatio ar = GetAspectByIndex(s_aspect_index.load());
    // height = round(width * h / w)
    // prevent division by zero
    if (ar.w <= 0 || ar.h <= 0) {
        ar.h = 16;
        ar.w = 9;
    }
    out_w = want_w;
    out_h = want_w * ar.h / ar.w;

    LogInfo("ComputeDesiredSize: out_w=%d, out_h=%d", out_w, out_h);
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


