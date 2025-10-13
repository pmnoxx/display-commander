#pragma once

#include <atomic>
#include <thread>
#include <windows.h>

// Forward declaration for LONGLONG
typedef long long LONGLONG;

namespace autoclick {

// Global variables for auto-click functionality
extern std::atomic<bool> g_auto_click_thread_running;
extern std::thread g_auto_click_thread;
extern const bool g_move_mouse;
extern const bool g_mouse_spoofing_enabled;

// UI state tracking for optimization
extern std::atomic<bool> g_ui_overlay_open;
extern std::atomic<LONGLONG> g_last_ui_draw_time_ns;

// Function declarations
void PerformClick(int x, int y, int sequence_num, bool is_test = false);
void AutoClickThread();
void ToggleAutoClickEnabled();
void StartAutoClickThread();
void StopAutoClickThread();
void DrawAutoClickFeature();
void DrawSequence(int sequence_num);
void DrawMouseCoordinatesDisplay();

// UI state management functions
void UpdateUIOverlayState(bool is_open);
void UpdateLastUIDrawTime();

} // namespace autoclick
