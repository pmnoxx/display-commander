#pragma once

#include <windows.h>

// Forward declarations
void ComputeDesiredSize(int& out_w, int& out_h);

// Window management functions
void CalculateWindowState(HWND hwnd, const char* reason);
bool ShouldApplyWindowedForBackbuffer(int desired_w, int desired_h);
