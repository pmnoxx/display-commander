#include "dxgi_present_hooks.hpp"
#include "../../globals.hpp"
#include "../../latent_sync/refresh_rate_monitor_integration.hpp"
#include "../../performance_types.hpp"
#include "../../settings/advanced_tab_settings.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../swapchain_events.hpp"
#include "../../ui/new_ui/new_ui_tabs.hpp"
#include "../../utils/detour_call_tracker.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/perf_measurement.hpp"
#include "../../utils/timing.hpp"
#include "../hook_suppression_manager.hpp"
#include "../present_traffic_tracking.hpp"
#include "dxgi_gpu_completion.hpp"

#include <dwmapi.h>
#include <MinHook.h>

#include <d3d11_4.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cmath>
#include <memory>
#include <string>

/*
 * IDXGISwapChain VTable Layout Documentation
 * ==========================================
 *
 * IDXGISwapChain inherits from IDXGIDeviceSubObject, which inherits from IDXGIObject,
 * which inherits from IUnknown. The vtable contains all methods from the inheritance chain.
 *
 * VTable Indices 0-18:
 * [0-2]   IUnknown methods (QueryInterface, AddRef, Release)
 * [3-5]   IDXGIObject methods (SetPrivateData, SetPrivateDataInterface, GetPrivateData)
 * [6-7]   IDXGIDeviceSubObject methods (GetDevice, GetParent)
 * [8-18]  IDXGISwapChain methods (Present, GetBuffer, SetFullscreenState, etc.)
 *
 * Note: Index 18 (GetDesc1) is only available in IDXGISwapChain1 and later versions.
 * The base IDXGISwapChain interface only goes up to index 17.
 */

// GPU completion measurement state
namespace {
struct GPUMeasurementState {
    // TODO: caused reshade to report not cleaned up state
    Microsoft::WRL::ComPtr<ID3D11Fence> d3d11_fence;
    Microsoft::WRL::ComPtr<ID3D12Fence> d3d12_fence;
    HANDLE event_handle = nullptr;
    std::atomic<uint64_t> fence_value{0};
    std::atomic<bool> initialized{false};
    std::atomic<bool> is_d3d12{false};
    std::atomic<bool> initialization_attempted{false};

    ~GPUMeasurementState() {
        if (event_handle != nullptr) {
            CloseHandle(event_handle);
            event_handle = nullptr;
        }
    }
};

GPUMeasurementState g_gpu_state;

// Helper function to enqueue GPU completion measurement for D3D11
void EnqueueGPUCompletionD3D11(IDXGISwapChain* swapchain) {
    if (settings::g_mainTabSettings.gpu_measurement_enabled.GetValue() == 0) {
        g_gpu_fence_failure_reason.store("GPU measurement disabled");
        return;
    }

    Microsoft::WRL::ComPtr<ID3D11Device5> device5;
    HRESULT hr = swapchain->GetDevice(IID_PPV_ARGS(&device5));
    if (FAILED(hr)) {
        g_gpu_fence_failure_reason.store("D3D11: Failed to get device from swapchain");
        return;
    }
    // Initialize fence on first use
    if (!g_gpu_state.initialized.load() && !g_gpu_state.initialization_attempted.load()) {
        g_gpu_state.initialization_attempted.store(true);

        hr = device5->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_gpu_state.d3d11_fence));
        if (FAILED(hr)) {
            g_gpu_fence_failure_reason.store("D3D11: CreateFence failed (driver may not support fences)");
            return;
        }

        g_gpu_state.event_handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (g_gpu_state.event_handle == nullptr) {
            g_gpu_state.d3d11_fence.Reset();
            g_gpu_fence_failure_reason.store("D3D11: Failed to create event handle");
            return;
        }

        g_gpu_state.is_d3d12.store(false);
        g_gpu_state.initialized.store(true);
        //  g_gpu_fence_failure_reason.store("success!!!"); // Clear failure reason on success
    }

    if (!static_cast<bool>(g_gpu_state.d3d11_fence)) {
        g_gpu_fence_failure_reason.store("D3D11: Fence not initialized");
        return;
    }

    // Get immediate context and signal fence
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    device5->GetImmediateContext(&context);

    Microsoft::WRL::ComPtr<ID3D11DeviceContext4> context4;
    hr = context.As(&context4);
    if (FAILED(hr)) {
        g_gpu_fence_failure_reason.store("D3D11: ID3D11DeviceContext4 not supported (requires D3D11.3+)");
        return;
    }

    uint64_t signal_value = g_gpu_state.fence_value.fetch_add(1) + 1;

    // Signal the fence from GPU
    hr = context4->Signal(g_gpu_state.d3d11_fence.Get(), signal_value);
    if (FAILED(hr)) {
        g_gpu_fence_failure_reason.store("D3D11: Failed to signal fence");
        return;
    }

    // Set event to trigger when fence reaches this value
    hr = g_gpu_state.d3d11_fence->SetEventOnCompletion(signal_value, g_gpu_state.event_handle);
    if (FAILED(hr)) {
        g_gpu_fence_failure_reason.store("D3D11: SetEventOnCompletion failed");
        return;
    }

    // Store the event handle for external threads to wait on
    g_gpu_completion_event.store(g_gpu_state.event_handle);
    g_gpu_fence_failure_reason.store(nullptr);  // Clear failure reason on success
}

