#pragma once

#include <windows.h>
#include "../display_cache.hpp"


// Window management functions
void CalculateWindowState(HWND hwnd, const char* reason);
bool ShouldApplyWindowedForBackbuffer(int desired_w, int desired_h);
