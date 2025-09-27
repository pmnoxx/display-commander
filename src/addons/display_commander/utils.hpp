#pragma once

#define ImTextureID ImU64
#define DEBUG_LEVEL_0
#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include <windef.h>

#include <atomic>
#include <memory>
#include <vector>

// Structs needed for utility functions
struct AspectRatio {
    int w;
    int h;
};

struct MonitorInfo {
    HMONITOR handle;
    MONITORINFOEXW info;
};

// Constants
extern const int WIDTH_OPTIONS[];
extern const int HEIGHT_OPTIONS[];
extern const AspectRatio ASPECT_OPTIONS[];

// Forward declarations for utility functions
RECT RectFromWH(int width, int height);
void LogInfo(const char *msg, ...);
void LogWarn(const char *msg, ...);
void LogError(const char *msg, ...);
void LogDebug(const char *msg, ...);
// Window state detection
AspectRatio GetAspectByIndex(int index);
int GetAspectWidthValue(int display_width);

// Monitor enumeration callback
BOOL CALLBACK MonitorEnumProc(HMONITOR hmon, HDC hdc, LPRECT rect, LPARAM lparam);

// XInput processing functions
float ApplyDeadzone(float value, float deadzone);
float ProcessStickInput(float value, float deadzone, float max_input, float min_output);

// XInput thumbstick scaling helpers (handles asymmetric SHORT range: -32768 to 32767)
float ShortToFloat(SHORT value);
SHORT FloatToShort(float value);

// External declarations needed by utility functions
extern std::atomic<std::shared_ptr<const std::vector<MonitorInfo>>> g_monitors;