// Helper function to enqueue GPU completion measurement for D3D12
void EnqueueGPUCompletionD3D12(IDXGISwapChain* swapchain, ID3D12CommandQueue* command_queue) {
    if (settings::g_mainTabSettings.gpu_measurement_enabled.GetValue() == 0) {
        return;
    }

    Microsoft::WRL::ComPtr<ID3D12Device> device;
    HRESULT hr = swapchain->GetDevice(IID_PPV_ARGS(&device));
    if (FAILED(hr)) {
        g_gpu_fence_failure_reason.store("D3D12: Failed to get device from swapchain");
        return;
    }

    // Initialize fence on first use
    if (!g_gpu_state.initialized.load() && !g_gpu_state.initialization_attempted.load()) {
        g_gpu_state.initialization_attempted.store(true);

        hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_gpu_state.d3d12_fence));
        if (FAILED(hr)) {
            g_gpu_fence_failure_reason.store("D3D12: CreateFence failed");
            return;
        }

        g_gpu_state.event_handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (g_gpu_state.event_handle == nullptr) {
            g_gpu_state.d3d12_fence.Reset();
            g_gpu_fence_failure_reason.store("D3D12: Failed to create event handle");
            return;
        }

        g_gpu_state.is_d3d12.store(true);
        g_gpu_state.initialized.store(true);
    }

    if (!static_cast<bool>(g_gpu_state.d3d12_fence)) {
        g_gpu_fence_failure_reason.store("D3D12: Fence not initialized");
        return;
    }

    // Check if command queue is available
    if (command_queue == nullptr) {
        g_gpu_fence_failure_reason.store("D3D12: Command queue not provided (cannot signal fence)");
        return;
    }

    // Increment fence value and signal it on the command queue
    uint64_t signal_value = g_gpu_state.fence_value.fetch_add(1) + 1;

    // Set event to trigger when fence reaches this value
    hr = g_gpu_state.d3d12_fence->SetEventOnCompletion(signal_value, g_gpu_state.event_handle);
    if (FAILED(hr)) {
        g_gpu_fence_failure_reason.store("D3D12: SetEventOnCompletion failed");
        return;
    }

    // Signal the fence on the command queue
    // This will be signaled when the GPU completes all work up to this point
    hr = command_queue->Signal(g_gpu_state.d3d12_fence.Get(), signal_value);
    if (FAILED(hr)) {
        g_gpu_fence_failure_reason.store("D3D12: Failed to signal fence on command queue");
        return;
    }

    // Store the event handle for external threads to wait on
    g_gpu_completion_event.store(g_gpu_state.event_handle);
    g_gpu_fence_failure_reason.store(nullptr);  // Clear failure reason on success
}

// Helper function to enqueue GPU completion measurement (auto-detects API)
void EnqueueGPUCompletionInternal(IDXGISwapChain* swapchain, ID3D12CommandQueue* command_queue) {
    if (swapchain == nullptr) {
        g_gpu_fence_failure_reason.store("Failed to get device from swapchain");
        return;
    }
    // Capture g_sim_start_ns for sim-to-display latency measurement
    // Reset tracking flags for this frame
    g_sim_start_ns_for_measurement.store(g_sim_start_ns.load());
    g_present_update_after2_called.store(false);
    g_gpu_completion_callback_finished.store(false);

    // Try D3D12 first

    // Try D3D11
    Microsoft::WRL::ComPtr<ID3D11Device> d3d11_device;
    Microsoft::WRL::ComPtr<ID3D12Device> d3d12_device;
    if (SUCCEEDED(swapchain->GetDevice(IID_PPV_ARGS(&d3d12_device)))) {
        EnqueueGPUCompletionD3D12(swapchain, command_queue);
        return;
    } else if (SUCCEEDED(swapchain->GetDevice(IID_PPV_ARGS(&d3d11_device)))) {
        EnqueueGPUCompletionD3D11(swapchain);
        return;
    } else {
        g_gpu_fence_failure_reason.store("Failed to get device from swapchain");
    }
}

