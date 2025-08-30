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

// Reflex management functions
bool InstallReflexHooks();
void UninstallReflexHooks();
void SetReflexLatencyMarkers(reshade::api::swapchain* swapchain);
void SetReflexSleepMode(reshade::api::swapchain* swapchain);

// Frame lifecycle hooks for custom FPS limiter
void OnBeginRenderPass(reshade::api::command_list* cmd_list, uint32_t count, const reshade::api::render_pass_render_target_desc* rts, const reshade::api::render_pass_depth_stencil_desc* ds);
void OnEndRenderPass(reshade::api::command_list* cmd_list);

// Command list and queue lifecycle hooks
void OnInitCommandList(reshade::api::command_list* cmd_list);
void OnInitCommandQueue(reshade::api::command_queue* queue);
void OnResetCommandList(reshade::api::command_list* cmd_list);

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

// Swapchain event handlers
void OnInitSwapchain(reshade::api::swapchain* swapchain, bool resize);
void OnPresentUpdateBefore(reshade::api::command_queue *, reshade::api::swapchain* swapchain, const reshade::api::rect* source_rect, const reshade::api::rect* dest_rect, uint32_t dirty_rect_count, const reshade::api::rect* dirty_rects);
void OnPresentUpdateBefore2(reshade::api::effect_runtime* runtime);
void OnPresentUpdateAfter(reshade::api::command_queue* /*queue*/, reshade::api::swapchain* swapchain);
void OnPresentFlags(uint32_t* present_flags);

// Additional event handlers for frame timing and composition
void OnInitCommandList(reshade::api::command_list* cmd_list);
void OnExecuteCommandList(reshade::api::command_queue* queue, reshade::api::command_list* cmd_list);
void OnBindPipeline(reshade::api::command_list* cmd_list, reshade::api::pipeline_stage stages, reshade::api::pipeline pipeline);

// Note: GetIndependentFlipState is implemented in the .cpp file as it's complex

// Swapchain sync interval accessors
// Returns UINT32_MAX when using application's default, or <0-4> for explicit intervals, or UINT_MAX if unknown
uint32_t GetSwapchainSyncInterval(reshade::api::swapchain* swapchain);

// Event to capture sync interval from swapchain creation
bool OnCreateSwapchainCapture(reshade::api::device_api api, reshade::api::swapchain_desc& desc, void* hwnd);