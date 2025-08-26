#include "addon.hpp"
#include "reflex/reflex_management.hpp"
#include "dxgi/dxgi_device_info.hpp"
#include "resolution_helpers.hpp"
#include "display_cache.hpp"
#include "audio/audio_management.hpp"
#include <dxgi1_4.h>
#include "utils.hpp"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <mutex>
#include <minwindef.h>

// Use renodx2 utilities for swapchain color space changes
#include <utils/swapchain.hpp>
#include "globals.hpp"

// Frame lifecycle hooks for custom FPS limiter
void OnBeginRenderPass(reshade::api::command_list* cmd_list, uint32_t count, const reshade::api::render_pass_render_target_desc* rts, const reshade::api::render_pass_depth_stencil_desc* ds) {
    // Call custom FPS limiter frame begin if enabled
    if (s_custom_fps_limiter_enabled.load()) {
        if (dxgi::fps_limiter::g_customFpsLimiterManager) {
            auto& limiter = dxgi::fps_limiter::g_customFpsLimiterManager->GetFpsLimiter();
            if (limiter.IsEnabled()) {
                limiter.OnFrameBegin();
            }
        }
    }
}

void OnEndRenderPass(reshade::api::command_list* cmd_list) {
    // Call custom FPS limiter frame end if enabled
    if (s_custom_fps_limiter_enabled.load()) {
        if (dxgi::fps_limiter::g_customFpsLimiterManager) {
            auto& limiter = dxgi::fps_limiter::g_customFpsLimiterManager->GetFpsLimiter();
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

// Get the sync interval coefficient for FPS calculation
float GetSyncIntervalCoefficient(float sync_interval_value) {
  // Map sync interval values to their corresponding coefficients
  // 3 = V-Sync (1x), 4 = V-Sync 2x, 5 = V-Sync 3x, 6 = V-Sync 4x
  switch (static_cast<int>(sync_interval_value)) {
    case 0: return 0.0f;   // App Controlled
    case 1: return 0.0f;   // No-VSync
    case 2: return 1.0f;   // V-Sync
    case 3: return 2.0f;   // V-Sync 2x
    case 4: return 3.0f;   // V-Sync 3x
    case 5: return 4.0f;   // V-Sync 4x
    default: return 1.0f;  // Fallback
  }
}

// Capture sync interval during create_swapchain
bool OnCreateSwapchainCapture(reshade::api::device_api /*api*/, reshade::api::swapchain_desc& desc, void* hwnd) {
  if (hwnd == nullptr) return false;
  
  // Apply sync interval setting if enabled
  bool modified = false;
  
  // Explicit VSYNC overrides take precedence over generic sync-interval dropdown
  if (s_force_vsync_on.load()) {
    desc.sync_interval = 1; // VSYNC on
    modified = true;
  } else if (s_force_vsync_off.load()) {
    desc.sync_interval = 0; // VSYNC off
    desc.present_flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    modified = true;
  }
  // Apply tearing preference if requested and applicable
  {
    const bool is_flip = (desc.present_mode == DXGI_SWAP_EFFECT_FLIP_DISCARD || desc.present_mode == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL);
    if (is_flip) {
      if (s_prevent_tearing.load() && desc.sync_interval > 0) { //  && desc.sync_interval < INT_MAX
        // Clear allow tearing flag when preventing tearing
        desc.present_flags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        if (desc.sync_interval == INT_MAX) {
          desc.sync_interval = 0;
        }
        modified = true;
      } else if (s_allow_tearing.load() && desc.sync_interval < INT_MAX) {
        // Enable tearing when requested
        desc.present_flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        modified = true;
      }
    }
  }
  
  if (s_enable_resolution_override.load()) {
    const int width = s_override_resolution_width.load();
    const int height = s_override_resolution_height.load();
    
    // Only apply if both width and height are > 0 (same logic as ReShade ForceResolution)
    if (width > 0 && height > 0) {
      desc.back_buffer.texture.width = static_cast<uint32_t>(width);
      desc.back_buffer.texture.height = static_cast<uint32_t>(height);
      modified = true;
      
      // Log the resolution override for debugging
      std::ostringstream oss;
      oss << "Resolution Override applied: " << width << "x" << height;
      LogInfo(oss.str().c_str());
    }
  }
  // Log sync interval and present flags with detailed explanation
  {
    std::ostringstream oss;
    oss << "Swapchain Creation - Sync Interval: " << desc.sync_interval;
    
    // Map sync interval to human-readable description
    switch (desc.sync_interval) {
      case 0: oss << " (App Controlled)"; break;
      case 1: oss << " (No V-Sync)"; break;
      case 2: oss << " (V-Sync)"; break;
      case 3: oss << " (V-Sync 2x)"; break;
      case 4: oss << " (V-Sync 3x)"; break;
      case 5: oss << " (V-Sync 4x)"; break;
      default: oss << " (Unknown)"; break;
    }
    
    oss << ", Present Flags: 0x" << std::hex << desc.present_flags << std::dec;
    
    // Show which features are enabled in present_flags
    if (desc.present_flags == 0) {
      oss << " (No special flags)";
    } else {
      oss << " - Enabled features:";
      /*
      
        DXGI_SWAP_CHAIN_FLAG_NONPREROTATED	= 1,
        DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH	= 2,
        DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE	= 4,
        DXGI_SWAP_CHAIN_FLAG_RESTRICTED_CONTENT	= 8,
        DXGI_SWAP_CHAIN_FLAG_RESTRICT_SHARED_RESOURCE_DRIVER	= 16,
        DXGI_SWAP_CHAIN_FLAG_DISPLAY_ONLY	= 32,
        DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT	= 64,
        DXGI_SWAP_CHAIN_FLAG_FOREGROUND_LAYER	= 128,
        DXGI_SWAP_CHAIN_FLAG_FULLSCREEN_VIDEO	= 256,
        DXGI_SWAP_CHAIN_FLAG_YUV_VIDEO	= 512,
        DXGI_SWAP_CHAIN_FLAG_HW_PROTECTED	= 1024,
        DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING	= 2048,
        DXGI_SWAP_CHAIN_FLAG_RESTRICTED_TO_ALL_HOLOGRAPHIC_DISPLAYS	= 4096
        */
      if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_NONPREROTATED) {
        oss << " NONPREROTATED";
      }
      if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH) {
        oss << " ALLOW_MODE_SWITCH";
      }
      if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE) {
        oss << " GDI_COMPATIBLE";
      }
      if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_RESTRICTED_CONTENT) {
        oss << " RESTRICTED_CONTENT";
      }
      if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_RESTRICT_SHARED_RESOURCE_DRIVER) {
        oss << " RESTRICT_SHARED_RESOURCE_DRIVER";
      }
      if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_DISPLAY_ONLY) {
        oss << " DISPLAY_ONLY";
      }
      if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) {
        oss << " FRAME_LATENCY_WAITABLE_OBJECT";
      }
      if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_FOREGROUND_LAYER) {
        oss << " FOREGROUND_LAYER";
      }
      if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_FULLSCREEN_VIDEO) {
        oss << " FULLSCREEN_VIDEO";
      }
      if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_YUV_VIDEO) {
        oss << " YUV_VIDEO";
      }
      if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_HW_PROTECTED) {
        oss << " HW_PROTECTED";
      }
      if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) {
        oss << " ALLOW_TEARING";
      }
      if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_RESTRICTED_TO_ALL_HOLOGRAPHIC_DISPLAYS) {
        oss << " RESTRICTED_TO_ALL_HOLOGRAPHIC_DISPLAYS";
      } 
    }
    
    LogInfo(oss.str().c_str());
  }
  
  return modified; // return true if we modified the desc
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
        case DxgiBypassMode::kUnknown: s_dxgi_composition_state.store(0); break;
        case DxgiBypassMode::kComposed: s_dxgi_composition_state.store(1); break;
        case DxgiBypassMode::kOverlay: s_dxgi_composition_state.store(2); break;
        case DxgiBypassMode::kIndependentFlip: s_dxgi_composition_state.store(3); break;
    }
    std::ostringstream oss2;
    oss2 << "DXGI Composition State (onSwapChainInit): " << DxgiBypassModeToString(mode)
         << " (" << static_cast<int>(s_dxgi_composition_state) << ")";
    LogInfo(oss2.str().c_str());
  }
  
  // Log Independent Flip conditions to update failure tracking
  LogDebug(resize ? "Schedule auto-apply on swapchain init (resize)"
                  : "Schedule auto-apply on swapchain init");
  
  // Note: Minimize hook removed - use continuous monitoring instead

  // Set Reflex sleep mode and latency markers if enabled
  if (s_reflex_enabled.load()) {
    SetReflexSleepMode(swapchain);
    SetReflexLatencyMarkers(swapchain);
    // Also call sleep to start the frame properly
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
  
  // Avoid querying swapchain/device descriptors every frame. These are updated elsewhere.
  
  // Throttle queries to ~every 30 presents
  int c = ++g_comp_query_counter;

  // Record per-frame FPS sample for background aggregation (lock-free ring)
  {
    static auto start_tp = std::chrono::steady_clock::now();
    auto now_tp = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now_tp - start_tp).count();
    g_perf_time_seconds.store(elapsed, std::memory_order_release);
    static double last_tp = 0.0;
    const double dt = elapsed - last_tp;
    if (dt > 0.0) {
      const float fps = static_cast<float>(1.0 / dt);
      uint32_t idx = g_perf_ring_head.fetch_add(1, std::memory_order_acq_rel);
      g_perf_ring[idx & (kPerfRingCapacity - 1)] = PerfSample{elapsed, fps};
      last_tp = elapsed;
    }
  }
  
  // Call Reflex functions on EVERY frame (not throttled)
  if (s_reflex_enabled.load() && s_reflex_use_markers.load()) {
    
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

  // Call FPS Limiter on EVERY frame (not throttled)
  if (s_custom_fps_limiter_enabled.load()) {
    // Use background flag computed by monitoring thread; avoid GetForegroundWindow here
    const bool is_background = g_app_in_background.load(std::memory_order_acquire);
    
    // Get the appropriate FPS limit based on focus state
    float target_fps = 0.0f;
    if (is_background) {
      target_fps = s_fps_limit_background.load();  // Use background FPS limit
    } else {
      target_fps = s_fps_limit.load();
    }
    // Apply FPS limit via selected limiter mode
    if (dxgi::fps_limiter::g_customFpsLimiterManager) {
      if (s_fps_limiter_mode.load() == 1) {
        auto& latent = dxgi::fps_limiter::g_customFpsLimiterManager->GetLatentLimiter();
        if (target_fps > 0.0f) {
          latent.SetTargetFps(target_fps);
          latent.SetEnabled(true);
          latent.LimitFrameRate();
        } else {
          latent.SetEnabled(false);
        }
      } else {
        auto& limiter = dxgi::fps_limiter::g_customFpsLimiterManager->GetFpsLimiter();
        if (target_fps > 0.0f) {
          limiter.SetTargetFps(target_fps);
          limiter.SetEnabled(true);
          limiter.LimitFrameRate();
        } else {
          limiter.SetEnabled(false);
        }
      }
    }
  }

  // Apply input blocking based on background/foreground; avoid OS calls and redundant writes
  {
    reshade::api::effect_runtime* runtime = g_reshade_runtime.load();
    if (runtime != nullptr) {
      const bool is_background = g_app_in_background.load(std::memory_order_acquire);
      const bool wants_block_input = s_block_input_in_background.load() && is_background;
      // Call every frame as long as any blocking is desired
      if (is_background && wants_block_input) {
        runtime->block_input_next_frame();
      }
    }
  }
  
  // Handle keyboard shortcuts (Experimental)
  if (s_enable_mute_unmute_shortcut.load()) {
    // Get the runtime from the atomic variable
    reshade::api::effect_runtime* runtime = g_reshade_runtime.load();
    if (runtime != nullptr) {
      // Check for Ctrl+M shortcut
      if (runtime->is_key_pressed('M') && runtime->is_key_down(VK_CONTROL)) {
        // Toggle mute state
        bool new_mute_state = !s_audio_mute.load();
        if (SetMuteForCurrentProcess(new_mute_state)) {
          s_audio_mute.store(new_mute_state);
          g_muted_applied.store(new_mute_state);
          
          // Log the action
          std::ostringstream oss;
          oss << "Audio " << (new_mute_state ? "muted" : "unmuted") << " via Ctrl+M shortcut";
          LogInfo(oss.str().c_str());
        }
      }
    }
  }
}
