/*
 * Copyright (C) 2024 Display Commander
 * Window style hooks for managing window properties
 */

#pragma once

#include <windows.h>

namespace renodx::hooks {

// Window style hook functions
void InstallWindowStyleHooks(HMODULE hModule);
void UninstallWindowStyleHooks();

// Hooked window procedure
LRESULT CALLBACK WindowStyleHookProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

// Check if hooks are installed
bool AreWindowStyleHooksInstalled();

// Get continue rendering status for debugging
bool IsContinueRenderingEnabled();

// Fake activation functions
void FakeActivateWindow(HWND hwnd);
void SendFakeActivationMessages(HWND hwnd);

// External variables
extern HWND g_hooked_window;
extern WNDPROC g_original_window_proc;

} // namespace renodx::hooks
