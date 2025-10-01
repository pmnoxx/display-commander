#pragma once

#include <atomic>
#include <thread>

namespace autoclick {

// Global variables for auto-click functionality
extern std::atomic<bool> g_auto_click_thread_running;
extern std::thread g_auto_click_thread;
extern bool g_move_mouse;

// Function declarations
void PerformClick(int x, int y, int sequence_num, bool is_test = false);
void AutoClickThread();
void ToggleAutoClickEnabled();
void DrawAutoClickFeature();
void DrawSequence(int sequence_num);
void DrawMouseCoordinatesDisplay();

} // namespace autoclick
