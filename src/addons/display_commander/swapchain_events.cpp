#include "addon.hpp"
#include "reflex/reflex_management.hpp"
#include "dxgi/dxgi_device_info.hpp"
#include "dxgi/custom_fps_limiter_manager.hpp"
#include "resolution_helpers.hpp"
#include <dxgi1_4.h>
#include "utils.hpp"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <mutex>

// Use renodx2 utilities for swapchain color space changes
#include <utils/swapchain.hpp>

// Frame lifecycle hooks for custom FPS limiter
void OnBeginRenderPass(reshade::api::command_list* cmd_list, uint32_t count, const reshade::api::render_pass_render_target_desc* rts, const reshade::api::render_pass_depth_stencil_desc* ds) {
    // Call custom FPS limiter frame begin if enabled
    extern const float s_custom_fps_limiter_enabled;
    if (s_custom_fps_limiter_enabled > 0.5f) {
        if (renodx::dxgi::fps_limiter::g_customFpsLimiterManager) {
            auto& limiter = renodx::dxgi::fps_limiter::g_customFpsLimiterManager->GetFpsLimiter();
            if (limiter.IsEnabled()) {
                limiter.OnFrameBegin();
            }
        }
    }
}

void OnEndRenderPass(reshade::api::command_list* cmd_list) {
    // Call custom FPS limiter frame end if enabled
    extern const float s_custom_fps_limiter_enabled;
    if (s_custom_fps_limiter_enabled > 0.5f) {
        if (renodx::dxgi::fps_limiter::g_customFpsLimiterManager) {
            auto& limiter = renodx::dxgi::fps_limiter::g_customFpsLimiterManager->GetFpsLimiter();
            if (limiter.IsEnabled()) {
                limiter.OnFrameEnd();
            }
        }
    }
}

// Track last requested sync interval per HWND detected at create_swapchain
namespace {
static std::mutex g_sync_mutex;
static std::unordered_map<HWND, uint32_t> g_hwnd_to_syncinterval; // UINT32_MAX = default
static std::unordered_map<reshade::api::swapchain*, uint32_t> g_swapchain_to_syncinterval;
}

// Expose for UI
uint32_t GetSwapchainSyncInterval(reshade::api::swapchain* swapchain) {
  if (swapchain == nullptr) return UINT32_MAX;
  std::scoped_lock lk(g_sync_mutex);
  auto it = g_swapchain_to_syncinterval.find(swapchain);
  if (it != g_swapchain_to_syncinterval.end()) return it->second;
  return UINT32_MAX;
}

