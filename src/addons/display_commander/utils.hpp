#pragma once

#define ImTextureID ImU64
#define DEBUG_LEVEL_0
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <windef.h>
#include <include/reshade.hpp>

#include <sstream>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cmath>
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
void LogInfo(const char* msg);
void LogWarn(const char* msg);
void LogError(const char* msg);
void LogDebug(const std::string& s);
std::string FormatLastError();
// Window state detection
bool IsExclusiveFullscreen(HWND hwnd);
bool GetSpoofedFullscreenState(HWND hwnd);
int GetFullscreenSpoofingMode();
bool GetSpoofedWindowFocus(HWND hwnd);
int GetWindowFocusSpoofingMode();
UINT ComputeSWPFlags(HWND hwnd, bool style_changed);
bool IsBorderlessStyleBits(LONG_PTR style);
bool IsBorderless(HWND hwnd);
void RemoveWindowBorderLocal(HWND window);
void ForceWindowToMonitorOrigin(HWND hwnd);
void ForceWindowToMonitorOriginThreaded(HWND hwnd);
std::vector<std::string> MakeLabels(const int* values, size_t count);
int FindClosestIndex(int value, const int* values, size_t count);
std::vector<std::string> MakeAspectLabels();
AspectRatio GetAspectByIndex(int index);
int GetCurrentMonitorWidth();
int GetCurrentMonitorHeight();

// Monitor enumeration callback
BOOL CALLBACK MonitorEnumProc(HMONITOR hmon, HDC hdc, LPRECT rect, LPARAM lparam);



// External declarations needed by utility functions
extern std::vector<MonitorInfo> g_monitors;
