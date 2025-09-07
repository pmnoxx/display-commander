/*
 * Copyright (C) 2024 Display Commander
 * Window style hooks for managing window properties
 */

#pragma once

#include <windows.h>

namespace renodx::hooks {

// Window style hook functions
void InstallWindowStyleHooks();
void UninstallWindowStyleHooks();

// Hooked window procedure
LRESULT CALLBACK WindowStyleHookProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

// Check if hooks are installed
bool AreWindowStyleHooksInstalled();

// Get focus spoofing status for debugging
bool IsFocusSpoofingEnabled();

// External variables
extern HWND g_hooked_window;
extern WNDPROC g_original_window_proc;

} // namespace renodx::hooks
