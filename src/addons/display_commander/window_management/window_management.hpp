#pragma once

#include <windows.h>
#include "../display_cache.hpp"

// Forward declarations
void ComputeDesiredSize(int& out_w, int& out_h);

// Window management functions
void CalculateWindowState(HWND hwnd, const char* reason);
int FindTargetMonitor(HWND hwnd, const RECT& wr_current, float target_monitor_index, MONITORINFO& mi, display_cache::RationalRefreshRate& out_refresh);
bool ShouldApplyWindowedForBackbuffer(int desired_w, int desired_h);
