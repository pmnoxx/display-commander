#include "addon.hpp"
#include "reflex/reflex_management.hpp"
#include "audio/audio_management.hpp"
#include <dxgi.h>
#include <dxgi1_4.h>
#include "utils.hpp"
#include "utils/timing.hpp"
#include <sstream>
#include <unordered_map>
#include <mutex>
#include <minwindef.h>
#include <atomic>

// Use renodx2 utilities for swapchain color space changes
#include <utils/swapchain.hpp>
#include "globals.hpp"
#include "latent_sync/latent_sync_limiter.hpp"
#include "swapchain_events_power_saving.hpp"

static std::atomic<LONGLONG> g_present_start_time_qpc{0};
std::atomic<LONGLONG> g_present_duration_ns{0};

// Render start time tracking
std::atomic<LONGLONG> g_submit_start_time_qpc{0};

// Present after end time tracking
std::atomic<LONGLONG> g_present_after_end_time{0};

// Simulation duration tracking
std::atomic<LONGLONG> g_simulation_duration_ns{0};

// FPS limiter start duration tracking (nanoseconds)
std::atomic<LONGLONG> fps_sleep_before_on_present_ns{0};

// FPS limiter start duration tracking (nanoseconds)
std::atomic<LONGLONG> fps_sleep_after_on_present_ns{0};

// Reshade overhead tracking (nanoseconds)
std::atomic<LONGLONG> g_reshade_overhead_duration_ns{0};

// Render submit end time tracking (QPC)
std::atomic<LONGLONG> g_render_submit_end_time_qpc{0};

// Render submit duration tracking (nanoseconds)
std::atomic<LONGLONG> g_render_submit_duration_ns{0};

std::atomic<int> l_frame_count{0};

void HandleRenderStartAndEndTimes() {
  LONGLONG expected = 0;
  if (g_submit_start_time_qpc.load() == 0) {
    LONGLONG now = get_now_qpc();
    LONGLONG present_after_end_time = g_present_after_end_time.load();
    if (present_after_end_time > 0 && g_submit_start_time_qpc.compare_exchange_strong(expected, now)) {
        // Compare to g_present_after_end_time
        #define QPC_TO_NS 100
        LONGLONG g_simulation_duration_ns_new = (now - present_after_end_time) * QPC_TO_NS;
        int alpha = 64;
        g_simulation_duration_ns.store( (1 * g_simulation_duration_ns_new + (alpha - 1) * g_simulation_duration_ns.load()) / alpha);
    }
  }
}

void HandleEndRenderSubmit() {
  LONGLONG now = get_now_qpc();
  g_render_submit_end_time_qpc.store(now);
  if (g_submit_start_time_qpc.load() > 0) {
    LONGLONG g_render_submit_duration_ns_new = (now - g_submit_start_time_qpc.load()) * QPC_TO_NS;
    int alpha = 64;
    g_render_submit_duration_ns.store( (1 * g_render_submit_duration_ns_new + (alpha - 1) * g_render_submit_duration_ns.load()) / alpha);
  }
}

void HandleOnPresentEnd() {
  LONGLONG now = get_now_qpc();

  g_present_after_end_time.store(now);
  g_submit_start_time_qpc.store(0);

  if (g_render_submit_end_time_qpc.load() > 0) {
    LONGLONG g_reshade_overhead_duration_ns_new = (now - g_render_submit_end_time_qpc.load()) * QPC_TO_NS;
    int alpha = 64;
    g_reshade_overhead_duration_ns.store( (1 * g_reshade_overhead_duration_ns_new + (alpha - 1) * g_reshade_overhead_duration_ns.load()) / alpha);
  }
}



