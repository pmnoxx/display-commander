/*
 * Copyright (C) 2024 Display Commander
 * Window procedure hooks using MinHook for managing window properties
 */

#pragma once

#include <windows.h>
#include <vector>

namespace display_commanderhooks {

struct WindowMessageRecord {
    UINT message_id = 0;
};

// True if window has caption or thick frame (standard bordered window). Borderless windows return false.
bool WindowHasBorder(HWND hwnd);

// Window procedure hook functions
bool InstallWindowProcHooks(HWND hwnd);
void UninstallWindowProcHooks();

// Get continue rendering status for debugging
bool IsContinueRenderingEnabled();

// Fake activation functions
void SendFakeActivationMessages(HWND hwnd);

// Non-blocking message detour: posts SendMessage on another thread to avoid deadlock. Skips if called within 100ms.
void DetourWindowMessageNonBlocking(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

// Process window message - returns true if message should be suppressed
// Called from message retrieval hooks (GetMessage/PeekMessage) when hwnd belongs to current process
bool ProcessWindowMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

// Returns latest-to-oldest snapshot of recent window messages.
std::vector<WindowMessageRecord> GetRecentWindowMessagesSnapshot();

// Clears the captured recent window message history.
void ClearRecentWindowMessages();

// Per-message debug history filters (when enabled, message is ignored in history capture).
bool GetDebugHistoryFilterEnabledForMessage(UINT message_id);
void SetDebugHistoryFilterEnabledForMessage(UINT message_id, bool enabled);

// Returns symbolic message name when known, fallback "WM_0xXXXX".
const char* GetWindowMessageName(UINT message_id);

}  // namespace display_commanderhooks
