#pragma once

#define ImTextureID ImU64
#define DEBUG_LEVEL_0
#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include <windef.h>

#include <atomic>
#include <memory>
#include <string>
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
void ProcessStickInputRadial(float &x, float &y, float deadzone, float max_input, float min_output);
float ApplyDeadzone(float value, float deadzone, float max_input);
float ProcessStickInput(float value, float deadzone, float max_input, float min_output);

// XInput thumbstick scaling helpers (handles asymmetric SHORT range: -32768 to 32767)
float ShortToFloat(SHORT value);
SHORT FloatToShort(float value);

// DLL version information
std::string GetDLLVersionString(const std::wstring& dllPath);

// Forward declaration for ReShade API types
namespace reshade { namespace api { enum class device_api; } }

// Graphics API version string conversion
const char* GetDeviceApiString(reshade::api::device_api api);
std::string GetDeviceApiVersionString(reshade::api::device_api api, uint32_t api_version);

// Rolling average (exponential moving average) calculation
// Formula: (new_value + (alpha - 1) * old_value) / alpha
// Default alpha=64 provides good smoothing for frame timing metrics
template<typename T>
inline T UpdateRollingAverage(T new_value, T old_value, int alpha = 64) {
    return (new_value + (alpha - 1) * old_value) / alpha;
}

// MinHook wrapper functions
bool CreateAndEnableHook(LPVOID pTarget, LPVOID pDetour, LPVOID* ppOriginal, const char* hookName);

// External declarations needed by utility functions
extern std::atomic<std::shared_ptr<const std::vector<MonitorInfo>>> g_monitors;