// Frame lifecycle hooks for custom FPS limiter
void OnBeginRenderPass(reshade::api::command_list* cmd_list, uint32_t count, const reshade::api::render_pass_render_target_desc* rts, const reshade::api::render_pass_depth_stencil_desc* ds) {
    // Increment event counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_BEGIN_RENDER_PASS].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    
    // Call custom FPS limiter frame begin if enabled
    if (s_custom_fps_limiter_enabled.load()) {
        if (dxgi::fps_limiter::g_customFpsLimiterManager) {
            auto& limiter = dxgi::fps_limiter::g_customFpsLimiterManager->GetFpsLimiter();
            if (limiter.IsEnabled()) {
                limiter.OnFrameBegin();
            }
        }
    }
    
    // Call Reflex manager callback if enabled
    if (s_reflex_enabled.load() && g_reflexManager && g_reflexManager->IsAvailable()) {
        g_reflexManager->OnBeginRenderPass(cmd_list, count, rts, ds);
    }
}

void OnEndRenderPass(reshade::api::command_list* cmd_list) {
    // Increment event counter
    g_swapchain_event_counters[SWAPCHAIN_EVENT_END_RENDER_PASS].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    
    // Call custom FPS limiter frame end if enabled
    if (s_custom_fps_limiter_enabled.load()) {
        if (dxgi::fps_limiter::g_customFpsLimiterManager) {
            auto& limiter = dxgi::fps_limiter::g_customFpsLimiterManager->GetFpsLimiter();
            if (limiter.IsEnabled()) {
                limiter.OnFrameEnd();
            }
        }
    }
    
    // Call Reflex manager callback if enabled
    if (s_reflex_enabled.load() && g_reflexManager && g_reflexManager->IsAvailable()) {
        g_reflexManager->OnEndRenderPass(cmd_list);
    }
}


