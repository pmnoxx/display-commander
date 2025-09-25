#pragma once

#include <windows.h>

// Window management functions
void CalculateWindowState(HWND hwnd, const char *reason);
bool ShouldApplyWindowedForBackbuffer(int desired_w, int desired_h);