// Cleanup function to reset fences and state when device is destroyed
void CleanupGPUMeasurementState() {
    if (g_gpu_state.initialized.load()) {
        LogInfo("Cleaning up GPU measurement fences on device destruction");

        // Reset fences (ComPtr will automatically release references)
        g_gpu_state.d3d11_fence.Reset();
        g_gpu_state.d3d12_fence.Reset();

        // Close event handle if it exists
        if (g_gpu_state.event_handle != nullptr) {
            CloseHandle(g_gpu_state.event_handle);
            g_gpu_state.event_handle = nullptr;
        }

        // Clear the atomic event handle so monitoring thread stops waiting on invalid handle
        g_gpu_completion_event.store(nullptr);

        // Reset state flags
        g_gpu_state.fence_value.store(0);
        g_gpu_state.initialized.store(false);
        g_gpu_state.is_d3d12.store(false);
        g_gpu_state.initialization_attempted.store(false);

        LogInfo("GPU measurement fences cleaned up successfully");
    }
}
}  // namespace

// Public API wrapper that works with ReShade swapchain
void EnqueueGPUCompletion(reshade::api::swapchain* swapchain, IDXGISwapChain* dxgi_swapchain,
                          reshade::api::command_queue* command_queue) {
    if (perf_measurement::IsSuppressionEnabled()
        && perf_measurement::IsMetricSuppressed(perf_measurement::Metric::EnqueueGPUCompletion)) {
        return;
    }

    perf_measurement::ScopedTimer perf_timer(perf_measurement::Metric::EnqueueGPUCompletion);

    if (swapchain == nullptr) {
        g_gpu_fence_failure_reason.store("Failed to get swapchain from swapchain, swapchain is nullptr");
        return;
    }

    // Get native D3D12 command queue if provided (for D3D12 fence signaling)
    ID3D12CommandQueue* d3d12_command_queue = nullptr;
    if (command_queue != nullptr && swapchain->get_device()->get_api() == reshade::api::device_api::d3d12) {
        d3d12_command_queue = reinterpret_cast<ID3D12CommandQueue*>(command_queue->get_native());
    }

    // Call the internal enqueue function
    ::EnqueueGPUCompletionInternal(dxgi_swapchain, d3d12_command_queue);
}

void EnqueueGPUCompletionFromRecordedState(IDXGISwapChain* dxgi_swapchain,
                                           const display_commanderhooks::dxgi::DCDxgiSwapchainData* data) {
    if (perf_measurement::IsSuppressionEnabled()
        && perf_measurement::IsMetricSuppressed(perf_measurement::Metric::EnqueueGPUCompletion)) {
        return;
    }
    perf_measurement::ScopedTimer perf_timer(perf_measurement::Metric::EnqueueGPUCompletion);
    if (dxgi_swapchain == nullptr || data == nullptr || data->dxgi_swapchain == nullptr) {
        return;
    }
    ID3D12CommandQueue* d3d12_queue = nullptr;
    if (data->device_api == reshade::api::device_api::d3d12 && data->command_queue != nullptr) {
        d3d12_queue = reinterpret_cast<ID3D12CommandQueue*>(data->command_queue->get_native());
    }
    ::EnqueueGPUCompletionInternal(data->dxgi_swapchain, d3d12_queue);
}