// Draw event handlers for render timing and power saving
// These are now implemented in swapchain_events_power_saving.cpp

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
  // Reset all event counters on new swapchain creation
  for (int i = 0; i < 40; i++) {
    g_swapchain_event_counters[i].store(0);
  }
  g_swapchain_event_total_count.store(0);
  
  // Increment event counter
  g_swapchain_event_counters[SWAPCHAIN_EVENT_CREATE_SWAPCHAIN_CAPTURE].fetch_add(1);
  g_swapchain_event_total_count.fetch_add(1);
  
  if (hwnd == nullptr) return false;
  
  // Apply sync interval setting if enabled
  bool modified = false;

  uint32_t prev_sync_interval = UINT32_MAX;
  uint32_t prev_present_flags = desc.present_flags;
  uint32_t prev_back_buffer_count = desc.back_buffer_count;
  const bool is_flip = (desc.present_mode == DXGI_SWAP_EFFECT_FLIP_DISCARD || desc.present_mode == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL);
  
  
  // Explicit VSYNC overrides take precedence over generic sync-interval dropdown
  if (s_force_vsync_on.load()) {
    desc.sync_interval = 1; // VSYNC on
    modified = true;
  } else if (s_force_vsync_off.load()) {
    desc.sync_interval = 0; // VSYNC off
    modified = true;
  }
  if (s_prevent_tearing.load() && (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0) {
    desc.present_flags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    modified = true;
  }
  if (desc.back_buffer_count < 2) {
    desc.back_buffer_count = 2;
    modified = true;
  }
  // Log sync interval and present flags with detailed explanation
  {
    std::ostringstream oss;
    oss << " Swapchain Creation - Sync Interval: " << desc.sync_interval;
    // desc.present_mode
    oss << " Present Mode: " << desc.present_mode;
    // desc.present_flags
    oss << " Fullscreen State: " << desc.fullscreen_state;
    // desc.back_buffer.texture.width
    oss << " Sync Interval: " << prev_sync_interval << " -> " << desc.sync_interval;
    
    oss << ", Present Flags: 0x" << std::hex << prev_present_flags << " -> " << std::hex << desc.present_flags;
    
    oss << " BackBufferCount: " << prev_back_buffer_count << " -> " << desc.back_buffer_count;

    oss << " BackBuffer: " << desc.back_buffer.texture.width << "x" << desc.back_buffer.texture.height;

    oss << " BackBuffer Format: " << (long long)desc.back_buffer.texture.format;

    oss << " BackBuffer Usage: " << (long long)desc.back_buffer.usage;


    // Show which features are enabled in present_flags
    if (desc.present_flags == 0) {
      oss << " (No special flags)";
    } else {
      oss << " - Enabled features:";
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
  // Reset frame count on swapchain init
  l_frame_count.store(0);

  // Increment event counter
  g_swapchain_event_counters[SWAPCHAIN_EVENT_INIT_SWAPCHAIN].fetch_add(1);
  g_swapchain_event_total_count.fetch_add(1);
  
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

void OnPresentUpdateAfter(  reshade::api::command_queue* /*queue*/, reshade::api::swapchain* swapchain) {
  // Increment event counter
  g_swapchain_event_counters[SWAPCHAIN_EVENT_PRESENT_UPDATE_AFTER].fetch_add(1);
  g_swapchain_event_total_count.fetch_add(1);

  // g_present_duration
  LONGLONG now_ticks = get_now_qpc();
  double g_present_duration_new = (now_ticks - g_present_start_time_qpc.load()) * QPC_TO_NS; // Convert QPC ticks to seconds (QPC frequency is typically 10MHz)
  double alpha = 64;
  g_present_duration_ns.store((1 * g_present_duration_new + (alpha - 1) * g_present_duration_ns.load()) / alpha);
  
  // Mark Present end for latent sync limiter timing
  if (s_custom_fps_limiter_enabled.load()) {
    if (dxgi::fps_limiter::g_customFpsLimiterManager) {
      if (s_fps_limiter_mode.load() == 1) {
        auto& latent = dxgi::fps_limiter::g_customFpsLimiterManager->GetLatentLimiter();
        if (latent.IsEnabled()) {
          latent.OnPresentEnd();
        }
      }
    }
  }
  
  // Call Reflex manager callback if enabled
  if (s_reflex_enabled.load() && g_reflexManager && g_reflexManager->IsAvailable()) {
    g_reflexManager->OnPresentUpdateAfter(/*queue*/nullptr, swapchain);
  }

  HandleOnPresentEnd();
}

void flush_command_queue() {
  if (ShouldBackgroundSuppressOperation()) return;
  if (g_reshade_runtime.load() != nullptr) {
    g_reshade_runtime.load()->get_command_queue()->flush_immediate_command_list();
  }
}

void HandleFpsLimiter() {
  flush_command_queue(); // todo only if sleep is happening()
  LONGLONG handle_fps_limiter_start_time_qpc = get_now_qpc();

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
  

  // Mark Present begin for latent sync limiter timing (right before Present executes)
  if (s_custom_fps_limiter_enabled.load()) {
    if (dxgi::fps_limiter::g_customFpsLimiterManager) {
      if (s_fps_limiter_mode.load() == 1) {
        auto& latent = dxgi::fps_limiter::g_customFpsLimiterManager->GetLatentLimiter();
        if (latent.IsEnabled()) {
          latent.OnPresentBegin();
        }
      }
    }
  }
  LONGLONG handle_fps_limiter_start_end_time_qpc = get_now_qpc();
  g_present_start_time_qpc.store(handle_fps_limiter_start_end_time_qpc);

  #define QPC_TO_NS 100
  LONGLONG handle_fps_limiter_start_duration_ns = (handle_fps_limiter_start_end_time_qpc - handle_fps_limiter_start_time_qpc) * QPC_TO_NS;
  fps_sleep_before_on_present_ns.store((handle_fps_limiter_start_duration_ns + (63) * fps_sleep_before_on_present_ns.load()) / 64);
}

// Update composition state after presents (required for valid stats)
void OnPresentUpdateBefore(
    reshade::api::command_queue* /*queue*/, reshade::api::swapchain* swapchain,
    const reshade::api::rect* /*source_rect*/, const reshade::api::rect* /*dest_rect*/,
    uint32_t /*dirty_rect_count*/, const reshade::api::rect* /*dirty_rects*/) {
  HandleEndRenderSubmit();
  if (g_flush_before_present.load()) {
    flush_command_queue(); // Flush command queue before addons start processing to reduce rendering latency caused by reshade
  }
  
  // Increment event counter
  g_swapchain_event_counters[SWAPCHAIN_EVENT_PRESENT_UPDATE_BEFORE].fetch_add(1);
  g_swapchain_event_total_count.fetch_add(1);
  
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
  // Handle keyboard shortcuts
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

  if (s_fps_limiter_injection.load() == 2) {
    HandleFpsLimiter();
  }
}

void OnPresentUpdateBefore2(reshade::api::effect_runtime* runtime) { 
  // Increment event counter
  g_swapchain_event_counters[SWAPCHAIN_EVENT_PRESENT_UPDATE_BEFORE2].fetch_add(1);
  g_swapchain_event_total_count.fetch_add(1);
  // Call Reflex manager callback if enabled
  if (s_reflex_enabled.load() && g_reflexManager && g_reflexManager->IsAvailable()) {
    g_reflexManager->OnPresentUpdateBefore2(runtime);
  }

  if (s_fps_limiter_injection.load() == 1) {
    HandleFpsLimiter();
  }
}


// Additional event handlers for frame timing and composition

void OnInitCommandList(reshade::api::command_list* cmd_list) {
  // Increment event counter
  g_swapchain_event_counters[SWAPCHAIN_EVENT_INIT_COMMAND_LIST].fetch_add(1);
  g_swapchain_event_total_count.fetch_add(1);
  
  // Log command list creation for debugging
  LogDebug("Command list initialized");
}

void OnInitCommandQueue(reshade::api::command_queue* queue) {
  // Increment event counter
  g_swapchain_event_counters[SWAPCHAIN_EVENT_INIT_COMMAND_QUEUE].fetch_add(1);
  g_swapchain_event_total_count.fetch_add(1);
  
  // Log command queue creation for debugging
  LogDebug("Command queue initialized");
}

void OnResetCommandList(reshade::api::command_list* cmd_list) {
  // Increment event counter
  g_swapchain_event_counters[SWAPCHAIN_EVENT_RESET_COMMAND_LIST].fetch_add(1);
  g_swapchain_event_total_count.fetch_add(1);
  
  // Log command list reset for debugging
  LogDebug("Command list reset");
}

void OnExecuteCommandList(reshade::api::command_queue* queue, reshade::api::command_list* cmd_list) {
  // Increment event counter
  g_swapchain_event_counters[SWAPCHAIN_EVENT_EXECUTE_COMMAND_LIST].fetch_add(1);
  g_swapchain_event_total_count.fetch_add(1);
  
  // This event could be used for frame timing analysis
  // as it represents when GPU work is actually submitted
  LogDebug("Command list executed");
}

bool OnBindPipeline(reshade::api::command_list* cmd_list, reshade::api::pipeline_stage stages, reshade::api::pipeline pipeline) {
  // Increment event counter
  g_swapchain_event_counters[SWAPCHAIN_EVENT_BIND_PIPELINE].fetch_add(1);
  g_swapchain_event_total_count.fetch_add(1);
  
  // This event tracks shader pipeline changes
  // which is very useful for frame timing analysis
  LogDebug("Pipeline bound");
  
  // Power saving: skip pipeline binding in background if enabled
  if (s_suppress_binding_in_background.load() && ShouldBackgroundSuppressOperation()) {
    return true; // Skip the pipeline binding
  }
  
  return false; // Don't skip the pipeline binding
}

// Present flags callback to strip DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
void OnPresentFlags(uint32_t* present_flags) {
  // Increment event counter
  g_swapchain_event_counters[SWAPCHAIN_EVENT_PRESENT_FLAGS].fetch_add(1);
  g_swapchain_event_total_count.fetch_add(1);

  // Always strip DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING flag
  if (s_prevent_tearing.load() && *present_flags & DXGI_PRESENT_ALLOW_TEARING) {
    *present_flags &= ~DXGI_PRESENT_ALLOW_TEARING;
    
    // Log the flag removal for debugging
    std::ostringstream oss;
    oss << "Present flags callback: Stripped DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING, new flags: 0x" << std::hex << *present_flags;
    LogInfo(oss.str().c_str());
  }

  if (s_fps_limiter_injection.load() == 0) {
    HandleFpsLimiter();
  }

  if (s_no_present_in_background.load() && g_app_in_background.load(std::memory_order_acquire)) {
    *present_flags = DXGI_PRESENT_DO_NOT_SEQUENCE;
    l_frame_count.fetch_add(1);
  }

  return;
}
