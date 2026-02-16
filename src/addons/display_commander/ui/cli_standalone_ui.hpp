#pragma once

#include <windows.h>

// Runs the installer UI inside the addon DLL (CommandLine UITest). No separate .exe.
// Creates a Win32 window, D3D11 device, and a second ImGui (ImGuiStandalone); runs until window is closed.
void RunStandaloneUI(HINSTANCE hInst);