namespace display_commanderhooks::dxgi {

// Log DXGI error up to 10 times per method (each detour has its own static counter).
inline void LogDxgiErrorUpTo10(const char* method, HRESULT hr, int* pCount) {
    if (SUCCEEDED(hr) || pCount == nullptr || *pCount >= 10) return;
    LogError("[DXGI error] %s returned 0x%08X", method, static_cast<unsigned>(hr));
    (*pCount)++;
}

// Original function pointers (methods we actually detour)
IDXGISwapChain_Present_pfn IDXGISwapChain_Present_Original = nullptr;
IDXGISwapChain_Present1_pfn IDXGISwapChain_Present1_Original = nullptr;

IDXGISwapChain_CheckColorSpaceSupport_pfn IDXGISwapChain_CheckColorSpaceSupport_Original = nullptr;

IDXGISwapChain_SetFullscreenState_pfn IDXGISwapChain_SetFullscreenState_Original = nullptr;
IDXGISwapChain_GetFullscreenState_pfn IDXGISwapChain_GetFullscreenState_Original = nullptr;
IDXGISwapChain_ResizeBuffers_pfn IDXGISwapChain_ResizeBuffers_Original = nullptr;
IDXGISwapChain_ResizeTarget_pfn IDXGISwapChain_ResizeTarget_Original = nullptr;
IDXGISwapChain_ResizeBuffers1_pfn IDXGISwapChain_ResizeBuffers1_Original = nullptr;

// Helper function for common Present/Present1 logic after calling original
void HandlePresentAfter(bool frame_generation_aware) {
    CALL_GUARD_NO_TS();
    if (perf_measurement::IsSuppressionEnabled()
        && perf_measurement::IsMetricSuppressed(perf_measurement::Metric::HandlePresentAfter)) {
        return;
    }

    perf_measurement::ScopedTimer perf_timer(perf_measurement::Metric::HandlePresentAfter);

    // Get device from swapchain for latency manager
    ::OnPresentUpdateAfter2(frame_generation_aware);
}

static std::atomic<int> in_present_call(0);

// VSync override: combo index 0=No override(-1), 1=Force ON(1), 2=1/2(2), 3=1/3(3), 4=1/4(4), 5=FORCED OFF(0). Returns
// -1 if no override.
static int VsyncOverrideComboIndexToApiValue(int combo_index) {
    static const int kMap[] = {-1, 1, 2, 3, 4, 0};
    if (combo_index < 0 || combo_index > 5) return -1;
    return kMap[combo_index];
}

// Hooked IDXGISwapChain::Present function
HRESULT STDMETHODCALLTYPE IDXGISwapChain_Present_Detour(IDXGISwapChain* This, UINT SyncInterval, UINT PresentFlags) {
    display_commanderhooks::dxgi::DCDxgiSwapchainData data{};
    if (This != nullptr) {
        display_commanderhooks::dxgi::LoadDCDxgiSwapchainData(This, &data);
    }

    if (in_present_call.load() > 0) {
        return IDXGISwapChain_Present_Original(This, SyncInterval, PresentFlags);
    }
    // Apply VSync override (Main tab): -1 = no override, 0-4 = force SyncInterval
    const int override_val = VsyncOverrideComboIndexToApiValue(settings::g_mainTabSettings.vsync_override.GetValue());
    const UINT effective_interval = (override_val >= 0) ? static_cast<UINT>(override_val) : SyncInterval;
    if (override_val >= 1) {
        PresentFlags &= ~DXGI_PRESENT_ALLOW_TEARING;
    } else if (override_val == 0) {
        PresentFlags |= DXGI_PRESENT_ALLOW_TEARING;
    }
    const LONGLONG now_ns = utils::get_now_ns();
    display_commanderhooks::g_last_dxgi_present_time_ns.store(static_cast<uint64_t>(now_ns), std::memory_order_relaxed);
    CALL_GUARD(now_ns);

    // Flush command queue before present when we have it from this swapchain's private data (optional, default on)
    if (settings::g_advancedTabSettings.flush_command_queue_before_sleep.GetValue()) {
        if (data.command_queue != nullptr) {
            data.command_queue->flush_immediate_command_list();
        }
    }

    ChooseFpsLimiter(static_cast<uint64_t>(utils::get_now_ns()), FpsLimiterCallSite::dxgi_swapchain);
    bool use_fps_limiter = GetChosenFpsLimiter(FpsLimiterCallSite::dxgi_swapchain);
    // Skip common present logic if wrapper is handling it
    if (use_fps_limiter) {
        ::OnPresentFlags2(true, false);  // Called from present_detour
        RecordNativeFrameTime();
    }
    if (GetChosenFrameTimeLocation() == FpsLimiterCallSite::dxgi_swapchain) {
        RecordFrameTime(FrameTimeMode::kPresent);
    }

    if (IDXGISwapChain_Present_Original == nullptr) {
        LogError("IDXGISwapChain_Present_Detour: IDXGISwapChain_Present_Original is null");
        return This->Present(effective_interval, PresentFlags);
    }

    auto res = IDXGISwapChain_Present_Original(This, effective_interval, PresentFlags);
    {
        static int s_err_count = 0;
        LogDxgiErrorUpTo10("IDXGISwapChain::Present", res, &s_err_count);
    }
    if (use_fps_limiter) {
        // Handle common after logic
        HandlePresentAfter(false);
    }
    if (settings::g_advancedTabSettings.enable_dxgi_refresh_rate_vrr_detection.GetValue()) {
        IDXGISwapChain* sc_for_monitor = (data.dxgi_swapchain != nullptr) ? data.dxgi_swapchain : This;
        ::dxgi::fps_limiter::SignalRefreshRateMonitor(sc_for_monitor);
    }
    CALL_GUARD_NO_TS();

    return res;
}

// Hooked IDXGISwapChain1::Present1 function
HRESULT STDMETHODCALLTYPE IDXGISwapChain_Present1_Detour(IDXGISwapChain1* This, UINT SyncInterval, UINT PresentFlags,
                                                         const DXGI_PRESENT_PARAMETERS* pPresentParameters) {
    IDXGISwapChain* baseSwapChain = reinterpret_cast<IDXGISwapChain*>(This);
    display_commanderhooks::dxgi::DCDxgiSwapchainData data{};
    if (baseSwapChain != nullptr) {
        display_commanderhooks::dxgi::LoadDCDxgiSwapchainData(baseSwapChain, &data);
    }

    CALL_GUARD_NO_TS();

    // Apply VSync override (Main tab): -1 = no override, 0-4 = force SyncInterval
    const int override_val = VsyncOverrideComboIndexToApiValue(settings::g_mainTabSettings.vsync_override.GetValue());
    const UINT effective_interval = (override_val >= 0) ? static_cast<UINT>(override_val) : SyncInterval;

    if (override_val >= 1) {
        PresentFlags &= ~DXGI_PRESENT_ALLOW_TEARING;
    } else if (override_val == 0) {
        PresentFlags |= DXGI_PRESENT_ALLOW_TEARING;
    }

    // Flush command queue before present when we have it from this swapchain's private data (optional, default on)
    if (settings::g_advancedTabSettings.flush_command_queue_before_sleep.GetValue()) {
        if (data.command_queue != nullptr) {
            data.command_queue->flush_immediate_command_list();
        }
    }

    ChooseFpsLimiter(static_cast<uint64_t>(utils::get_now_ns()), FpsLimiterCallSite::dxgi_swapchain1);
    bool use_fps_limiter = GetChosenFpsLimiter(FpsLimiterCallSite::dxgi_swapchain1);

    if (use_fps_limiter) {
        // Handle common before logic (with D3D10 check enabled)
        ::OnPresentFlags2(true, false);  // Called from present_detour
        RecordNativeFrameTime();
    }
    if (GetChosenFrameTimeLocation() == FpsLimiterCallSite::dxgi_swapchain1) {
        RecordFrameTime(FrameTimeMode::kPresent);
    }
    in_present_call.fetch_add(1);
    if (IDXGISwapChain_Present1_Original == nullptr) {
        LogError("IDXGISwapChain_Present1_Detour: IDXGISwapChain_Present1_Original is null");
        auto res = This->Present1(effective_interval, PresentFlags, pPresentParameters);

        in_present_call.fetch_sub(1);
        return res;
    }

    auto res = IDXGISwapChain_Present1_Original(This, effective_interval, PresentFlags, pPresentParameters);
    in_present_call.fetch_sub(1);
    {
        static int s_err_count = 0;
        LogDxgiErrorUpTo10("IDXGISwapChain1::Present1", res, &s_err_count);
    }
    if (use_fps_limiter) {
        // Handle common after logic
        HandlePresentAfter(false);
    }
    if (settings::g_advancedTabSettings.enable_dxgi_refresh_rate_vrr_detection.GetValue()) {
        IDXGISwapChain* sc_for_monitor = (data.dxgi_swapchain != nullptr) ? data.dxgi_swapchain : baseSwapChain;
        ::dxgi::fps_limiter::SignalRefreshRateMonitor(sc_for_monitor);
    }

    return res;
}

// Hooked IDXGISwapChain3::CheckColorSpaceSupport function
HRESULT STDMETHODCALLTYPE IDXGISwapChain_CheckColorSpaceSupport_Detour(IDXGISwapChain3* This,
                                                                       DXGI_COLOR_SPACE_TYPE ColorSpace,
                                                                       UINT* pColorSpaceSupport) {
    CALL_GUARD_NO_TS();

    if (IDXGISwapChain_CheckColorSpaceSupport_Original != nullptr) {
        HRESULT hr = IDXGISwapChain_CheckColorSpaceSupport_Original(This, ColorSpace, pColorSpaceSupport);
        {
            static int s_err_count = 0;
            LogDxgiErrorUpTo10("IDXGISwapChain3::CheckColorSpaceSupport", hr, &s_err_count);
        }

        // Hide HDR capabilities if enabled
        if (SUCCEEDED(hr) && pColorSpaceSupport != nullptr
            && settings::g_advancedTabSettings.hide_hdr_capabilities.GetValue()) {
            bool is_hdr_colorspace = false;
            switch (ColorSpace) {
                case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:
                case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:
                case DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020:
                case DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709:
                case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:
                    is_hdr_colorspace = true;
                    break;
                default: break;
            }

            if (is_hdr_colorspace) {
                *pColorSpaceSupport = 0;
            }
        }

        return hr;
    }

    return This->CheckColorSpaceSupport(ColorSpace, pColorSpaceSupport);
}

std::atomic<int> g_last_set_fullscreen_state{-1};  // -1 for not set, 0 for false, 1 for true
std::atomic<IDXGIOutput*> g_last_set_fullscreen_target{nullptr};
HRESULT STDMETHODCALLTYPE IDXGISwapChain_SetFullscreenState_Detour(IDXGISwapChain* This, BOOL Fullscreen,
                                                                   IDXGIOutput* pTarget) {
    CALL_GUARD_NO_TS();

    if (Fullscreen == g_last_set_fullscreen_state.load() && pTarget == g_last_set_fullscreen_target.load()) {
        return S_OK;
    }

    g_last_set_fullscreen_target.store(pTarget);
    g_last_set_fullscreen_state.store(Fullscreen);

    HRESULT hr;
    if (ShouldPreventExclusiveFullscreen()) {
        hr = IDXGISwapChain_SetFullscreenState_Original(This, false, pTarget);
    } else {
        hr = IDXGISwapChain_SetFullscreenState_Original(This, Fullscreen, pTarget);
    }
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGISwapChain::SetFullscreenState", hr, &s_err_count);
    return hr;
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetFullscreenState_Detour(IDXGISwapChain* This, BOOL* pFullscreen,
                                                                 IDXGIOutput** ppTarget) {
    CALL_GUARD_NO_TS();
    auto hr = IDXGISwapChain_GetFullscreenState_Original(This, pFullscreen, ppTarget);
    {
        static int s_err_count = 0;
        LogDxgiErrorUpTo10("IDXGISwapChain::GetFullscreenState", hr, &s_err_count);
    }

    if (ShouldPreventExclusiveFullscreen() && g_last_set_fullscreen_state.load() != -1 && pFullscreen != nullptr) {
        *pFullscreen = g_last_set_fullscreen_state.load();
    }

    return hr;
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_ResizeBuffers_Detour(IDXGISwapChain* This, UINT BufferCount, UINT Width,
                                                                UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    CALL_GUARD_NO_TS();

    if (Width != 0 && Height != 0) {
        g_game_render_width.store(Width);
        g_game_render_height.store(Height);
        LogInfo("IDXGISwapChain_ResizeBuffers_Detour - Game render resolution: %ux%u", Width, Height);
    }

    HRESULT hr = IDXGISwapChain_ResizeBuffers_Original(This, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGISwapChain::ResizeBuffers", hr, &s_err_count);
    return hr;
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_ResizeTarget_Detour(IDXGISwapChain* This,
                                                             const DXGI_MODE_DESC* pNewTargetParameters) {
    CALL_GUARD_NO_TS();

    if (pNewTargetParameters != nullptr) {
        if (pNewTargetParameters->Width != 0 && pNewTargetParameters->Height != 0) {
            g_game_render_width.store(pNewTargetParameters->Width);
            g_game_render_height.store(pNewTargetParameters->Height);
            LogInfo("IDXGISwapChain_ResizeTarget_Detour - Game render resolution: %ux%u", pNewTargetParameters->Width,
                    pNewTargetParameters->Height);
        }
    }

    HRESULT hr = IDXGISwapChain_ResizeTarget_Original(This, pNewTargetParameters);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGISwapChain::ResizeTarget", hr, &s_err_count);
    return hr;
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_ResizeBuffers1_Detour(IDXGISwapChain3* This, UINT BufferCount, UINT Width,
                                                              UINT Height, DXGI_FORMAT Format, UINT SwapChainFlags,
                                                              const UINT* pCreationNodeMask,
                                                              IUnknown* const* ppPresentQueue) {
    CALL_GUARD_NO_TS();

    if (Width != 0 && Height != 0) {
        g_game_render_width.store(Width);
        g_game_render_height.store(Height);
        LogInfo("IDXGISwapChain_ResizeBuffers1_Detour - Game render resolution: %ux%u", Width, Height);
    }

    HRESULT hr = IDXGISwapChain_ResizeBuffers1_Original(This, BufferCount, Width, Height, Format, SwapChainFlags,
                                                      pCreationNodeMask, ppPresentQueue);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGISwapChain3::ResizeBuffers1", hr, &s_err_count);
    return hr;
}

namespace {
IDXGISwapChain* g_hooked_swapchain = nullptr;
}  // namespace

// Hook a specific swapchain's vtable — only Present/Present1, fullscreen, resize, CheckColorSpaceSupport (HDR hide).
bool HookSwapchain(IDXGISwapChain* swapchain) {
    if (g_dx9_swapchain_detected.load()) {
        return false;
    }
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::DXGI_SWAPCHAIN)) {
        LogInfo("[HookSwapchain] installation suppressed by user setting");
        return false;
    }

    if (g_swapchainTrackingManager.IsSwapchainTracked(swapchain)) {
        return false;
    }
    LogInfo("[HookSwapchain] hooking swapchain: 0x%p", swapchain);
    static bool installed = false;
    if (installed) {
        LogInfo("[HookSwapchain] IDXGISwapChain hooks already installed");
        return true;
    }
    installed = true;

    g_hooked_swapchain = swapchain;
    g_swapchainTrackingManager.AddSwapchain(swapchain);

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain1;
    Microsoft::WRL::ComPtr<IDXGISwapChain2> swapchain2;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain3;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapchain4;
    auto vtable_version = 0;

    void** vtable = nullptr;

    if (vtable == nullptr) {
        HRESULT hr = swapchain->QueryInterface(IID_PPV_ARGS(&swapchain4));
        if (SUCCEEDED(hr)) {
            vtable_version = 4;
            vtable = *(void***)swapchain4.Get();
        } else {
            LogError(
                "[HookSwapchain] Failed to query IDXGISwapChain4 interface (HRESULT: 0x%08X). Swapchain hooking "
                "aborted.",
                hr);
        }
    }
    if (vtable == nullptr) {
        HRESULT hr = swapchain->QueryInterface(IID_PPV_ARGS(&swapchain3));
        if (SUCCEEDED(hr)) {
            vtable_version = 3;
            vtable = *(void***)swapchain3.Get();
        } else {
            LogError(
                "[HookSwapchain] Failed to query IDXGISwapChain3 interface (HRESULT: 0x%08X). Swapchain hooking "
                "aborted.",
                hr);
        }
    }

    if (vtable == nullptr) {
        HRESULT hr = swapchain->QueryInterface(IID_PPV_ARGS(&swapchain2));
        if (SUCCEEDED(hr)) {
            vtable_version = 2;
            vtable = *(void***)swapchain2.Get();
        } else {
            LogError(
                "[HookSwapchain] Failed to query IDXGISwapChain2 interface (HRESULT: 0x%08X). Swapchain hooking "
                "aborted.",
                hr);
        }
    }

    if (vtable == nullptr) {
        HRESULT hr = swapchain->QueryInterface(IID_PPV_ARGS(&swapchain1));
        if (SUCCEEDED(hr)) {
            vtable_version = 1;
            vtable = *(void***)swapchain1.Get();
        } else {
            LogError(
                "[HookSwapchain] Failed to query IDXGISwapChain1 interface (HRESULT: 0x%08X). Swapchain hooking "
                "aborted.",
                hr);
        }
    }

    if (vtable == nullptr) {
        vtable_version = 0;
        vtable = *(void***)swapchain;
    }
    LogInfo("[HookSwapchain] Hooking swapchain vtable version: %d", vtable_version);

    MH_STATUS init_status = SafeInitializeMinHook(display_commanderhooks::HookType::DXGI_SWAPCHAIN);
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("[HookSwapchain] Failed to initialize MinHook for DXGI hooks - Status: %d", init_status);
        return false;
    }
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
        display_commanderhooks::HookType::DXGI_SWAPCHAIN);

    LogInfo("[HookSwapchain] Installing IDXGISwapChain detours (Present, fullscreen, resize; Present1/SC3 if available)");

    // Indices 8 Present, 10–11 fullscreen, 13–14 ResizeBuffers/ResizeTarget
    if (!CreateAndEnableHook(vtable[8], IDXGISwapChain_Present_Detour, (LPVOID*)&IDXGISwapChain_Present_Original,
                             "IDXGISwapChain::Present")) {
        LogError("[HookSwapchain] Failed to create and enable IDXGISwapChain::Present hook");
        return false;
    }

    if (!CreateAndEnableHook(vtable[10], IDXGISwapChain_SetFullscreenState_Detour,
                             (LPVOID*)&IDXGISwapChain_SetFullscreenState_Original, "IDXGISwapChain::SetFullscreenState")) {
        LogError("[HookSwapchain] Failed to create and enable IDXGISwapChain::SetFullscreenState hook");
    }
    if (!CreateAndEnableHook(vtable[11], IDXGISwapChain_GetFullscreenState_Detour,
                             (LPVOID*)&IDXGISwapChain_GetFullscreenState_Original, "IDXGISwapChain::GetFullscreenState")) {
        LogError("[HookSwapchain] Failed to create and enable IDXGISwapChain::GetFullscreenState hook");
    }
    if (!CreateAndEnableHook(vtable[13], IDXGISwapChain_ResizeBuffers_Detour,
                             (LPVOID*)&IDXGISwapChain_ResizeBuffers_Original, "IDXGISwapChain::ResizeBuffers")) {
        LogError("[HookSwapchain] Failed to create and enable IDXGISwapChain::ResizeBuffers hook");
    }
    if (!CreateAndEnableHook(vtable[14], IDXGISwapChain_ResizeTarget_Detour,
                             (LPVOID*)&IDXGISwapChain_ResizeTarget_Original, "IDXGISwapChain::ResizeTarget")) {
        LogError("[HookSwapchain] Failed to create and enable IDXGISwapChain::ResizeTarget hook");
    }

    if (vtable_version >= 1) {
        if (!CreateAndEnableHook(vtable[22], IDXGISwapChain_Present1_Detour, (LPVOID*)&IDXGISwapChain_Present1_Original,
                                 "IDXGISwapChain1::Present1")) {
            LogError("[HookSwapchain] Failed to create and enable IDXGISwapChain1::Present1 hook");
        }
    }

    if (vtable_version >= 3) {
        if (!CreateAndEnableHook(vtable[37], IDXGISwapChain_CheckColorSpaceSupport_Detour,
                                 (LPVOID*)&IDXGISwapChain_CheckColorSpaceSupport_Original,
                                 "IDXGISwapChain3::CheckColorSpaceSupport")) {
            LogError("[HookSwapchain] Failed to create and enable IDXGISwapChain3::CheckColorSpaceSupport hook");
        }
        if (!CreateAndEnableHook(vtable[39], IDXGISwapChain_ResizeBuffers1_Detour,
                                 (LPVOID*)&IDXGISwapChain_ResizeBuffers1_Original, "IDXGISwapChain3::ResizeBuffers1")) {
            LogError("[HookSwapchain] Failed to create and enable IDXGISwapChain3::ResizeBuffers1 hook");
        }
    }

    LogInfo("Successfully hooked IDXGISwapChain for swapchain: %x%p", swapchain);

    return true;
}
// GUID for Display Commander per-swapchain data (DCDxgiSwapchainData blob). Do not conflict with ReShade SKID.
constexpr GUID kDcDxgiSwapchainData = {0xdc7b2f81, 0xc4d5, 0x4e0f, {0x9b, 0x3e, 0x2d, 0x5f, 0x6c, 0x7e, 0x8f, 0x9a}};

bool LoadDCDxgiSwapchainData(IDXGISwapChain* swapchain, DCDxgiSwapchainData* out) {
    if (swapchain == nullptr || out == nullptr) return false;
    *out = DCDxgiSwapchainData{};
    UINT size = sizeof(DCDxgiSwapchainData);
    HRESULT hr = swapchain->GetPrivateData(kDcDxgiSwapchainData, &size, out);
    return SUCCEEDED(hr) && size == sizeof(DCDxgiSwapchainData);
}

void SaveDCDxgiSwapchainData(IDXGISwapChain* swapchain, const DCDxgiSwapchainData* data) {
    if (swapchain == nullptr || data == nullptr) return;
    swapchain->SetPrivateData(kDcDxgiSwapchainData, sizeof(DCDxgiSwapchainData), data);
}

std::atomic<std::shared_ptr<DCDxgiSwapchainData>> g_last_d3d9_dc_swapchain_data{nullptr};

void SaveDCDxgiSwapchainDataForD3D9(const DCDxgiSwapchainData* data) {
    if (data == nullptr) return;
    g_last_d3d9_dc_swapchain_data.store(std::make_shared<DCDxgiSwapchainData>(*data),
                                        std::memory_order_release);
}

// Cleanup GPU measurement fences when device is destroyed
void CleanupGPUMeasurementFences() {
    LogInfo("[CleanupGPUMeasurementFences] cleaning up GPU measurement fences");
    ::CleanupGPUMeasurementState();
}

}  // namespace display_commanderhooks::dxgi
