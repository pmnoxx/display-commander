#pragma once

#include <string>
#include <windows.h>

// Helpers used by RunDLL injection entry points (StartAndInject, WaitAndInject).
// Implemented in main_entry.cpp; a future refactor may move injection code to a dedicated module.

std::wstring GetReShadeDllPath(bool is_wow64);
bool InjectIntoProcess(DWORD pid, const std::wstring& dll_path);
void WaitForProcessAndInject(const std::wstring& exe_name);
