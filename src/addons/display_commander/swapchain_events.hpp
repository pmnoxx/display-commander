#pragma once

#include "globals.hpp"
#include "performance_types.hpp"

#include <reshade.hpp>

#include <dxgi1_6.h>

#include <atomic>

// ============================================================================
// API TYPE ENUM
// ============================================================================

/** True when user enabled "Inject Reflex" and addon should run Reflex (sleep + markers) in place of native. */
bool IsInjectedReflexEnabled();

// ============================================================================
// SWAPCHAIN EVENT HANDLERS
// ============================================================================

// Pipeline and resource binding hooks
bool OnBindPipeline(reshade::api::command_list* cmd_list, reshade::api::pipeline_stage stages,
                    reshade::api::pipeline pipeline);

// Device lifecycle hooks
bool OnCreateDevice(reshade::api::device_api api, uint32_t& api_version);
void OnDestroyDevice(reshade::api::device* device);
void OnInitDevice(reshade::api::device* device);

// Effect runtime lifecycle hooks
void OnDestroyEffectRuntime(reshade::api::effect_runtime* runtime);

// Swapchain lifecycle hooks
void OnInitSwapchain(reshade::api::swapchain* swapchain, bool resize);
bool OnCreateSwapchainCapture(reshade::api::device_api api, reshade::api::swapchain_desc& desc, void* hwnd);
void OnDestroySwapchain(reshade::api::swapchain* swapchain, bool resize);

// Centralized initialization method
void DoInitializationWithHwnd(HWND hwnd);

// Present event handlers
void OnPresentUpdateBefore(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain,
                           const reshade::api::rect* source_rect, const reshade::api::rect* dest_rect,
                           uint32_t dirty_rect_count, const reshade::api::rect* dirty_rects);
void OnPresentUpdateAfter(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain);
void OnPresentUpdateAfter2(bool frame_generation_aware = false);
void OnPresentFlags2(bool from_present_detour = true, bool frame_generation_aware = false);

// ============================================================================
// POWER SAVING HELPER FUNCTIONS
// ============================================================================

// Helper function to determine if an operation should be suppressed for power saving
bool ShouldBackgroundSuppressOperation();

// ============================================================================
// POWER SAVING SETTINGS
// ============================================================================

// Power saving configuration flags
extern std::atomic<bool> s_suppress_compute_in_background;
extern std::atomic<bool> s_suppress_copy_in_background;
extern std::atomic<bool> s_suppress_binding_in_background;
extern std::atomic<bool> s_suppress_memory_ops_in_background;

// ============================================================================
// SWAPCHAIN UTILITY FUNCTIONS
// ============================================================================

// Get target FPS based on background state
float GetTargetFps();

// Reflex enable/params derived from FPS limiter mode (Main tab: onpresent_reflex_mode, reflex_limiter_reflex_mode,
// reflex_disabled_limiter_mode). Use these instead of deprecated reflex_enable / reflex_low_latency / reflex_boost.
bool ShouldReflexBeEnabled();
bool ShouldReflexLowLatencyBeEnabled();
bool ShouldReflexBoostBeEnabled();
/** True only when FPS limiter mode is Reflex: use Reflex minimumIntervalUs for FPS limiting. */
bool ShouldUseReflexAsFpsLimiter();

// ============================================================================
// PERFORMANCE MONITORING FUNCTIONS
// ============================================================================

// Record per-frame FPS sample for background aggregation
void RecordFrameTime(FrameTimeMode reason = FrameTimeMode::kPresent);

// Record native frame time (for frames shown to display via native swapchain Present)
// Similar to RecordFrameTime but specifically for native frames when limit_real_frames is enabled
void RecordNativeFrameTime();

// ============================================================================
// TIMING VARIABLES
// ============================================================================

// Present after end time tracking (simulation start time)
extern std::atomic<LONGLONG> g_frame_time_ns;
extern std::atomic<LONGLONG> g_sim_start_ns;

// ============================================================================
// INITIALIZATION STATE
// ============================================================================

// Global initialization state
extern std::atomic<bool> g_initialized_with_hwnd;
