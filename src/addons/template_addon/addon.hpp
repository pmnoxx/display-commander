#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windef.h>
#include <imgui.h>
#include <reshade.hpp>

#include <atomic>

// Template addon namespace
namespace template_addon {

// Global state variables
extern std::atomic<bool> g_enabled;
extern std::atomic<bool> g_show_ui;
extern std::atomic<float> g_slider_value;
extern std::atomic<int> g_selected_option;

// UI functions
void DrawUI();
void DrawMainTab();
void DrawSettingsTab();
void DrawAboutTab();

// Utility functions
void LogInfo(const char* format, ...);
void LogWarn(const char* format, ...);
void LogError(const char* format, ...);

// Settings management
void LoadSettings();
void SaveSettings();

} // namespace template_addon
