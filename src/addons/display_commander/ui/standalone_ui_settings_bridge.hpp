#pragma once

#include <cstdint>
#include <string>

#include <windows.h>

// Bridge for standalone settings UI (No ReShade): get/set main tab settings without
// including globals.hpp/reshade in the standalone UI TU. Implemented in standalone_ui_settings_bridge.cpp
// which uses settings::g_mainTabSettings so atomics and config stay in sync.

namespace standalone_ui_settings {

int GetFpsLimiterMode();
void SetFpsLimiterMode(int value);

float GetFpsLimit();
void SetFpsLimit(float value);

bool GetAudioMute();
void SetAudioMute(bool value);

/** Game audio volume (0–100%). */
float GetAudioVolumePercent();
void SetAudioVolumePercent(float value);

bool GetMuteInBackground();
void SetMuteInBackground(bool value);

int GetWindowMode();
void SetWindowMode(int value);

std::string GetTargetDisplayDeviceId();
void SetTargetDisplayDeviceId(const std::string& value);

/** Current FPS from present/performance ring (last 1 s). Returns 0.0 when no data (e.g. no game running). */
double GetCurrentFps();

/** Game window (last swapchain HWND). Returns nullptr when none. For display only; use uintptr_t to avoid HWND in header. */
uintptr_t GetLastSwapchainHwnd();

/** Register/unregister the standalone UI window so it is not treated as the game window. Call with hwnd after CreateWindow, nullptr before DestroyWindow. */
void SetStandaloneUiHwnd(uintptr_t hwnd);

/** CreateWindowW bypassing the hook (same signature as CreateWindowW). Use from standalone UI so window creation is not intercepted. */
HWND CreateWindowW_Direct(LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth,
                          int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam);

}  // namespace standalone_ui_settings
