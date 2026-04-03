// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#pragma once

#include <string>

#include <Windows.h>

void ChooseAndSetDcConfigPath(HMODULE h_module);
void CaptureDllLoadCallerPath(HMODULE h_our_module);
void EnsureDisplayCommanderLogWithModulePath(HMODULE h_module);
void LogBootDllMainStage(const char* stage_message);
void LogBootRegisterAndPostInitStage(const char* stage_message);
void LogBootInitWithoutHwndStage(const char* stage_message);
void LogBootDcConfigPath();
