/*
 * Copyright (C) 2024 Display Commander
 * Window procedure hooks using MinHook for managing window properties
 */

#pragma once

#include <windows.h>

namespace display_commanderhooks {

// True if window has caption or thick frame (standard bordered window). Borderless windows return false.
bool WindowHasBorder(HWND hwnd);

// Window procedure hook functions
bool InstallWindowProcHooks(HWND hwnd);
void UninstallWindowProcHooks();

// Get continue rendering status for debugging
bool IsContinueRenderingEnabled();

// Fake activation functions
void SendFakeActivationMessages(HWND hwnd);

// Get the currently hooked window (backward compatibility - uses game window)
HWND GetHookedWindow();

// Message detouring function (similar to Special-K's SK_DetourWindowProc)
LRESULT DetourWindowMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

// Process window message - returns true if message should be suppressed
// Called from message retrieval hooks (GetMessage/PeekMessage) when hwnd belongs to current process
bool ProcessWindowMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

}  // namespace display_commanderhooks
