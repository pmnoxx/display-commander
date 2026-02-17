#pragma once

#include <windows.h>

// Runs the installer UI inside the addon DLL (CommandLine SetupDC [script_dir]). No separate .exe.
// Creates a Win32 window, D3D11 device, and a second ImGui (ImGuiStandalone); runs until window is closed.
// script_dir_utf8: UTF-8 path to the folder where the installer script runs (target dir for ReShade/addon). Caller passes "." when not specified.
void RunStandaloneUI(HINSTANCE hInst, const char* script_dir_utf8 = nullptr);
