#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windef.h>
#include <dxgi1_3.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <cstdint>
#include <vector>

#include "utils.hpp"
#include "globals.hpp"

// WASAPI per-app volume control
#include <mmdeviceapi.h>
#include <audiopolicy.h>

// Audio management functions
bool SetMuteForCurrentProcess(bool mute);
bool SetVolumeForCurrentProcess(float volume_0_100);
void RunBackgroundAudioMonitor();

// Forward declarations
void ComputeDesiredSize(int& out_w, int& out_h);

std::vector<std::string> MakeMonitorLabels();

// Forward declarations that depend on enums
DxgiBypassMode GetIndependentFlipState(reshade::api::swapchain* swapchain);



// Include consolidated swapchain events header
#include "swapchain_events.hpp"

// Command list and queue lifecycle hooks (declared in swapchain_events.hpp)

// Function declarations
const char* DxgiBypassModeToString(DxgiBypassMode mode);
bool SetIndependentFlipState(reshade::api::swapchain* swapchain);
void ApplyWindowChange(HWND hwnd, const char* reason = "unknown", bool force_apply = false);
bool ShouldApplyWindowedForBackbuffer(int desired_w, int desired_h);

// Continuous monitoring functions
void StartContinuousMonitoring();
void StopContinuousMonitoring();
void ContinuousMonitoringThread();
bool NeedsWindowAdjustment(HWND hwnd, int& out_width, int& out_height, int& out_pos_x, int& out_pos_y, WindowStyleMode& out_style_mode);

// CONTINUOUS RENDERING FUNCTIONS REMOVED - Focus spoofing is now handled by Win32 hooks

// Swapchain event handlers (declared in swapchain_events.hpp)

// Note: GetIndependentFlipState is implemented in the .cpp file as it's complex

// Power saving settings and swapchain utilities (declared in swapchain_events.hpp)