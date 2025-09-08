#include "addon.hpp"
#include "audio/audio_management.hpp"
#include <dxgi.h>
#include <dxgi1_4.h>
#include "utils.hpp"
#include "utils/timing.hpp"
#include <sstream>
#include <minwindef.h>
#include <atomic>
#include "utils/timing.hpp"
#include "hooks/api_hooks.hpp"
#include "widgets/xinput_widget/xinput_widget.hpp"

// Use renodx2 utilities for swapchain color space changes
#include <utils/swapchain.hpp>
#include "globals.hpp"
#include "latent_sync/latent_sync_limiter.hpp"
#include "swapchain_events_power_saving.hpp"
#include "latency/latency_manager.hpp"
#include "ui/new_ui/experimental_tab_settings.hpp"
#include "ui/new_ui/main_new_tab_settings.hpp"


bool is_target_resolution(int width, int height) {
  return width >= 1280 && width <= 3840 && height >= 720 && height <= 2160 && width * 9 == height * 16;
}

std::atomic<LONGLONG> g_present_start_time_ns{0};
std::atomic<LONGLONG> g_present_duration_ns{0};

// Render start time tracking
std::atomic<LONGLONG> g_submit_start_time_ns{0};

// Present after end time tracking
std::atomic<LONGLONG> g_present_after_end_time_ns{0};

// Simulation duration tracking
std::atomic<LONGLONG> g_simulation_duration_ns{0};

// FPS limiter start duration tracking (nanoseconds)
std::atomic<LONGLONG> fps_sleep_before_on_present_ns{0};

// FPS limiter start duration tracking (nanoseconds)
std::atomic<LONGLONG> fps_sleep_after_on_present_ns{0};

// Reshade overhead tracking (nanoseconds)
std::atomic<LONGLONG> g_reshade_overhead_duration_ns{0};

// Render submit end time tracking (QPC)
std::atomic<LONGLONG> g_render_submit_end_time_ns{0};

// Render submit duration tracking (nanoseconds)
std::atomic<LONGLONG> g_render_submit_duration_ns{0};

std::atomic<int> l_frame_count{0};

void HandleRenderStartAndEndTimes() {
  LONGLONG expected = 0;
  if (g_submit_start_time_ns.load() == 0) {
    LONGLONG now_ns = utils::get_now_ns();
    LONGLONG present_after_end_time_ns = g_present_after_end_time_ns.load();
    if (present_after_end_time_ns > 0 && g_submit_start_time_ns.compare_exchange_strong(expected, now_ns)) {
        // Compare to g_present_after_end_time
        LONGLONG g_simulation_duration_ns_new = (now_ns - present_after_end_time_ns);
        int alpha = 64;
        g_simulation_duration_ns.store( (1 * g_simulation_duration_ns_new + (alpha - 1) * g_simulation_duration_ns.load()) / alpha);


    //  reshade::api::swapchain* swapchain = g_last_swapchain_ptr.load(std::memory_order_acquire);
      if (s_reflex_enable.load()) {
    //    auto* device = swapchain ? swapchain->get_device() : nullptr;
    //    if (device && g_latencyManager->Initialize(device)) {
          if (s_reflex_use_markers.load()) {
            g_latencyManager->SetMarker(LatencyMarkerType::SIMULATION_END);
            g_latencyManager->SetMarker(LatencyMarkerType::RENDERSUBMIT_START);
          }
    //    }
      }
    }
  }
}

void HandleEndRenderSubmit() {
  LONGLONG now_ns = utils::get_now_ns();
  g_render_submit_end_time_ns.store(now_ns);
  if (g_submit_start_time_ns.load() > 0) {
    LONGLONG g_render_submit_duration_ns_new = (now_ns - g_submit_start_time_ns.load());
    int alpha = 64;
    g_render_submit_duration_ns.store( (1 * g_render_submit_duration_ns_new + (alpha - 1) * g_render_submit_duration_ns.load()) / alpha);
  }
}