// Capture sync interval during create_swapchain
#if RESHADE_API_VERSION >= 17
bool OnCreateSwapchainCapture(reshade::api::device_api /*api*/, reshade::api::swapchain_desc& desc, void* hwnd) {
#else
bool OnCreateSwapchainCapture(reshade::api::swapchain_desc& desc, void* hwnd) {
#endif
  if (hwnd == nullptr) return false;
  std::scoped_lock lk(g_sync_mutex);
  g_hwnd_to_syncinterval[static_cast<HWND>(hwnd)] = desc.sync_interval; // UINT32_MAX or 0..4
  return false; // do not modify
}

void OnInitSwapchain(reshade::api::swapchain* swapchain, bool resize) {
  // Update last known backbuffer size and colorspace
  auto* device = swapchain->get_device();
  if (device != nullptr && swapchain->get_back_buffer_count() > 0) {
    auto bb = swapchain->get_back_buffer(0);
    auto desc = device->get_resource_desc(bb);
    g_last_backbuffer_width.store(static_cast<int>(desc.texture.width));
    g_last_backbuffer_height.store(static_cast<int>(desc.texture.height));
    
    // Store current colorspace for UI display
    g_current_colorspace = swapchain->get_color_space();
    
    std::ostringstream oss; oss << "OnInitSwapchain(backbuffer=" << desc.texture.width << "x" << desc.texture.height
                                << ", resize=" << (resize ? "true" : "false") << ", colorspace=" << static_cast<int>(g_current_colorspace) << ")";
    LogDebug(oss.str());
  }

  // Schedule auto-apply even on resizes (generation counter ensures only latest runs)
  HWND hwnd = static_cast<HWND>(swapchain->get_hwnd());
  g_last_swapchain_hwnd.store(hwnd);
  g_last_swapchain_ptr.store(swapchain);
  if (hwnd == nullptr) return;
  // Bind captured sync interval to swapchain instance
  {
    std::scoped_lock lk(g_sync_mutex);
    auto it = g_hwnd_to_syncinterval.find(hwnd);
    if (it != g_hwnd_to_syncinterval.end()) {
      g_swapchain_to_syncinterval[swapchain] = it->second;
    }
  }
  // Update DXGI composition state if possible
  {
    DxgiBypassMode mode = GetIndependentFlipState(swapchain);
    switch (mode) {
      case DxgiBypassMode::kUnknown: s_dxgi_composition_state = 0.f; break;
      case DxgiBypassMode::kComposed: s_dxgi_composition_state = 1.f; break;
      case DxgiBypassMode::kOverlay: s_dxgi_composition_state = 2.f; break;
      case DxgiBypassMode::kIndependentFlip: s_dxgi_composition_state = 3.f; break;
    }
    std::ostringstream oss2;
    oss2 << "DXGI Composition State (onSwapChainInit): " << DxgiBypassModeToString(mode)
         << " (" << static_cast<int>(s_dxgi_composition_state) << ")";
    LogInfo(oss2.str().c_str());
  }
  
  // Log Independent Flip conditions to update failure tracking
  LogDebug(resize ? "Schedule auto-apply on swapchain init (resize)"
                  : "Schedule auto-apply on swapchain init");
  
  // Fix HDR10 colorspace if enabled and needed
  if (s_fix_hdr10_colorspace >= 0.5f) {
    FixHDR10Colorspace(swapchain);
  }
  
  // Note: Minimize hook removed - use continuous monitoring instead

  // Set Reflex sleep mode and latency markers if enabled
  if (s_reflex_enabled >= 0.5f) {
    SetReflexSleepMode(swapchain);
    SetReflexLatencyMarkers(swapchain);
    // Also call sleep to start the frame properly
    extern std::unique_ptr<ReflexManager> g_reflexManager;
    if (g_reflexManager && g_reflexManager->IsAvailable()) {
      g_reflexManager->CallSleep(swapchain);
    }
  }
}

// Update composition state after presents (required for valid stats)
void OnPresentUpdate(
    reshade::api::command_queue* /*queue*/, reshade::api::swapchain* swapchain,
    const reshade::api::rect* /*source_rect*/, const reshade::api::rect* /*dest_rect*/,
    uint32_t /*dirty_rect_count*/, const reshade::api::rect* /*dirty_rects*/) {
  
  // Track swap chain buffer size changes
  static uint32_t last_width = 0;
  static uint32_t last_height = 0;
  
  if (swapchain != nullptr) {
    uint32_t current_width = 0;
    uint32_t current_height = 0;
    
    // Get the current back buffer dimensions
    if (swapchain->get_back_buffer_count() > 0) {
      auto back_buffer = swapchain->get_back_buffer(0);
      if (back_buffer != 0) {
        auto desc = swapchain->get_device()->get_resource_desc(back_buffer);
        current_width = desc.texture.width;
        current_height = desc.texture.height;
      }
    }
    
    // Log if dimensions changed
    if (current_width != last_width || current_height != last_height) {
      if (last_width != 0 || last_height != 0) { // Don't log on first call
        std::ostringstream oss;
        oss << "Swap chain buffer size changed: " << last_width << "x" << last_height << " -> " << current_width << "x" << current_height;
        LogInfo(oss.str().c_str());
      }
      last_width = current_width;
      last_height = current_height;
    }
  }
  
  // Throttle queries to ~every 30 presents
  int c = ++g_comp_query_counter;
  
  // Call Reflex functions on EVERY frame (not throttled)
  if (s_reflex_enabled >= 0.5f) {
    extern std::unique_ptr<ReflexManager> g_reflexManager;
    extern std::atomic<bool> g_reflex_settings_changed;
    
    if (g_reflexManager && g_reflexManager->IsAvailable()) {
      // Force sleep mode update if settings have changed (required for NVIDIA overlay detection)
      if (g_reflex_settings_changed.load()) {
        SetReflexSleepMode(swapchain);
        g_reflex_settings_changed.store(false);
        LogDebug("Sleep mode updated due to settings change");
      } else if ((c % 60) == 0) { // Added periodic refresh
        // Refresh sleep mode every 60 frames to maintain Reflex state
        SetReflexSleepMode(swapchain);
      }
      // Call sleep at frame start (this is crucial for Reflex to work)
      g_reflexManager->CallSleep(swapchain);
      // Set full latency markers (SIMULATION, RENDERSUBMIT, INPUT) as PCLStats ETW events
      // PRESENT markers are sent separately to NVAPI only
      SetReflexLatencyMarkers(swapchain);
      // Set PRESENT markers to wrap the Present call (NVAPI only, no ETW)
      SetReflexPresentMarkers(swapchain);
    }
  }

  // Call Custom FPS Limiter on EVERY frame (not throttled)
  extern const float s_custom_fps_limiter_enabled;
  if (s_custom_fps_limiter_enabled > 0.5f) {
    // Check window focus and apply appropriate FPS limit
    extern std::atomic<HWND> g_last_swapchain_hwnd;
    extern float s_fps_limit_background;
    
    HWND hwnd = g_last_swapchain_hwnd.load();
    if (hwnd == nullptr) hwnd = GetForegroundWindow();
    
    const bool is_background = (hwnd != nullptr && GetForegroundWindow() != hwnd);
    
    // Get the appropriate FPS limit based on focus state
    float target_fps = 0.0f;
    if (is_background) {
      target_fps = s_fps_limit_background;  // Use background FPS limit
    } else {
      extern float s_fps_limit;  // Use foreground FPS limit from UI settings
      target_fps = s_fps_limit;
    }
    
    // Apply the FPS limit to the Custom FPS Limiter
    if (renodx::dxgi::fps_limiter::g_customFpsLimiterManager) {
      auto& limiter = renodx::dxgi::fps_limiter::g_customFpsLimiterManager->GetFpsLimiter();
      if (target_fps > 0.0f) {
        limiter.SetTargetFps(target_fps);
        limiter.SetEnabled(true);
        limiter.LimitFrameRate();
      } else {
        limiter.SetEnabled(false);
      }
    }
  }

  // Apply input blocking based on background/foreground state every frame
  {
    reshade::api::effect_runtime* runtime = g_reshade_runtime.load();
    if (runtime != nullptr) {
      HWND hwnd = g_last_swapchain_hwnd.load();
      if (hwnd == nullptr) hwnd = GetForegroundWindow();
      const bool is_background = (hwnd != nullptr && GetForegroundWindow() != hwnd);
      const bool block_mouse = (s_block_mouse_in_background >= 0.5f) && is_background;
      const bool block_keyboard = (s_block_keyboard_in_background >= 0.5f) && is_background;
      const bool block_warp = (s_block_mouse_cursor_warping_in_background >= 0.5f) && is_background;
      if (block_mouse) {  
        runtime->block_mouse_input(block_mouse);
      }
      if (block_keyboard) {
        runtime->block_keyboard_input(block_keyboard);
      }
      if (block_warp) {
        runtime->block_mouse_cursor_warping(block_warp);
      }
    }
  }

  // CONTINUOUS RENDERING FEATURE COMPLETELY REMOVED - Focus spoofing is now handled by Win32 hooks
  // This provides a much cleaner and more effective solution
  
  if ((c % 30) != 0) return;
  
  
  DxgiBypassMode mode = GetIndependentFlipState(swapchain);
  int state = 0;
  switch (mode) {
    case DxgiBypassMode::kComposed: state = 1; break;
    case DxgiBypassMode::kOverlay: state = 2; break;
    case DxgiBypassMode::kIndependentFlip: state = 3; break;
    case DxgiBypassMode::kUnknown:
    default: state = 0; break;
  }
  s_dxgi_composition_state = static_cast<float>(state);
  
  // Update colorspace info (throttled to avoid excessive calls)
  if ((c % 30) == 0) {
    g_current_colorspace = swapchain->get_color_space();
    
    // Enumerate DXGI devices during present (throttled to avoid excessive calls)
    extern std::unique_ptr<DXGIDeviceInfoManager> g_dxgiDeviceInfoManager;
    if (g_dxgiDeviceInfoManager && g_dxgiDeviceInfoManager->IsInitialized()) {
      g_dxgiDeviceInfoManager->EnumerateDevicesOnPresent();
    }
  }
  
  int last = g_comp_last_logged.load();
  if (state != last) {
    g_comp_last_logged.store(state);
    std::ostringstream oss;
    oss << "DXGI Composition State (present): " << DxgiBypassModeToString(mode) << " (" << state << ")";
    LogInfo(oss.str().c_str());
  }


}

// Fix HDR10 colorspace when swapchain format is RGB10A2 and colorspace is not already HDR10
void FixHDR10Colorspace(reshade::api::swapchain* swapchain) {
  if (swapchain == nullptr) return;
  
  // Update timestamp
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  std::stringstream ss;
  struct tm timeinfo;
  localtime_s(&timeinfo, &time_t);
  ss << std::put_time(&timeinfo, "%H:%M:%S");
  g_hdr10_override_timestamp = ss.str();
  
  // Get the device to check backbuffer format
  auto* device = swapchain->get_device();
  if (device == nullptr || swapchain->get_back_buffer_count() == 0) return;
  
  // Get backbuffer description to check format
  auto bb = swapchain->get_back_buffer(0);
  auto desc = device->get_resource_desc(bb);
  
  // Only proceed if format is RGB10A2 (R10G10B10A2_UNORM)
  if (desc.texture.format != reshade::api::format::r10g10b10a2_unorm) {
    return;
  }
  
  // Only proceed if current colorspace is sRGB (srgb_nonlinear)
  auto current_colorspace = swapchain->get_color_space();
  if (current_colorspace != reshade::api::color_space::srgb_nonlinear) {
    g_hdr10_override_status = "Skipped: colorspace not sRGB";
    LogDebug("Skipping HDR10 colorspace fix: current colorspace is not sRGB");
    return;
  }
  
  // Use the proper ChangeColorSpace function from utils
  // This handles all the DXGI interface management and runtime updates properly
  if (renodx::utils::swapchain::ChangeColorSpace(swapchain, reshade::api::color_space::hdr10_st2084)) {
    LogInfo("Successfully changed colorspace from sRGB to HDR10 ST2084");
    
    // Verify the change took effect
    auto new_colorspace = swapchain->get_color_space();
    if (new_colorspace == reshade::api::color_space::hdr10_st2084) {
      g_hdr10_override_status = "Success: changed to HDR10 ST2084";
      LogInfo("Colorspace change verified: now using HDR10 ST2084");
    } else {
      g_hdr10_override_status = "Warning: change may not have taken effect";
      std::ostringstream oss;
      oss << "Colorspace change may not have taken effect. Current: " << static_cast<int>(new_colorspace);
      LogWarn(oss.str().c_str());
    }
  } else {
    g_hdr10_override_status = "Failed: ChangeColorSpace returned false";
    LogWarn("Failed to change colorspace to HDR10 ST2084");
  }
}
