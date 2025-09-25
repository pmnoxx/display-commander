/*
 * Copyright (C) 2024 Display Commander
 * Window procedure hooks using MinHook for managing window properties
 */

#pragma once

#include <windows.h>

namespace display_commanderhooks {

// Window procedure hook functions
bool InstallWindowProcHooks(HWND hwnd);
void UninstallWindowProcHooks();

// Check if hooks are installed
bool AreWindowProcHooksInstalled();

// Get continue rendering status for debugging
bool IsContinueRenderingEnabled();

// Fake activation functions
void FakeActivateWindow(HWND hwnd);
void SendFakeActivationMessages(HWND hwnd);

// Set the target window to hook
void SetTargetWindow(HWND hwnd);

// Get the currently hooked window
HWND GetHookedWindow();

// Message detouring function (similar to Special-K's SK_DetourWindowProc)
LRESULT DetourWindowMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

} // namespace display_commanderhooks