void HandleOnPresentEnd() {
  LONGLONG now_ns = utils::get_now_ns();

  g_present_after_end_time_ns.store(now_ns);
  g_submit_start_time_ns.store(0);

  if (g_render_submit_end_time_ns.load() > 0) {
    LONGLONG g_reshade_overhead_duration_ns_new = (now_ns - g_render_submit_end_time_ns.load());
    int alpha = 64;
    g_reshade_overhead_duration_ns.store( (1 * g_reshade_overhead_duration_ns_new + (alpha - 1) * g_reshade_overhead_duration_ns.load()) / alpha);
  }
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

// Convert ComboSetting value to reshade::api::format
static reshade::api::format GetFormatFromComboValue(int combo_value) {
  switch (combo_value) {
    case 0: return reshade::api::format::r8g8b8a8_unorm;      // R8G8B8A8_UNORM
    case 1: return reshade::api::format::r10g10b10a2_unorm;   // R10G10B10A2_UNORM
    case 2: return reshade::api::format::r16g16b16a16_float;  // R16G16B16A16_FLOAT
    default: return reshade::api::format::r8g8b8a8_unorm;     // Default fallback
  }
}

// Calculate target resolution for buffer upgrade
static std::pair<uint32_t, uint32_t> CalculateBufferUpgradeResolution(uint32_t original_width, uint32_t original_height) {
  if (!ui::new_ui::g_experimentalTabSettings.buffer_resolution_upgrade_enabled.GetValue()) {
    return {original_width, original_height}; // No upgrade
  }
  int mode = ui::new_ui::g_experimentalTabSettings.buffer_resolution_upgrade_mode.GetValue();

  switch (mode) {
    case 0: // Upgrade 1280x720 by scale factor
      if (is_target_resolution(original_width, original_height)) {
        double scale_factor = 3840.0 / original_width;
        return {static_cast<uint32_t>(round(original_width * scale_factor)), static_cast<uint32_t>(round(original_height * scale_factor))};
      }
      break;

    case 1: // Upgrade by Scale Factor
      {
        int scale_factor = ui::new_ui::g_experimentalTabSettings.buffer_resolution_upgrade_scale_factor.GetValue();
        return {original_width * scale_factor, original_height * scale_factor};
      }

    case 2: // Upgrade Custom Resolution
      return {
        static_cast<uint32_t>(ui::new_ui::g_experimentalTabSettings.buffer_resolution_upgrade_width.GetValue()),
        static_cast<uint32_t>(ui::new_ui::g_experimentalTabSettings.buffer_resolution_upgrade_height.GetValue())
      };

    default:
      break;
  }

  return {original_width, original_height}; // No change
}

// Calculate viewport scale factor for buffer resolution upgrade
static std::pair<float, float> CalculateViewportScaleFactor(uint32_t original_width, uint32_t original_height) {
  if (!ui::new_ui::g_experimentalTabSettings.buffer_resolution_upgrade_enabled.GetValue()) {
    return {1.0f, 1.0f}; // No scaling
  }

  int mode = ui::new_ui::g_experimentalTabSettings.buffer_resolution_upgrade_mode.GetValue();

  switch (mode) {
    case 0: // Upgrade 1280x720 to 2560x1440 (2x)
      if (is_target_resolution(original_width, original_height)) {
        float scale_new = 3840.0f / original_width;
        return {scale_new, scale_new}; // 2x scale
      }
      break;

    case 1: // Upgrade by Scale Factor
      {
        int scale_factor = ui::new_ui::g_experimentalTabSettings.buffer_resolution_upgrade_scale_factor.GetValue();
        return {static_cast<float>(scale_factor), static_cast<float>(scale_factor)};
      }

    case 2: // Upgrade Custom Resolution
      {
        int target_width = ui::new_ui::g_experimentalTabSettings.buffer_resolution_upgrade_width.GetValue();
        int target_height = ui::new_ui::g_experimentalTabSettings.buffer_resolution_upgrade_height.GetValue();
        return {
          static_cast<float>(target_width) / static_cast<float>(original_width),
          static_cast<float>(target_height) / static_cast<float>(original_height)
        };
      }

    default:
      break;
  }

  return {1.0f, 1.0f}; // No scaling
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

  // Apply backbuffer format override if enabled
  if (ui::new_ui::g_experimentalTabSettings.backbuffer_format_override_enabled.GetValue()) {
    reshade::api::format original_format = desc.back_buffer.texture.format;
    reshade::api::format target_format = GetFormatFromComboValue(ui::new_ui::g_experimentalTabSettings.backbuffer_format_override.GetValue());

    if (original_format != target_format) {
      desc.back_buffer.texture.format = target_format;
      modified = true;

      // Log the format change
      std::ostringstream format_oss;
      format_oss << "Backbuffer format override: " << static_cast<int>(original_format)
                 << " -> " << static_cast<int>(target_format);
      LogInfo("%s", format_oss.str().c_str());
    }
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
  if (swapchain == nullptr) {
    LogDebug("OnInitSwapchain: swapchain is null");
    return;
  }
  // Reset frame count on swapchain init
  l_frame_count.store(0);

  // Increment event counter
  g_swapchain_event_counters[SWAPCHAIN_EVENT_INIT_SWAPCHAIN].fetch_add(1);
  g_swapchain_event_total_count.fetch_add(1);

  // Schedule auto-apply even on resizes (generation counter ensures only latest runs)
  HWND hwnd = static_cast<HWND>(swapchain->get_hwnd());
  if (hwnd != nullptr) {
    g_last_swapchain_hwnd.store(hwnd);
    // Set the game window for API hooks
    renodx::hooks::SetGameWindow(hwnd);
  }
}

HANDLE g_timer_handle = nullptr;
void TimerPresentPacingDelay() {
  LONGLONG start_ns = utils::get_now_ns();
  float delay_percentage = s_present_pacing_delay_percentage.load();
  if (delay_percentage > 0.0f) {
    // Calculate frame time from the most recent performance sample
    const uint32_t head = g_perf_ring_head.load(std::memory_order_acquire);
    if (head > 0) {
      const uint32_t last_idx = (head - 1) & (kPerfRingCapacity - 1);
      const PerfSample& last_sample = g_perf_ring[last_idx];
      if (last_sample.fps > 0.0f) {
        // Convert FPS to frame time in milliseconds, then to nanoseconds
        float frame_time_ms = 1000.0f / last_sample.fps;
        float delay_ms = frame_time_ms * (delay_percentage / 100.0f);
        LONGLONG delta_ns = static_cast<LONGLONG>(delay_ms * utils::NS_TO_MS);
        delta_ns -= late_amount_ns.load();
        if (delta_ns > 0) {
          utils::wait_until_ns(utils::get_now_ns() + delta_ns, g_timer_handle);
        }
      }
    }
  }

  LONGLONG end_ns = utils::get_now_ns();
  fps_sleep_after_on_present_ns.store(end_ns - start_ns);
}

void OnPresentUpdateAfter(reshade::api::command_queue* /*queue*/, reshade::api::swapchain* swapchain) {
  // Increment event counter
  g_swapchain_event_counters[SWAPCHAIN_EVENT_PRESENT_UPDATE_AFTER].fetch_add(1);
  g_swapchain_event_total_count.fetch_add(1);
  if (s_reflex_enable.load()) {
    auto* device = swapchain ? swapchain->get_device() : nullptr;
    if (device && g_latencyManager->Initialize(device)) {
      if (s_reflex_use_markers.load()) {
        g_latencyManager->SetMarker(LatencyMarkerType::PRESENT_END);
      }
    }
  }

  // g_present_duration
  LONGLONG now_ns = utils::get_now_ns();

  double g_present_duration_new_ns = (now_ns - g_present_start_time_ns.load()); // Convert QPC ticks to seconds (QPC frequency is typically 10MHz)
  double alpha = 64;
  g_present_duration_ns.store((1 * g_present_duration_new_ns + (alpha - 1) * g_present_duration_ns.load()) / alpha);

  // Mark Present end for latent sync limiter timing
  if (dxgi::latent_sync::g_latentSyncManager) {
    auto& latent = dxgi::latent_sync::g_latentSyncManager->GetLatentLimiter();
    latent.OnPresentEnd();
  }
  TimerPresentPacingDelay();
  HandleOnPresentEnd();

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
      LogInfo("Block input in background: %s wants to block: %s", is_background ? "true" : "false", wants_block_input ? "true" : "false");
    } else {
      // Log error when g_reshade_runtime is null and input blocking is desired
      const bool is_background = g_app_in_background.load(std::memory_order_acquire);
      const bool wants_block_input = s_block_input_in_background.load() && is_background;
      if (wants_block_input) {
        LogError("Block input in background failed: g_reshade_runtime is null (background=%s, setting=%s)",
                 is_background ? "true" : "false",
                 s_block_input_in_background.load() ? "true" : "false");
      }
    }
  }

  // DXGI composition state computation and periodic device/colorspace refresh
  // (moved from continuous monitoring thread to avoid accessing g_last_swapchain_ptr from background thread)
  if (swapchain != nullptr) {
    // Periodically refresh colorspace and enumerate devices (approx every 4 seconds at 60fps = 240 frames)
    static int present_after_counter = 0;
    present_after_counter++;
    if (present_after_counter >= 240) { // Refresh every 240 presents (about 4 seconds at 60fps)
      // Compute DXGI composition state and log on change
      DxgiBypassMode mode = GetIndependentFlipState(swapchain);
      int state = 0;
      switch (mode) {
          case DxgiBypassMode::kComposed: state = 1; break;
          case DxgiBypassMode::kOverlay: state = 2; break;
          case DxgiBypassMode::kIndependentFlip: state = 3; break;
          case DxgiBypassMode::kUnknown:
          default: state = 0; break;
      }

      // Update shared state for fast reads on present
      s_dxgi_composition_state.store(state);

      int last = g_comp_last_logged.load();
      if (state != last) {
          g_comp_last_logged.store(state);
          std::ostringstream oss;
          oss << "DXGI Composition State (OnPresentAfter): " << DxgiBypassModeToString(mode) << " (" << state << ")";
          LogInfo(oss.str().c_str());
      }

        present_after_counter = 0;
        g_current_colorspace = swapchain->get_color_space();

        extern std::unique_ptr<DXGIDeviceInfoManager> g_dxgiDeviceInfoManager;
        if (g_dxgiDeviceInfoManager && g_dxgiDeviceInfoManager->IsInitialized()) {
            g_dxgiDeviceInfoManager->EnumerateDevicesOnPresent();
        }
    }
  }

  // NVIDIA Reflex: SIMULATION_END marker (minimal) and Sleep
  if (s_reflex_enable.load()) {
    auto* device = swapchain ? swapchain->get_device() : nullptr;
    if (device && g_latencyManager->Initialize(device)) {
      g_latencyManager->IncreaseFrameId();
      // Apply sleep mode opportunistically each frame to reflect current toggles
      g_latencyManager->ApplySleepMode(s_reflex_low_latency.load(), s_reflex_boost.load(), s_reflex_use_markers.load());
      g_latencyManager->Sleep();
      if (s_reflex_use_markers.load()) {
        g_latencyManager->SetMarker(LatencyMarkerType::SIMULATION_START);
      }
    }
  } else {
    if (g_latencyManager->IsInitialized()) {
      g_latencyManager->Shutdown();
    }
  }
}

