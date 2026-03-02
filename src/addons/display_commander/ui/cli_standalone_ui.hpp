#pragma once

#include <windows.h>

// No-ReShade mode: simplified settings window (FPS limiter, mute, target display). Called when .NO_RESHADE/.NORESHADE is present.
void RunStandaloneSettingsUI(HINSTANCE hInst);

// Standalone .exe entry point: initializes Display Commander (no-ReShade path) and runs RunStandaloneSettingsUI on the
// current thread. Only used when building the .exe (DISPLAY_COMMANDER_BUILD_EXE); declare here for main_exe.cpp.
void RunDisplayCommanderStandalone(HINSTANCE hInst);

// Standalone .exe UI: Games tab only (used by DisplayCommander_x64.exe so the window shows only the Games tab).
void RunStandaloneGamesOnlyUI(HINSTANCE hInst);