void flush_command_queue() {
  if (ShouldBackgroundSuppressOperation()) return;
  if (g_reshade_runtime.load() != nullptr) {
    g_reshade_runtime.load()->get_command_queue()->flush_immediate_command_list();
  } else {
    LogError("flush_command_queue failed: g_reshade_runtime is null");
  }
}

void HandleFpsLimiter() {
  LONGLONG handle_fps_limiter_start_time_ns = utils::get_now_ns();
  // Use background flag computed by monitoring thread; avoid GetForegroundWindow here

  float target_fps = 0.0f;
  if (g_app_in_background.load()) {
    target_fps = s_fps_limit_background.load();
  } else {
    target_fps = s_fps_limit.load();
  }
  if (target_fps > 0.0f && target_fps < 5.0f) {
    target_fps = 5.0f;
  }
  if (target_fps > 0.0f) {
    flush_command_queue();
  }

  late_amount_ns.store(0);

  // Call FPS Limiter on EVERY frame (not throttled)
  switch (s_fps_limiter_mode.load()) {
    case FpsLimiterMode::kNone: {
      // No FPS limiting - do nothing
      break;
    }
    case FpsLimiterMode::kCustom: {
      // Use FPS limiter manager for Custom (Sleep/Spin) mode
      if (dxgi::fps_limiter::g_customFpsLimiterManager) {
        auto& limiter = dxgi::fps_limiter::g_customFpsLimiterManager->GetFpsLimiter();
        if (target_fps > 0.0f) {
          limiter.LimitFrameRate(target_fps);
        }
      }
      break;
    }
    case FpsLimiterMode::kLatentSync: {
      // Use latent sync manager for VBlank Scanline Sync mode
      if (dxgi::latent_sync::g_latentSyncManager) {
        auto& latent = dxgi::latent_sync::g_latentSyncManager->GetLatentLimiter();
        if (target_fps > 0.0f) {
          latent.LimitFrameRate();
        }
      }
      break;
    }
  }

  LONGLONG handle_fps_limiter_start_end_time_ns = utils::get_now_ns();
  g_present_start_time_ns.store(handle_fps_limiter_start_end_time_ns);

  LONGLONG handle_fps_limiter_start_duration_ns = handle_fps_limiter_start_end_time_ns - handle_fps_limiter_start_time_ns;
  fps_sleep_before_on_present_ns.store((handle_fps_limiter_start_duration_ns + (63) * fps_sleep_before_on_present_ns.load()) / 64);
}

// Update composition state after presents (required for valid stats)
void OnPresentUpdateBefore(
    reshade::api::command_queue* /*queue*/, reshade::api::swapchain* swapchain,
    const reshade::api::rect* /*source_rect*/, const reshade::api::rect* /*dest_rect*/,
    uint32_t /*dirty_rect_count*/, const reshade::api::rect* /*dirty_rects*/) {

  HandleRenderStartAndEndTimes();

  HandleEndRenderSubmit();
  // NVIDIA Reflex: RENDERSUBMIT_END marker (minimal)
  if (s_reflex_enable.load()) {
    auto* device = swapchain ? swapchain->get_device() : nullptr;
    if (device && g_latencyManager->Initialize(device)) {
      if (s_reflex_use_markers.load()) {
        g_latencyManager->SetMarker(LatencyMarkerType::RENDERSUBMIT_END);
      }
    }
  }
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


  // Handle keyboard shortcuts (only when game is in foreground)
  HWND game_hwnd = g_last_swapchain_hwnd.load();
  bool is_game_in_foreground = (game_hwnd != nullptr && GetForegroundWindow() == game_hwnd);

  if (s_enable_mute_unmute_shortcut.load() && is_game_in_foreground) {
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
    } else {
      LogError("Mute/unmute shortcut failed: g_reshade_runtime is null");
    }
  }

  // Handle Ctrl+R shortcut for background toggle (only when game is in foreground)
  if (s_enable_background_toggle_shortcut.load() && is_game_in_foreground) {
    // Get the runtime from the atomic variable
    reshade::api::effect_runtime* runtime = g_reshade_runtime.load();
    if (runtime != nullptr) {
      // Check for Ctrl+R shortcut
      if (runtime->is_key_pressed('R') && runtime->is_key_down(VK_CONTROL)) {
        // Toggle render setting and make present follow the same state
        bool new_render_state = !s_no_render_in_background.load();
        bool new_present_state = new_render_state; // Present always follows render state

        s_no_render_in_background.store(new_render_state);
        s_no_present_in_background.store(new_present_state);

        // Update the settings in the UI as well
        ui::new_ui::g_main_new_tab_settings.no_render_in_background.SetValue(new_render_state);
        ui::new_ui::g_main_new_tab_settings.no_present_in_background.SetValue(new_present_state);

        // Log the action
        std::ostringstream oss;
        oss << "Background settings toggled via Ctrl+R shortcut - Both Render and Present: "
            << (new_render_state ? "disabled" : "enabled");
        LogInfo(oss.str().c_str());
      }
    } else {
      LogError("Background toggle shortcut failed: g_reshade_runtime is null");
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

  // Check for XInput chord screenshot trigger
  display_commander::widgets::xinput_widget::CheckAndHandleScreenshot();

  if (s_fps_limiter_injection.load() == 1) {
    HandleFpsLimiter();
  }
}


bool OnBindPipeline(reshade::api::command_list* cmd_list, reshade::api::pipeline_stage stages, reshade::api::pipeline pipeline) {
  // Increment event counter
  g_swapchain_event_counters[SWAPCHAIN_EVENT_BIND_PIPELINE].fetch_add(1);
  g_swapchain_event_total_count.fetch_add(1);

  // Power saving: skip pipeline binding in background if enabled
  if (s_suppress_binding_in_background.load() && ShouldBackgroundSuppressOperation()) {
    return true; // Skip the pipeline binding
  }

  return false; // Don't skip the pipeline binding
}

// Present flags callback to strip DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
void OnPresentFlags(uint32_t* present_flags, reshade::api::swapchain* swapchain) {
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

  // Don't block presents if continue rendering is enabled
  if (s_no_present_in_background.load() && g_app_in_background.load(std::memory_order_acquire) && !s_continue_rendering.load()) {
    *present_flags = DXGI_PRESENT_DO_NOT_SEQUENCE;
  }
  if (s_reflex_enable.load()) {
    auto* device = swapchain ? swapchain->get_device() : nullptr;
    if (device && g_latencyManager->Initialize(device)) {
      if (s_reflex_use_markers.load()) {
        g_latencyManager->SetMarker(LatencyMarkerType::PRESENT_START);
      }
    }
  }
  l_frame_count.fetch_add(1);

  return;
}

// Resource creation event handler to upgrade buffer resolutions and texture formats
bool OnCreateResource(reshade::api::device* device, reshade::api::resource_desc& desc, reshade::api::subresource_data* /*initial_data*/, reshade::api::resource_usage /*usage*/) {
  bool modified = false;

  // Only handle 2D textures
  if (desc.type != reshade::api::resource_type::texture_2d) {
    return false; // No modification needed
  }

  // Handle buffer resolution upgrade if enabled
  if (ui::new_ui::g_experimentalTabSettings.buffer_resolution_upgrade_enabled.GetValue()) {
    uint32_t original_width = desc.texture.width;
    uint32_t original_height = desc.texture.height;

    auto [target_width, target_height] = CalculateBufferUpgradeResolution(original_width, original_height);

    if (original_width != target_width || original_height != target_height) {
      desc.texture.width = target_width;
      desc.texture.height = target_height;

      // Log the resolution upgrade
      std::ostringstream res_oss;
      res_oss << "Buffer resolution upgrade: " << original_width << "x" << original_height
              << " -> " << target_width << "x" << target_height;
      LogInfo("%s", res_oss.str().c_str());

      modified = true;
    }
  }

  // Handle texture format upgrade if enabled
  if (ui::new_ui::g_experimentalTabSettings.texture_format_upgrade_enabled.GetValue()) {
    reshade::api::format original_format = desc.texture.format;
    reshade::api::format target_format = reshade::api::format::r16g16b16a16_float; // RGB16A16

    // Only upgrade certain formats to RGB16A16
    bool should_upgrade_format = false;
    switch (original_format) {
      case reshade::api::format::r8g8b8a8_typeless:
      case reshade::api::format::r8g8b8a8_unorm_srgb:
      case reshade::api::format::r8g8b8a8_unorm:
      case reshade::api::format::b8g8r8a8_unorm:
      case reshade::api::format::r8g8b8a8_snorm:
      case reshade::api::format::b8g8r8a8_typeless:
      case reshade::api::format::r8g8b8a8_uint:
      case reshade::api::format::r8g8b8a8_sint:
        should_upgrade_format = true;
        break;
      default:
        // Don't upgrade formats that are already high precision or special formats
        break;
    }

    // Only upgrade textures at specific resolutions (720p, 1440p, 4K)
    bool should_upgrade_resolution = false;
    uint32_t width = desc.texture.width;
    uint32_t height = desc.texture.height;

    // Check for common resolutions: 720p (1280x720), 1440p (2560x1440), 4K (3840x2160)
    if (is_target_resolution(width, height)) {
      should_upgrade_resolution = true;
    }

    if (should_upgrade_format && should_upgrade_resolution && original_format != target_format) {
      desc.texture.format = target_format;

      // Log the format upgrade
      std::ostringstream format_oss;
      format_oss << "Texture format upgrade: " << static_cast<int>(original_format)
                 << " -> " << static_cast<int>(target_format) << " (RGB16A16) at " << width << "x" << height;
      LogInfo("%s", format_oss.str().c_str());

      modified = true;
    }
  }

  return modified;
}

// Resource view creation event handler to upgrade render target views for buffer resolution and texture format upgrades
bool OnCreateResourceView(reshade::api::device* device, reshade::api::resource resource, reshade::api::resource_usage usage_type, reshade::api::resource_view_desc& desc) {
  bool modified = false;

  // Get the resource description to check if it was upgraded
  if (!device) return false;

  // Get the resource description
  reshade::api::resource_desc resource_desc = device->get_resource_desc(resource);

  // Only handle 2D textures
  if (resource_desc.type != reshade::api::resource_type::texture_2d) {
    return false; // No modification needed
  }

  // Handle buffer resolution upgrade if enabled
  if (ui::new_ui::g_experimentalTabSettings.buffer_resolution_upgrade_enabled.GetValue()) {
    // Only handle render target views for resolution upgrade
    if (usage_type == reshade::api::resource_usage::render_target) {
      uint32_t original_width = resource_desc.texture.width;
      uint32_t original_height = resource_desc.texture.height;

      auto [target_width, target_height] = CalculateBufferUpgradeResolution(original_width, original_height);

      // If the resource was upgraded, log the resource view creation
      if (original_width != target_width || original_height != target_height) {
        // Log the resource view creation for upgraded resource
        std::ostringstream view_oss;
        view_oss << "Resource view created for upgraded render target: " << original_width << "x" << original_height
                 << " -> " << target_width << "x" << target_height;
        LogInfo("%s", view_oss.str().c_str());

        // Note: resource_view_desc doesn't have width/height fields
        // The view is automatically tied to the upgraded resource dimensions
        modified = true;
      }
    }
  }

  // Handle texture format upgrade if enabled
  if (ui::new_ui::g_experimentalTabSettings.texture_format_upgrade_enabled.GetValue()) {
    reshade::api::format resource_format = resource_desc.texture.format;
    reshade::api::format target_format = reshade::api::format::r16g16b16a16_float; // RGB16A16

    // Check if the resource was upgraded to RGB16A16
    if (resource_format == target_format) {
      // If the resource was upgraded to RGB16A16, we need to upgrade the view format too
      reshade::api::format original_view_format = desc.format;

      // Only upgrade view formats that match the original texture formats we upgrade
      bool should_upgrade_view = false;
      switch (original_view_format) {
        case reshade::api::format::r8g8b8a8_typeless:
        case reshade::api::format::r8g8b8a8_unorm_srgb:
        case reshade::api::format::r8g8b8a8_unorm:
        case reshade::api::format::b8g8r8a8_unorm:
        case reshade::api::format::r8g8b8a8_snorm:
        case reshade::api::format::r8g8b8a8_uint:
        case reshade::api::format::r8g8b8a8_sint:
          should_upgrade_view = true;
          break;
        default:
          // Don't upgrade view formats that are already high precision or special formats
          break;
      }

      if (should_upgrade_view && original_view_format != target_format) {
        desc.format = target_format;

        // Log the view format upgrade
        std::ostringstream view_format_oss;
        view_format_oss << "Resource view format upgrade: " << static_cast<int>(original_view_format)
                        << " -> " << static_cast<int>(target_format) << " (RGB16A16)";
        LogInfo("%s", view_format_oss.str().c_str());

        modified = true;
      }
    }
  }

  return modified;
}

// Viewport event handler to scale viewports for buffer resolution upgrade
void OnSetViewport(reshade::api::command_list* cmd_list, uint32_t first, uint32_t count, const reshade::api::viewport* viewports) {
  // Only handle viewport scaling if buffer resolution upgrade is enabled
  if (!ui::new_ui::g_experimentalTabSettings.buffer_resolution_upgrade_enabled.GetValue()) {
    return; // No modification needed
  }

  // Get the current backbuffer to determine if we need to scale
  auto* device = cmd_list->get_device();
  if (!device) return;

  int mode = ui::new_ui::g_experimentalTabSettings.buffer_resolution_upgrade_mode.GetValue();
  bool needs_scaling = false;

  if (mode == 0) { // Upgrade 1280x720 by scale factor
    int scale_factor = ui::new_ui::g_experimentalTabSettings.buffer_resolution_upgrade_scale_factor.GetValue();
    // Check if any viewport matches the source dimensions we want to upgrade
    for (uint32_t i = 0; i < count; i++) {
      const auto& viewport = viewports[i];
      // Only scale viewports that match the source resolution (1280x720)
      if (is_target_resolution(viewport.width, viewport.height)) {
        needs_scaling = true;
        break;
      }
    }
  }

    if (needs_scaling) {
    int scale_factor = ui::new_ui::g_experimentalTabSettings.buffer_resolution_upgrade_scale_factor.GetValue();
    float scale_f = static_cast<float>(scale_factor);

    // Create scaled viewports only for matching dimensions
    std::vector<reshade::api::viewport> scaled_viewports(count);
    for (uint32_t i = 0; i < count; i++) {
      const auto& viewport = viewports[i];

      // Only scale viewports that match the source resolution
      if (is_target_resolution(viewport.width, viewport.height)) {
        double scale_new = 3840.0 / viewport.width;
        scaled_viewports[i] = {
          static_cast<float>(viewport.x * scale_new),      // x
          static_cast<float>(viewport.y * scale_new),      // y
          static_cast<float>(viewport.width * scale_new),  // width (1280 -> 1280*scale)
          static_cast<float>(viewport.height * scale_new), // height (720 -> 720*scale)
          viewport.min_depth,        // min_depth
          viewport.max_depth         // max_depth
        };

        // Log the viewport scaling
        std::ostringstream viewport_oss;
        viewport_oss << "Viewport scaling: " << viewport.x << "," << viewport.y
                     << " " << viewport.width << "x" << viewport.height
                     << " -> " << scaled_viewports[i].x << "," << scaled_viewports[i].y
                     << " " << scaled_viewports[i].width << "x" << scaled_viewports[i].height;
        LogInfo("%s", viewport_oss.str().c_str());
      } else {
        // Keep original viewport for non-matching dimensions
        scaled_viewports[i] = viewport;
      }
    }

    // Set the scaled viewports - this will override the original viewport setting
    cmd_list->bind_viewports(first, count, scaled_viewports.data());
  }
}

// Scissor rectangle event handler to scale scissor rectangles for buffer resolution upgrade
void OnSetScissorRects(reshade::api::command_list* cmd_list, uint32_t first, uint32_t count, const reshade::api::rect* rects) {
  // Only handle scissor scaling if buffer resolution upgrade is enabled
  if (!ui::new_ui::g_experimentalTabSettings.buffer_resolution_upgrade_enabled.GetValue()) {
    return; // No modification needed
  }

  int mode = ui::new_ui::g_experimentalTabSettings.buffer_resolution_upgrade_mode.GetValue();
  bool needs_scaling = false;

  if (mode == 0) { // Upgrade 1280x720 by scale factor
    int scale_factor = ui::new_ui::g_experimentalTabSettings.buffer_resolution_upgrade_scale_factor.GetValue();
    // Check if any scissor rectangle matches the source dimensions we want to upgrade
    for (uint32_t i = 0; i < count; i++) {
      const auto& rect = rects[i];
      // Only scale scissor rectangles that match the source resolution (1280x720)
      if (is_target_resolution(rect.right - rect.left, rect.bottom - rect.top)) {
        needs_scaling = true;
        break;
      }
    }
  }

  if (needs_scaling) {
    int scale_factor = ui::new_ui::g_experimentalTabSettings.buffer_resolution_upgrade_scale_factor.GetValue();

    // Create scaled scissor rectangles only for matching dimensions
    std::vector<reshade::api::rect> scaled_rects(count);
    for (uint32_t i = 0; i < count; i++) {
      const auto& rect = rects[i];

      // Only scale scissor rectangles that match the source resolution
      if (is_target_resolution(rect.right - rect.left, rect.bottom - rect.top)) {
        double scale_new = 3840.0 / (rect.right - rect.left);
        scaled_rects[i] = {
          static_cast<int32_t>(round(rect.left * scale_new)),    // left
          static_cast<int32_t>(round(rect.top * scale_new)),     // top
          static_cast<int32_t>(round(rect.right * scale_new)),   // right
          static_cast<int32_t>(round(rect.bottom * scale_new))   // bottom
        };

        // Log the scissor scaling
        std::ostringstream scissor_oss;
        scissor_oss << "Scissor scaling: " << rect.left << "," << rect.top
                    << " " << rect.right << "x" << rect.bottom
                    << " -> " << scaled_rects[i].left << "," << scaled_rects[i].top
                    << " " << scaled_rects[i].right << "x" << scaled_rects[i].bottom;
        LogInfo("%s", scissor_oss.str().c_str());
      } else {
        // Keep original scissor rectangle for non-matching dimensions
        scaled_rects[i] = rect;
      }
    }

    // Set the scaled scissor rectangles
    cmd_list->bind_scissor_rects(first, count, scaled_rects.data());
  }
}
