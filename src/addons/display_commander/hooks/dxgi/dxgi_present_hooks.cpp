#include "dxgi_present_hooks.hpp"
#include "../../autoclick/autoclick_manager.hpp"
#include "../../globals.hpp"
#include "../../latent_sync/refresh_rate_monitor_integration.hpp"
#include "../../performance_types.hpp"
#include "../../settings/advanced_tab_settings.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../swapchain_events.hpp"
#include "../../ui/new_ui/new_ui_tabs.hpp"
#include "../../utils/detour_call_tracker.hpp"
#include "../../utils/dxgi_color_space.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/perf_measurement.hpp"
#include "../../utils/timing.hpp"
#include "dxgi_factory_wrapper.hpp"
#include "../hook_suppression_manager.hpp"
#include "../present_traffic_tracking.hpp"
#include "dxgi_gpu_completion.hpp"
#include "utils/logging.hpp"

#include <dwmapi.h>
#include <MinHook.h>

#include <d3d11_4.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cmath>
#include <memory>
#include <string>

// Forward declaration for g_sim_start_ns from swapchain_events.cpp
extern std::atomic<LONGLONG> g_sim_start_ns;

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

// Original function pointers
IDXGISwapChain_Present_pfn IDXGISwapChain_Present_Original = nullptr;
IDXGISwapChain_Present1_pfn IDXGISwapChain_Present1_Original = nullptr;

// Streamline proxy swap chain (only Present/Present1 hooked for FPS limiter)
IDXGISwapChain_Present_pfn IDXGISwapChain_Present_Streamline_Original = nullptr;
IDXGISwapChain_Present1_pfn IDXGISwapChain_Present1_Streamline_Original = nullptr;

IDXGISwapChain_GetDesc_pfn IDXGISwapChain_GetDesc_Original = nullptr;
IDXGISwapChain_GetDesc1_pfn IDXGISwapChain_GetDesc1_Original = nullptr;
IDXGISwapChain_CheckColorSpaceSupport_pfn IDXGISwapChain_CheckColorSpaceSupport_Original = nullptr;
IDXGIFactory_CreateSwapChain_pfn IDXGIFactory_CreateSwapChain_Original = nullptr;
IDXGIFactory1_CreateSwapChainForHwnd_pfn IDXGIFactory1_CreateSwapChainForHwnd_Original = nullptr;
IDXGIFactory1_CreateSwapChainForCoreWindow_pfn IDXGIFactory1_CreateSwapChainForCoreWindow_Original = nullptr;
IDXGIFactory2_CreateSwapChainForComposition_pfn IDXGIFactory2_CreateSwapChainForComposition_Original = nullptr;

// Streamline proxy factory vtable originals
IDXGIFactory_CreateSwapChain_pfn IDXGIFactory_CreateSwapChain_Streamline_Original = nullptr;
IDXGIFactory1_CreateSwapChainForHwnd_pfn IDXGIFactory1_CreateSwapChainForHwnd_Streamline_Original = nullptr;
IDXGIFactory1_CreateSwapChainForCoreWindow_pfn IDXGIFactory1_CreateSwapChainForCoreWindow_Streamline_Original = nullptr;
IDXGIFactory2_CreateSwapChainForComposition_pfn IDXGIFactory2_CreateSwapChainForComposition_Streamline_Original =
    nullptr;

// Additional original function pointers
IDXGISwapChain_GetBuffer_pfn IDXGISwapChain_GetBuffer_Original = nullptr;
IDXGISwapChain_SetFullscreenState_pfn IDXGISwapChain_SetFullscreenState_Original = nullptr;
IDXGISwapChain_GetFullscreenState_pfn IDXGISwapChain_GetFullscreenState_Original = nullptr;
IDXGISwapChain_ResizeBuffers_pfn IDXGISwapChain_ResizeBuffers_Original = nullptr;
IDXGISwapChain_ResizeTarget_pfn IDXGISwapChain_ResizeTarget_Original = nullptr;
IDXGISwapChain_GetContainingOutput_pfn IDXGISwapChain_GetContainingOutput_Original = nullptr;
IDXGISwapChain_GetFrameStatistics_pfn IDXGISwapChain_GetFrameStatistics_Original = nullptr;
IDXGISwapChain_GetLastPresentCount_pfn IDXGISwapChain_GetLastPresentCount_Original = nullptr;

// IDXGISwapChain1 original function pointers
IDXGISwapChain_GetFullscreenDesc_pfn IDXGISwapChain_GetFullscreenDesc_Original = nullptr;
IDXGISwapChain_GetHwnd_pfn IDXGISwapChain_GetHwnd_Original = nullptr;
IDXGISwapChain_GetCoreWindow_pfn IDXGISwapChain_GetCoreWindow_Original = nullptr;
IDXGISwapChain_IsTemporaryMonoSupported_pfn IDXGISwapChain_IsTemporaryMonoSupported_Original = nullptr;
IDXGISwapChain_GetRestrictToOutput_pfn IDXGISwapChain_GetRestrictToOutput_Original = nullptr;
IDXGISwapChain_SetBackgroundColor_pfn IDXGISwapChain_SetBackgroundColor_Original = nullptr;
IDXGISwapChain_GetBackgroundColor_pfn IDXGISwapChain_GetBackgroundColor_Original = nullptr;
IDXGISwapChain_SetRotation_pfn IDXGISwapChain_SetRotation_Original = nullptr;
IDXGISwapChain_GetRotation_pfn IDXGISwapChain_GetRotation_Original = nullptr;

// IDXGISwapChain2 original function pointers
IDXGISwapChain_SetSourceSize_pfn IDXGISwapChain_SetSourceSize_Original = nullptr;
IDXGISwapChain_GetSourceSize_pfn IDXGISwapChain_GetSourceSize_Original = nullptr;
IDXGISwapChain_SetMaximumFrameLatency_pfn IDXGISwapChain_SetMaximumFrameLatency_Original = nullptr;
IDXGISwapChain_GetMaximumFrameLatency_pfn IDXGISwapChain_GetMaximumFrameLatency_Original = nullptr;
IDXGISwapChain_GetFrameLatencyWaitableObject_pfn IDXGISwapChain_GetFrameLatencyWaitableObject_Original = nullptr;
IDXGISwapChain_SetMatrixTransform_pfn IDXGISwapChain_SetMatrixTransform_Original = nullptr;
IDXGISwapChain_GetMatrixTransform_pfn IDXGISwapChain_GetMatrixTransform_Original = nullptr;

// IDXGISwapChain3 original function pointers
IDXGISwapChain_GetCurrentBackBufferIndex_pfn IDXGISwapChain_GetCurrentBackBufferIndex_Original = nullptr;
IDXGISwapChain_SetColorSpace1_pfn IDXGISwapChain_SetColorSpace1_Original = nullptr;
IDXGISwapChain_ResizeBuffers1_pfn IDXGISwapChain_ResizeBuffers1_Original = nullptr;

// IDXGISwapChain4 original function pointers
IDXGISwapChain_SetHDRMetaData_pfn IDXGISwapChain_SetHDRMetaData_Original = nullptr;

// IDXGIOutput original function pointers
IDXGIOutput_SetGammaControl_pfn IDXGIOutput_SetGammaControl_Original = nullptr;
IDXGIOutput_GetGammaControl_pfn IDXGIOutput_GetGammaControl_Original = nullptr;
IDXGIOutput_GetDesc_pfn IDXGIOutput_GetDesc_Original = nullptr;

// IDXGIOutput6 original function pointers
IDXGIOutput6_GetDesc1_pfn IDXGIOutput6_GetDesc1_Original = nullptr;

// Hook state and swapchain tracking
namespace {
std::atomic<bool> g_dxgi_present_hooks_installed{false};
std::atomic<bool> g_createswapchain_vtable_hooked{false};

// Factory vtable hooks (idempotent per process - all IDXGIFactory1 share same vtable)
std::atomic<bool> g_factory_create_swapchain_hooked{false};
std::atomic<bool> g_factory_create_swapchain_for_hwnd_hooked{false};
std::atomic<bool> g_factory_create_swapchain_for_core_window_hooked{false};
std::atomic<bool> g_factory_create_swapchain_for_composition_hooked{false};

}  // namespace

// Helper function for common Present/Present1 logic after calling original
void HandlePresentAfter(bool frame_generation_aware) {
    CALL_GUARD(utils::get_now_ns());
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

    // Increment DXGI Present counter
    g_dxgi_core_event_counters[DXGI_CORE_EVENT_PRESENT].fetch_add(1);

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
    CALL_GUARD(utils::get_now_ns());

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

    CALL_GUARD(utils::get_now_ns());

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

    // Increment DXGI Present1 counter
    g_dxgi_sc1_event_counters[DXGI_SC1_EVENT_PRESENT1].fetch_add(1);

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

// Streamline proxy swap chain: Present detour (FPS limiter only when use_streamline_proxy_fps_limiter is on)
static HRESULT STDMETHODCALLTYPE IDXGISwapChain_Present_Streamline_Detour(IDXGISwapChain* This, UINT SyncInterval,
                                                                          UINT PresentFlags) {
    display_commanderhooks::dxgi::DCDxgiSwapchainData data{};
    if (This != nullptr) {
        display_commanderhooks::dxgi::LoadDCDxgiSwapchainData(This, &data);
    }
    if (in_present_call.load() > 0) {
        if (IDXGISwapChain_Present_Streamline_Original != nullptr)
            return IDXGISwapChain_Present_Streamline_Original(This, SyncInterval, PresentFlags);
        return This->Present(SyncInterval, PresentFlags);
    }
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
    g_dxgi_core_event_counters[DXGI_CORE_EVENT_PRESENT].fetch_add(1);
    bool use_fps_limiter = false;
    if (settings::g_mainTabSettings.use_streamline_proxy_fps_limiter.GetValue()) {
        ChooseFpsLimiter(static_cast<uint64_t>(utils::get_now_ns()),
                         FpsLimiterCallSite::dxgi_swapchain_streamline_proxy);
        use_fps_limiter = GetChosenFpsLimiter(FpsLimiterCallSite::dxgi_swapchain_streamline_proxy);
        if (use_fps_limiter) {
            ::OnPresentFlags2(true, true);
            RecordNativeFrameTime();
        }
        if (GetChosenFrameTimeLocation() == FpsLimiterCallSite::dxgi_swapchain_streamline_proxy) {
            RecordFrameTime(FrameTimeMode::kPresent);
        }
    }
    if (IDXGISwapChain_Present_Streamline_Original == nullptr) {
        return This->Present(effective_interval, PresentFlags);
    }
    auto res = IDXGISwapChain_Present_Streamline_Original(This, effective_interval, PresentFlags);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGISwapChain::Present (Streamline)", res, &s_err_count);
    if (use_fps_limiter) {
        HandlePresentAfter(true);
    }
    if (settings::g_advancedTabSettings.enable_dxgi_refresh_rate_vrr_detection.GetValue()) {
        IDXGISwapChain* sc_for_monitor = (data.dxgi_swapchain != nullptr) ? data.dxgi_swapchain : This;
        ::dxgi::fps_limiter::SignalRefreshRateMonitor(sc_for_monitor);
    }
    CALL_GUARD(utils::get_now_ns());
    return res;
}

// Streamline proxy swap chain: Present1 detour (FPS limiter only when use_streamline_proxy_fps_limiter is on)
static HRESULT STDMETHODCALLTYPE IDXGISwapChain_Present1_Streamline_Detour(
    IDXGISwapChain1* This, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS* pPresentParameters) {
    IDXGISwapChain* baseSwapChain = reinterpret_cast<IDXGISwapChain*>(This);
    display_commanderhooks::dxgi::DCDxgiSwapchainData data{};
    if (baseSwapChain != nullptr) {
        display_commanderhooks::dxgi::LoadDCDxgiSwapchainData(baseSwapChain, &data);
    }
    CALL_GUARD(utils::get_now_ns());
    const int override_val = VsyncOverrideComboIndexToApiValue(settings::g_mainTabSettings.vsync_override.GetValue());
    const UINT effective_interval = (override_val >= 0) ? static_cast<UINT>(override_val) : SyncInterval;
    if (override_val >= 1) {
        PresentFlags &= ~DXGI_PRESENT_ALLOW_TEARING;
    } else if (override_val == 0) {
        PresentFlags |= DXGI_PRESENT_ALLOW_TEARING;
    }
    g_dxgi_sc1_event_counters[DXGI_SC1_EVENT_PRESENT1].fetch_add(1);
    bool use_fps_limiter = false;
    if (settings::g_mainTabSettings.use_streamline_proxy_fps_limiter.GetValue()) {
        ChooseFpsLimiter(static_cast<uint64_t>(utils::get_now_ns()),
                         FpsLimiterCallSite::dxgi_swapchain1_streamline_proxy);
        use_fps_limiter = GetChosenFpsLimiter(FpsLimiterCallSite::dxgi_swapchain1_streamline_proxy);
        if (use_fps_limiter) {
            ::OnPresentFlags2(true, false);
            RecordNativeFrameTime();
        }
        if (GetChosenFrameTimeLocation() == FpsLimiterCallSite::dxgi_swapchain1_streamline_proxy) {
            RecordFrameTime(FrameTimeMode::kPresent);
        }
    }
    in_present_call.fetch_add(1);
    if (IDXGISwapChain_Present1_Streamline_Original == nullptr) {
        auto res = This->Present1(effective_interval, PresentFlags, pPresentParameters);
        in_present_call.fetch_sub(1);
        return res;
    }
    auto res = IDXGISwapChain_Present1_Streamline_Original(This, effective_interval, PresentFlags, pPresentParameters);
    in_present_call.fetch_sub(1);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGISwapChain1::Present1 (Streamline)", res, &s_err_count);
    if (use_fps_limiter) {
        HandlePresentAfter(false);
    }
    if (settings::g_advancedTabSettings.enable_dxgi_refresh_rate_vrr_detection.GetValue()) {
        IDXGISwapChain* sc_for_monitor = (data.dxgi_swapchain != nullptr) ? data.dxgi_swapchain : baseSwapChain;
        ::dxgi::fps_limiter::SignalRefreshRateMonitor(sc_for_monitor);
    }
    return res;
}

// Hooked IDXGISwapChain::GetDesc function
HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetDesc_Detour(IDXGISwapChain* This, DXGI_SWAP_CHAIN_DESC* pDesc) {
    CALL_GUARD(utils::get_now_ns());
    // Increment DXGI GetDesc counter
    g_dxgi_core_event_counters[DXGI_CORE_EVENT_GETDESC].fetch_add(1);

    // Call original function
    if (IDXGISwapChain_GetDesc_Original != nullptr) {
        HRESULT hr = IDXGISwapChain_GetDesc_Original(This, pDesc);
        {
            static int s_err_count = 0;
            LogDxgiErrorUpTo10("IDXGISwapChain::GetDesc", hr, &s_err_count);
        }
        /*
                // Hide HDR capabilities if enabled
                if (SUCCEEDED(hr) && pDesc != nullptr &&
           settings::g_advancedTabSettings.hide_hdr_capabilities.GetValue()) {
                    // Check if the format is HDR-capable and hide it
                    bool is_hdr_format = false;
                    switch (pDesc->BufferDesc.Format) {
                        case DXGI_FORMAT_R10G10B10A2_UNORM:     // 10-bit HDR (commonly used for HDR10)
                        case DXGI_FORMAT_R11G11B10_FLOAT:        // 11-bit HDR (HDR11)
                        case DXGI_FORMAT_R16G16B16A16_FLOAT:    // 16-bit HDR (HDR16)
                        case DXGI_FORMAT_R32G32B32A32_FLOAT:    // 32-bit HDR (HDR32)
                        case DXGI_FORMAT_R16G16B16A16_UNORM:    // 16-bit HDR (alternative)
                        case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:    // 9-bit HDR (shared exponent)
                            is_hdr_format = true;
                            break;
                        default:
                            break;
                    }

                    if (is_hdr_format) {
                        // Force to SDR format
                        pDesc->BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

                        static int hdr_format_hidden_log_count = 0;
                        if (hdr_format_hidden_log_count < 3) {
                            LogInfo("HDR hiding: GetDesc - hiding HDR format %d, forcing to R8G8B8A8_UNORM",
                                   static_cast<int>(pDesc->BufferDesc.Format));
                            hdr_format_hidden_log_count++;
                        }
                    }

                    // Remove HDR-related flags
                    if ((pDesc->Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0) {
                        // Keep tearing flag but remove any HDR-specific flags
                        pDesc->Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
                    } else {
                        pDesc->Flags = 0;
                    }
                }

                // Log the description if successful (only on first few calls to avoid spam)
                if (SUCCEEDED(hr) && pDesc != nullptr) {
                    static int getdesc_log_count = 0;
                    if (getdesc_log_count < 3) {
                        LogInfo("SwapChain Desc - Width: %u, Height: %u, Format: %u, RefreshRate: %u/%u, BufferCount:
           %u", pDesc->BufferDesc.Width, pDesc->BufferDesc.Height, pDesc->BufferDesc.Format,
                               pDesc->BufferDesc.RefreshRate.Numerator, pDesc->BufferDesc.RefreshRate.Denominator,
                               pDesc->BufferCount);
                        getdesc_log_count++;
                    }
                }
        */
        return hr;
    }

    // Fallback to direct call if hook failed
    return This->GetDesc(pDesc);
}

// Hooked IDXGISwapChain1::GetDesc1 function
HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetDesc1_Detour(IDXGISwapChain1* This, DXGI_SWAP_CHAIN_DESC1* pDesc) {
    CALL_GUARD(utils::get_now_ns());
    // Increment DXGI GetDesc1 counter
    g_dxgi_sc1_event_counters[DXGI_SC1_EVENT_GETDESC1].fetch_add(1);

    // Call original function
    if (IDXGISwapChain_GetDesc1_Original != nullptr) {
        HRESULT hr = IDXGISwapChain_GetDesc1_Original(This, pDesc);
        {
            static int s_err_count = 0;
            LogDxgiErrorUpTo10("IDXGISwapChain1::GetDesc1", hr, &s_err_count);
        }
        /*
                // Hide HDR capabilities if enabled
                if (SUCCEEDED(hr) && pDesc != nullptr &&
           settings::g_advancedTabSettings.hide_hdr_capabilities.GetValue()) {
                    // Check if the format is HDR-capable and hide it
                    bool is_hdr_format = false;
                    switch (pDesc->Format) {
                        case DXGI_FORMAT_R10G10B10A2_UNORM:     // 10-bit HDR (commonly used for HDR10)
                        case DXGI_FORMAT_R11G11B10_FLOAT:        // 11-bit HDR (HDR11)
                        case DXGI_FORMAT_R16G16B16A16_FLOAT:    // 16-bit HDR (HDR16)
                        case DXGI_FORMAT_R32G32B32A32_FLOAT:    // 32-bit HDR (HDR32)
                        case DXGI_FORMAT_R16G16B16A16_UNORM:    // 16-bit HDR (alternative)
                        case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:    // 9-bit HDR (shared exponent)
                            is_hdr_format = true;
                            break;
                        default:
                            break;
                    }

                    if (is_hdr_format) {
                        // Force to SDR format
                        pDesc->Format = DXGI_FORMAT_R8G8B8A8_UNORM;

                        static int hdr_format_hidden_log_count = 0;
                        if (hdr_format_hidden_log_count < 3) {
                            LogInfo("HDR hiding: GetDesc1 - hiding HDR format %d, forcing to R8G8B8A8_UNORM",
                                    static_cast<int>(pDesc->Format));
                            hdr_format_hidden_log_count++;
                        }
                    }

                    // Remove HDR-related flags
                    if ((pDesc->Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0) {
                        // Keep tearing flag but remove any HDR-specific flags
                        pDesc->Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
                    } else {
                        pDesc->Flags = 0;
                    }
                }

                // Log the description if successful (only on first few calls to avoid spam)
                if (SUCCEEDED(hr) && pDesc != nullptr) {
                    static int getdesc1_log_count = 0;
                    if (getdesc1_log_count < 3) {
                        LogInfo("SwapChain Desc1 - Width: %u, Height: %u, Format: %u, BufferCount: %u, SwapEffect: %u,
           Scaling: %u", pDesc->Width, pDesc->Height, pDesc->Format, pDesc->BufferCount, pDesc->SwapEffect,
           pDesc->Scaling); getdesc1_log_count++;
                    }
                }
        */
        return hr;
    }

    // Fallback to direct call if hook failed
    return This->GetDesc1(pDesc);
}

// Hooked IDXGISwapChain3::CheckColorSpaceSupport function
HRESULT STDMETHODCALLTYPE IDXGISwapChain_CheckColorSpaceSupport_Detour(IDXGISwapChain3* This,
                                                                       DXGI_COLOR_SPACE_TYPE ColorSpace,
                                                                       UINT* pColorSpaceSupport) {
    CALL_GUARD(utils::get_now_ns());
    // Increment DXGI CheckColorSpaceSupport counter
    g_dxgi_sc3_event_counters[DXGI_SC3_EVENT_CHECKCOLORSPACESUPPORT].fetch_add(1);

    // Log the color space check (only on first few calls to avoid spam)
    static int checkcolorspace_log_count = 0;
    if (checkcolorspace_log_count < 3) {
        LogInfo("CheckColorSpaceSupport called for ColorSpace: %d", static_cast<int>(ColorSpace));
        checkcolorspace_log_count++;
    }

    // Call original function
    if (IDXGISwapChain_CheckColorSpaceSupport_Original != nullptr) {
        HRESULT hr = IDXGISwapChain_CheckColorSpaceSupport_Original(This, ColorSpace, pColorSpaceSupport);
        {
            static int s_err_count = 0;
            LogDxgiErrorUpTo10("IDXGISwapChain3::CheckColorSpaceSupport", hr, &s_err_count);
        }

        // Hide HDR capabilities if enabled
        if (SUCCEEDED(hr) && pColorSpaceSupport != nullptr
            && settings::g_advancedTabSettings.hide_hdr_capabilities.GetValue()) {
            // Check if this is an HDR color space
            bool is_hdr_colorspace = false;
            switch (ColorSpace) {
                case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:       // HDR10
                case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:    // HDR10
                case DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020:  // HDR10
                case DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709:     // HDR10
                case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:       // HDR10
                    is_hdr_colorspace = true;
                    break;
                default: break;
            }

            if (is_hdr_colorspace) {
                // Hide HDR support by setting support to 0
                *pColorSpaceSupport = 0;

                static int hdr_hidden_log_count = 0;
                if (hdr_hidden_log_count < 3) {
                    LogInfo("HDR hiding: CheckColorSpaceSupport for HDR ColorSpace %d - hiding support",
                            static_cast<int>(ColorSpace));
                    hdr_hidden_log_count++;
                }
            }
        }

        // Log the result if successful
        if (SUCCEEDED(hr) && pColorSpaceSupport != nullptr) {
            static int checkcolorspace_result_log_count = 0;
            if (checkcolorspace_result_log_count < 3) {
                LogInfo("CheckColorSpaceSupport result: ColorSpace %d support = 0x%x", static_cast<int>(ColorSpace),
                        *pColorSpaceSupport);
                checkcolorspace_result_log_count++;
            }
        }

        return hr;
    }

    // Fallback to direct call if hook failed
    return This->CheckColorSpaceSupport(ColorSpace, pColorSpaceSupport);
}

// Hooked IDXGIFactory::CreateSwapChain function
HRESULT STDMETHODCALLTYPE IDXGIFactory_CreateSwapChain_Detour(IDXGIFactory* This, IUnknown* pDevice,
                                                              DXGI_SWAP_CHAIN_DESC* pDesc,
                                                              IDXGISwapChain** ppSwapChain) {
    CALL_GUARD(utils::get_now_ns());
    // Increment DXGI Factory CreateSwapChain counter
    g_dxgi_factory_event_counters[DXGI_FACTORY_EVENT_CREATESWAPCHAIN].fetch_add(1);

    // Log the swapchain creation parameters (only on first few calls to avoid spam)
    static int createswapchain_log_count = 0;
    if (createswapchain_log_count < 3 && pDesc != nullptr) {
        LogInfo(
            "IDXGIFactory::CreateSwapChain - Width: %u, Height: %u, Format: %u, BufferCount: %u, SwapEffect: %u, "
            "Windowed: %s",
            pDesc->BufferDesc.Width, pDesc->BufferDesc.Height, pDesc->BufferDesc.Format, pDesc->BufferCount,
            pDesc->SwapEffect, pDesc->Windowed ? "true" : "false");
        createswapchain_log_count++;
    }

    // Call original function
    if (IDXGIFactory_CreateSwapChain_Original != nullptr) {
        HRESULT hr = IDXGIFactory_CreateSwapChain_Original(This, pDevice, pDesc, ppSwapChain);
        {
            static int s_err_count = 0;
            LogDxgiErrorUpTo10("IDXGIFactory::CreateSwapChain", hr, &s_err_count);
        }

        // If successful, hook the newly created swapchain
        if (SUCCEEDED(hr) && ppSwapChain != nullptr && *ppSwapChain != nullptr) {
            LogInfo("IDXGIFactory::CreateSwapChain succeeded, hooking new swapchain: 0x%p", *ppSwapChain);
            // causes sekiro to crash
            HookSwapchain(*ppSwapChain);
        }

        return hr;
    }

    // Fallback to direct call if hook failed
    return This->CreateSwapChain(pDevice, pDesc, ppSwapChain);
}

// IDXGIFactory1::CreateSwapChainForHwnd (vtable index 14). Log errors and hook new swapchain on success.
HRESULT STDMETHODCALLTYPE IDXGIFactory1_CreateSwapChainForHwnd_Detour(
    IDXGIFactory1* This, IUnknown* pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1* pDesc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGIOutput* pRestrictToOutput,
    IDXGISwapChain1** ppSwapChain) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_factory_event_counters[DXGI_FACTORY_EVENT_CREATESWAPCHAIN].fetch_add(1);

    if (IDXGIFactory1_CreateSwapChainForHwnd_Original == nullptr) {
        Microsoft::WRL::ComPtr<IDXGIFactory2> factory2;
        if (SUCCEEDED(This->QueryInterface(IID_PPV_ARGS(&factory2))))
            return factory2->CreateSwapChainForHwnd(pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput,
                                                    ppSwapChain);
        return E_NOINTERFACE;
    }
    HRESULT hr = IDXGIFactory1_CreateSwapChainForHwnd_Original(This, pDevice, hWnd, pDesc, pFullscreenDesc,
                                                               pRestrictToOutput, ppSwapChain);
    {
        static int s_err_count = 0;
        LogDxgiErrorUpTo10("IDXGIFactory1::CreateSwapChainForHwnd", hr, &s_err_count);
    }
    if (SUCCEEDED(hr) && ppSwapChain != nullptr && *ppSwapChain != nullptr) {
        LogInfo("IDXGIFactory1::CreateSwapChainForHwnd succeeded, hooking new swapchain: 0x%p", *ppSwapChain);
        HookSwapchain(*ppSwapChain);
    }
    return hr;
}

// IDXGIFactory1::CreateSwapChainForCoreWindow (vtable index 15). Log errors and hook new swapchain on success.
HRESULT STDMETHODCALLTYPE IDXGIFactory1_CreateSwapChainForCoreWindow_Detour(IDXGIFactory1* This, IUnknown* pDevice,
                                                                            IUnknown* pWindow,
                                                                            const DXGI_SWAP_CHAIN_DESC1* pDesc,
                                                                            IDXGIOutput* pRestrictToOutput,
                                                                            IDXGISwapChain1** ppSwapChain) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_factory_event_counters[DXGI_FACTORY_EVENT_CREATESWAPCHAIN].fetch_add(1);

    if (IDXGIFactory1_CreateSwapChainForCoreWindow_Original == nullptr) {
        Microsoft::WRL::ComPtr<IDXGIFactory2> factory2;
        if (SUCCEEDED(This->QueryInterface(IID_PPV_ARGS(&factory2))))
            return factory2->CreateSwapChainForCoreWindow(pDevice, pWindow, pDesc, pRestrictToOutput, ppSwapChain);
        return E_NOINTERFACE;
    }
    HRESULT hr = IDXGIFactory1_CreateSwapChainForCoreWindow_Original(This, pDevice, pWindow, pDesc, pRestrictToOutput,
                                                                     ppSwapChain);
    {
        static int s_err_count = 0;
        LogDxgiErrorUpTo10("IDXGIFactory1::CreateSwapChainForCoreWindow", hr, &s_err_count);
    }
    if (SUCCEEDED(hr) && ppSwapChain != nullptr && *ppSwapChain != nullptr) {
        LogInfo("IDXGIFactory1::CreateSwapChainForCoreWindow succeeded, hooking new swapchain: 0x%p", *ppSwapChain);
        HookSwapchain(*ppSwapChain);
    }
    return hr;
}

// IDXGIFactory2::CreateSwapChainForComposition (vtable index 24). Log errors and hook new swapchain on success.
HRESULT STDMETHODCALLTYPE IDXGIFactory2_CreateSwapChainForComposition_Detour(IDXGIFactory2* This, IUnknown* pDevice,
                                                                             const DXGI_SWAP_CHAIN_DESC1* pDesc,
                                                                             IDXGIOutput* pRestrictToOutput,
                                                                             IDXGISwapChain1** ppSwapChain) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_factory_event_counters[DXGI_FACTORY_EVENT_CREATESWAPCHAIN].fetch_add(1);

    if (IDXGIFactory2_CreateSwapChainForComposition_Original == nullptr) {
        return E_NOINTERFACE;
    }
    HRESULT hr =
        IDXGIFactory2_CreateSwapChainForComposition_Original(This, pDevice, pDesc, pRestrictToOutput, ppSwapChain);
    {
        static int s_err_count = 0;
        LogDxgiErrorUpTo10("IDXGIFactory2::CreateSwapChainForComposition", hr, &s_err_count);
    }
    if (SUCCEEDED(hr) && ppSwapChain != nullptr && *ppSwapChain != nullptr) {
        LogInfo("IDXGIFactory2::CreateSwapChainForComposition succeeded, hooking new swapchain: 0x%p", *ppSwapChain);
        HookSwapchain(*ppSwapChain);
    }
    return hr;
}

// Streamline proxy factory detours (same behavior as above but call Streamline originals)
static HRESULT STDMETHODCALLTYPE IDXGIFactory_CreateSwapChain_Streamline_Detour(IDXGIFactory* This, IUnknown* pDevice,
                                                                                DXGI_SWAP_CHAIN_DESC* pDesc,
                                                                                IDXGISwapChain** ppSwapChain) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_factory_event_counters[DXGI_FACTORY_EVENT_CREATESWAPCHAIN].fetch_add(1);
    if (IDXGIFactory_CreateSwapChain_Streamline_Original == nullptr) {
        return This->CreateSwapChain(pDevice, pDesc, ppSwapChain);
    }
    HRESULT hr = IDXGIFactory_CreateSwapChain_Streamline_Original(This, pDevice, pDesc, ppSwapChain);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGIFactory::CreateSwapChain (Streamline)", hr, &s_err_count);
    if (SUCCEEDED(hr) && ppSwapChain != nullptr && *ppSwapChain != nullptr) {
        LogInfo("IDXGIFactory::CreateSwapChain (Streamline) succeeded, hooking proxy swapchain Present/Present1: 0x%p",
                *ppSwapChain);
        HookStreamlineProxySwapchain(*ppSwapChain);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE IDXGIFactory1_CreateSwapChainForHwnd_Streamline_Detour(
    IDXGIFactory1* This, IUnknown* pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1* pDesc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGIOutput* pRestrictToOutput,
    IDXGISwapChain1** ppSwapChain) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_factory_event_counters[DXGI_FACTORY_EVENT_CREATESWAPCHAIN].fetch_add(1);
    if (IDXGIFactory1_CreateSwapChainForHwnd_Streamline_Original == nullptr) {
        Microsoft::WRL::ComPtr<IDXGIFactory2> factory2;
        if (SUCCEEDED(This->QueryInterface(IID_PPV_ARGS(&factory2))))
            return factory2->CreateSwapChainForHwnd(pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput,
                                                    ppSwapChain);
        return E_NOINTERFACE;
    }
    // Upgrade to HDR10: match hdr_upgrade (format + FLIP_DISCARD + ALLOW_TEARING + BufferCount >= 2) when needed
    HRESULT hr = IDXGIFactory1_CreateSwapChainForHwnd_Streamline_Original(This, pDevice, hWnd, pDesc, pFullscreenDesc,
                                                                          pRestrictToOutput, ppSwapChain);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGIFactory1::CreateSwapChainForHwnd (Streamline)", hr, &s_err_count);
    if (SUCCEEDED(hr) && ppSwapChain != nullptr && *ppSwapChain != nullptr) {
        LogInfo(
            "IDXGIFactory1::CreateSwapChainForHwnd (Streamline) succeeded, hooking proxy swapchain Present/Present1: "
            "0x%p",
            *ppSwapChain);
        HookStreamlineProxySwapchain(*ppSwapChain);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE IDXGIFactory1_CreateSwapChainForCoreWindow_Streamline_Detour(
    IDXGIFactory1* This, IUnknown* pDevice, IUnknown* pWindow, const DXGI_SWAP_CHAIN_DESC1* pDesc,
    IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_factory_event_counters[DXGI_FACTORY_EVENT_CREATESWAPCHAIN].fetch_add(1);
    if (IDXGIFactory1_CreateSwapChainForCoreWindow_Streamline_Original == nullptr) {
        Microsoft::WRL::ComPtr<IDXGIFactory2> factory2;
        if (SUCCEEDED(This->QueryInterface(IID_PPV_ARGS(&factory2))))
            return factory2->CreateSwapChainForCoreWindow(pDevice, pWindow, pDesc, pRestrictToOutput, ppSwapChain);
        return E_NOINTERFACE;
    }
    HRESULT hr = IDXGIFactory1_CreateSwapChainForCoreWindow_Streamline_Original(This, pDevice, pWindow, pDesc,
                                                                                pRestrictToOutput, ppSwapChain);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGIFactory1::CreateSwapChainForCoreWindow (Streamline)", hr, &s_err_count);
    if (SUCCEEDED(hr) && ppSwapChain != nullptr && *ppSwapChain != nullptr) {
        LogInfo(
            "IDXGIFactory1::CreateSwapChainForCoreWindow (Streamline) succeeded, hooking proxy swapchain "
            "Present/Present1: 0x%p",
            *ppSwapChain);
        HookStreamlineProxySwapchain(*ppSwapChain);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE IDXGIFactory2_CreateSwapChainForComposition_Streamline_Detour(
    IDXGIFactory2* This, IUnknown* pDevice, const DXGI_SWAP_CHAIN_DESC1* pDesc, IDXGIOutput* pRestrictToOutput,
    IDXGISwapChain1** ppSwapChain) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_factory_event_counters[DXGI_FACTORY_EVENT_CREATESWAPCHAIN].fetch_add(1);
    if (IDXGIFactory2_CreateSwapChainForComposition_Streamline_Original == nullptr) {
        return E_NOINTERFACE;
    }
    HRESULT hr = IDXGIFactory2_CreateSwapChainForComposition_Streamline_Original(This, pDevice, pDesc,
                                                                                 pRestrictToOutput, ppSwapChain);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGIFactory2::CreateSwapChainForComposition (Streamline)", hr, &s_err_count);
    if (SUCCEEDED(hr) && ppSwapChain != nullptr && *ppSwapChain != nullptr) {
        LogInfo(
            "IDXGIFactory2::CreateSwapChainForComposition (Streamline) succeeded, hooking proxy swapchain "
            "Present/Present1: 0x%p",
            *ppSwapChain);
        HookStreamlineProxySwapchain(*ppSwapChain);
    }
    return hr;
}

bool HookFactory(IUnknown* iunknown) {
    if (iunknown == nullptr) {
        return false;
    }
    if (g_dx9_swapchain_detected.load()) {
        return false;
    }
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::DXGI_SWAPCHAIN)) {
        return false;
    }
    Microsoft::WRL::ComPtr<IDXGIFactory> ifactory;
    HRESULT hr = iunknown->QueryInterface(IID_PPV_ARGS(ifactory.GetAddressOf()));
    if (FAILED(hr) || ifactory == nullptr) {
        LogInfo("HookFactory: factory does not support IDXGIFactory1, hooking CreateSwapChain only");
        return false;
    }

    MH_STATUS init_status = SafeInitializeMinHook(display_commanderhooks::HookType::DXGI_SWAPCHAIN);
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("HookFactory: MinHook init failed: %d", init_status);
        return false;
    }

    static bool hooked_dxgifactory = false;
    if (hooked_dxgifactory) {
        return true;
    }

    {
        void** vtable = *reinterpret_cast<void***>(ifactory.Get());
        if (CreateAndEnableHook(vtable[IDXGIFactory_CreateSwapChain], IDXGIFactory_CreateSwapChain_Detour,
                                reinterpret_cast<LPVOID*>(&IDXGIFactory_CreateSwapChain_Original),
                                "IDXGIFactory::CreateSwapChain")) {
            g_factory_create_swapchain_hooked.store(true, std::memory_order_relaxed);
            LogInfo("HookFactory: IDXGIFactory::CreateSwapChain hooked");
            hooked_dxgifactory = true;
        }
    }

    Microsoft::WRL::ComPtr<IDXGIFactory1> ifactory1;
    hr = iunknown->QueryInterface(IID_PPV_ARGS(ifactory1.GetAddressOf()));
    if (FAILED(hr) || ifactory1 == nullptr) {
        LogInfo(
            "HookFactory: factory does not support IDXGIFactory1, hooking CreateSwapChainForHwnd and "
            "CreateSwapChainForCoreWindow only");
        return false;
    }
    {
        void** vtable1 = *reinterpret_cast<void***>(ifactory1.Get());
        if (CreateAndEnableHook(vtable1[IDXGIFactory1_CreateSwapChainForHwnd],
                                IDXGIFactory1_CreateSwapChainForHwnd_Detour,
                                reinterpret_cast<LPVOID*>(&IDXGIFactory1_CreateSwapChainForHwnd_Original),
                                "IDXGIFactory1::CreateSwapChainForHwnd")) {
            g_factory_create_swapchain_for_hwnd_hooked.store(true, std::memory_order_relaxed);
            LogInfo("HookFactory: IDXGIFactory1::CreateSwapChainForHwnd hooked");
            hooked_dxgifactory = true;
        }
        if (CreateAndEnableHook(vtable1[IDXGIFactory1_CreateSwapChainForCoreWindow],
                                IDXGIFactory1_CreateSwapChainForCoreWindow_Detour,
                                reinterpret_cast<LPVOID*>(&IDXGIFactory1_CreateSwapChainForCoreWindow_Original),
                                "IDXGIFactory1::CreateSwapChainForCoreWindow")) {
            g_factory_create_swapchain_for_core_window_hooked.store(true, std::memory_order_relaxed);
            LogInfo("HookFactory: IDXGIFactory1::CreateSwapChainForCoreWindow hooked");
            hooked_dxgifactory = true;
        }
    }

    Microsoft::WRL::ComPtr<IDXGIFactory2> ifactory2;
    hr = iunknown->QueryInterface(IID_PPV_ARGS(ifactory2.GetAddressOf()));
    if (SUCCEEDED(hr) && ifactory2 != nullptr) {
        void** vtable2 = *reinterpret_cast<void***>(ifactory2.Get());
        if (CreateAndEnableHook(vtable2[IDXGIFactory2_CreateSwapChainForComposition],
                                IDXGIFactory2_CreateSwapChainForComposition_Detour,
                                reinterpret_cast<LPVOID*>(&IDXGIFactory2_CreateSwapChainForComposition_Original),
                                "IDXGIFactory2::CreateSwapChainForComposition")) {
            g_factory_create_swapchain_for_composition_hooked.store(true, std::memory_order_relaxed);
            LogInfo("HookFactory: IDXGIFactory2::CreateSwapChainForComposition hooked");
            hooked_dxgifactory = true;
        }
    }

    return hooked_dxgifactory;
}

bool HookStreamlineProxyFactory(IUnknown* iunknown) {
    if (iunknown == nullptr) {
        return false;
    }
    if (g_dx9_swapchain_detected.load()) {
        return false;
    }
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::SL_PROXY_DXGI_SWAPCHAIN)) {
        return false;
    }
    Microsoft::WRL::ComPtr<IDXGIFactory> ifactory;
    HRESULT hr = iunknown->QueryInterface(IID_PPV_ARGS(ifactory.GetAddressOf()));
    if (FAILED(hr) || ifactory == nullptr) {
        return false;
    }

    MH_STATUS init_status = SafeInitializeMinHook(display_commanderhooks::HookType::SL_PROXY_DXGI_SWAPCHAIN);
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("HookStreamlineProxyFactory: MinHook init failed: %d", init_status);
        return false;
    }

    static bool streamline_proxy_factory_hooked = false;
    if (streamline_proxy_factory_hooked) {
        return true;
    }

    bool any_hooked = false;
    {
        void** vtable = *reinterpret_cast<void***>(ifactory.Get());
        if (CreateAndEnableHook(vtable[IDXGIFactory_CreateSwapChain], IDXGIFactory_CreateSwapChain_Streamline_Detour,
                                reinterpret_cast<LPVOID*>(&IDXGIFactory_CreateSwapChain_Streamline_Original),
                                "IDXGIFactory::CreateSwapChain (Streamline)")) {
            any_hooked = true;
            LogInfo("HookStreamlineProxyFactory: IDXGIFactory::CreateSwapChain hooked");
        }
    }

    Microsoft::WRL::ComPtr<IDXGIFactory1> ifactory1;
    hr = iunknown->QueryInterface(IID_PPV_ARGS(ifactory1.GetAddressOf()));
    if (SUCCEEDED(hr) && ifactory1 != nullptr) {
        void** vtable1 = *reinterpret_cast<void***>(ifactory1.Get());
        if (CreateAndEnableHook(vtable1[IDXGIFactory1_CreateSwapChainForHwnd],
                                IDXGIFactory1_CreateSwapChainForHwnd_Streamline_Detour,
                                reinterpret_cast<LPVOID*>(&IDXGIFactory1_CreateSwapChainForHwnd_Streamline_Original),
                                "IDXGIFactory1::CreateSwapChainForHwnd (Streamline)")) {
            any_hooked = true;
            LogInfo("HookStreamlineProxyFactory: IDXGIFactory1::CreateSwapChainForHwnd hooked");
        }
        if (CreateAndEnableHook(
                vtable1[IDXGIFactory1_CreateSwapChainForCoreWindow],
                IDXGIFactory1_CreateSwapChainForCoreWindow_Streamline_Detour,
                reinterpret_cast<LPVOID*>(&IDXGIFactory1_CreateSwapChainForCoreWindow_Streamline_Original),
                "IDXGIFactory1::CreateSwapChainForCoreWindow (Streamline)")) {
            any_hooked = true;
            LogInfo("HookStreamlineProxyFactory: IDXGIFactory1::CreateSwapChainForCoreWindow hooked");
        }
    }

    Microsoft::WRL::ComPtr<IDXGIFactory2> ifactory2;
    hr = iunknown->QueryInterface(IID_PPV_ARGS(ifactory2.GetAddressOf()));
    if (SUCCEEDED(hr) && ifactory2 != nullptr) {
        void** vtable2 = *reinterpret_cast<void***>(ifactory2.Get());
        if (CreateAndEnableHook(
                vtable2[IDXGIFactory2_CreateSwapChainForComposition],
                IDXGIFactory2_CreateSwapChainForComposition_Streamline_Detour,
                reinterpret_cast<LPVOID*>(&IDXGIFactory2_CreateSwapChainForComposition_Streamline_Original),
                "IDXGIFactory2::CreateSwapChainForComposition (Streamline)")) {
            any_hooked = true;
            LogInfo("HookStreamlineProxyFactory: IDXGIFactory2::CreateSwapChainForComposition hooked");
        }
    }

    if (any_hooked) {
        streamline_proxy_factory_hooked = true;
        display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
            display_commanderhooks::HookType::SL_PROXY_DXGI_SWAPCHAIN);
    }
    return streamline_proxy_factory_hooked;
}

// Additional DXGI detour functions
HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetBuffer_Detour(IDXGISwapChain* This, UINT Buffer, REFIID riid,
                                                          void** ppSurface) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_core_event_counters[DXGI_CORE_EVENT_GETBUFFER].fetch_add(1);
    HRESULT hr = IDXGISwapChain_GetBuffer_Original(This, Buffer, riid, ppSurface);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGISwapChain::GetBuffer", hr, &s_err_count);
    return hr;
}

std::atomic<int> g_last_set_fullscreen_state{-1};  // -1 for not set, 0 for false, 1 for true
std::atomic<IDXGIOutput*> g_last_set_fullscreen_target{nullptr};
HRESULT STDMETHODCALLTYPE IDXGISwapChain_SetFullscreenState_Detour(IDXGISwapChain* This, BOOL Fullscreen,
                                                                   IDXGIOutput* pTarget) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_core_event_counters[DXGI_CORE_EVENT_SETFULLSCREENSTATE].fetch_add(1);

    if (Fullscreen == g_last_set_fullscreen_state.load() && pTarget == g_last_set_fullscreen_target.load()) {
        return S_OK;
    }

    g_last_set_fullscreen_target.store(pTarget);
    g_last_set_fullscreen_state.store(Fullscreen);

    // Check if fullscreen prevention is enabled (window mode != No changes) and we're trying to go fullscreen
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
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_core_event_counters[DXGI_CORE_EVENT_GETFULLSCREENSTATE].fetch_add(1);
    auto hr = IDXGISwapChain_GetFullscreenState_Original(This, pFullscreen, ppTarget);
    {
        static int s_err_count = 0;
        LogDxgiErrorUpTo10("IDXGISwapChain::GetFullscreenState", hr, &s_err_count);
    }

    // NOTE: we assume that ppTarget is g_last_set_fullscreen_target.load()
    if (ShouldPreventExclusiveFullscreen() && g_last_set_fullscreen_state.load() != -1 && pFullscreen != nullptr) {
        *pFullscreen = g_last_set_fullscreen_state.load();
    }

    // Return proxy wrapper instead of hooking vtable
    if (SUCCEEDED(hr) && ppTarget && *ppTarget) {
        IDXGIOutput* originalOutput = *ppTarget;
        IDXGIOutput6* wrappedOutput = display_commanderhooks::CreateOutputWrapper(originalOutput);
        if (wrappedOutput != nullptr) {
            // Release the original output and replace with wrapper
            originalOutput->Release();
            *ppTarget = wrappedOutput;
        }
    }

    return hr;
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_ResizeBuffers_Detour(IDXGISwapChain* This, UINT BufferCount, UINT Width,
                                                              UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_core_event_counters[DXGI_CORE_EVENT_RESIZEBUFFERS].fetch_add(1);

    // Capture game render resolution (before any modifications) - matches Special K's render_x/render_y
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
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_core_event_counters[DXGI_CORE_EVENT_RESIZETARGET].fetch_add(1);

    // Capture game render resolution (before any modifications) - matches Special K's render_x/render_y
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

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetContainingOutput_Detour(IDXGISwapChain* This, IDXGIOutput** ppOutput) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_core_event_counters[DXGI_CORE_EVENT_GETCONTAININGOUTPUT].fetch_add(1);

    HRESULT hr = IDXGISwapChain_GetContainingOutput_Original(This, ppOutput);
    {
        static int s_err_count = 0;
        LogDxgiErrorUpTo10("IDXGISwapChain::GetContainingOutput", hr, &s_err_count);
    }

    // Return proxy wrapper instead of hooking vtable
    if (SUCCEEDED(hr) && ppOutput && *ppOutput) {
        IDXGIOutput* originalOutput = *ppOutput;
        IDXGIOutput6* wrappedOutput = display_commanderhooks::CreateOutputWrapper(originalOutput);
        if (wrappedOutput != nullptr) {
            // Release the original output and replace with wrapper
            originalOutput->Release();
            *ppOutput = wrappedOutput;
        }
    }

    return hr;
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetFrameStatistics_Detour(IDXGISwapChain* This,
                                                                   DXGI_FRAME_STATISTICS* pStats) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_core_event_counters[DXGI_CORE_EVENT_GETFRAMESTATISTICS].fetch_add(1);
    HRESULT hr = IDXGISwapChain_GetFrameStatistics_Original(This, pStats);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGISwapChain::GetFrameStatistics", hr, &s_err_count);
    return hr;
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetLastPresentCount_Detour(IDXGISwapChain* This, UINT* pLastPresentCount) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_core_event_counters[DXGI_CORE_EVENT_GETLASTPRESENTCOUNT].fetch_add(1);
    HRESULT hr = IDXGISwapChain_GetLastPresentCount_Original(This, pLastPresentCount);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGISwapChain::GetLastPresentCount", hr, &s_err_count);
    return hr;
}

// IDXGISwapChain1 detour functions
HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetFullscreenDesc_Detour(IDXGISwapChain1* This,
                                                                  DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pDesc) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_sc1_event_counters[DXGI_SC1_EVENT_GETFULLSCREENDESC].fetch_add(1);
    HRESULT hr = IDXGISwapChain_GetFullscreenDesc_Original(This, pDesc);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGISwapChain1::GetFullscreenDesc", hr, &s_err_count);
    return hr;
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetHwnd_Detour(IDXGISwapChain1* This, HWND* pHwnd) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_sc1_event_counters[DXGI_SC1_EVENT_GETHWND].fetch_add(1);

    HRESULT hr = IDXGISwapChain_GetHwnd_Original(This, pHwnd);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGISwapChain1::GetHwnd", hr, &s_err_count);
    return hr;
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetCoreWindow_Detour(IDXGISwapChain1* This, REFIID refiid, void** ppUnk) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_sc1_event_counters[DXGI_SC1_EVENT_GETCOREWINDOW].fetch_add(1);
    HRESULT hr = IDXGISwapChain_GetCoreWindow_Original(This, refiid, ppUnk);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGISwapChain1::GetCoreWindow", hr, &s_err_count);
    return hr;
}

BOOL STDMETHODCALLTYPE IDXGISwapChain_IsTemporaryMonoSupported_Detour(IDXGISwapChain1* This) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_sc1_event_counters[DXGI_SC1_EVENT_ISTEMPORARYMONOSUPPORTED].fetch_add(1);
    return IDXGISwapChain_IsTemporaryMonoSupported_Original(This);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetRestrictToOutput_Detour(IDXGISwapChain1* This,
                                                                    IDXGIOutput** ppRestrictToOutput) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_sc1_event_counters[DXGI_SC1_EVENT_GETRESTRICTTOOUTPUT].fetch_add(1);

    HRESULT hr = IDXGISwapChain_GetRestrictToOutput_Original(This, ppRestrictToOutput);
    {
        static int s_err_count = 0;
        LogDxgiErrorUpTo10("IDXGISwapChain1::GetRestrictToOutput", hr, &s_err_count);
    }

    // Return proxy wrapper instead of hooking vtable
    if (SUCCEEDED(hr) && ppRestrictToOutput && *ppRestrictToOutput) {
        IDXGIOutput* originalOutput = *ppRestrictToOutput;
        IDXGIOutput6* wrappedOutput = display_commanderhooks::CreateOutputWrapper(originalOutput);
        if (wrappedOutput != nullptr) {
            // Release the original output and replace with wrapper
            originalOutput->Release();
            *ppRestrictToOutput = wrappedOutput;
        }
    }

    return hr;
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_SetBackgroundColor_Detour(IDXGISwapChain1* This, const DXGI_RGBA* pColor) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_sc1_event_counters[DXGI_SC1_EVENT_SETBACKGROUNDCOLOR].fetch_add(1);
    HRESULT hr = IDXGISwapChain_SetBackgroundColor_Original(This, pColor);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGISwapChain1::SetBackgroundColor", hr, &s_err_count);
    return hr;
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetBackgroundColor_Detour(IDXGISwapChain1* This, DXGI_RGBA* pColor) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_sc1_event_counters[DXGI_SC1_EVENT_GETBACKGROUNDCOLOR].fetch_add(1);
    HRESULT hr = IDXGISwapChain_GetBackgroundColor_Original(This, pColor);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGISwapChain1::GetBackgroundColor", hr, &s_err_count);
    return hr;
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_SetRotation_Detour(IDXGISwapChain1* This, DXGI_MODE_ROTATION Rotation) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_sc1_event_counters[DXGI_SC1_EVENT_SETROTATION].fetch_add(1);
    HRESULT hr = IDXGISwapChain_SetRotation_Original(This, Rotation);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGISwapChain1::SetRotation", hr, &s_err_count);
    return hr;
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetRotation_Detour(IDXGISwapChain1* This, DXGI_MODE_ROTATION* pRotation) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_sc1_event_counters[DXGI_SC1_EVENT_GETROTATION].fetch_add(1);
    HRESULT hr = IDXGISwapChain_GetRotation_Original(This, pRotation);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGISwapChain1::GetRotation", hr, &s_err_count);
    return hr;
}

// IDXGISwapChain2 detour functions
HRESULT STDMETHODCALLTYPE IDXGISwapChain_SetSourceSize_Detour(IDXGISwapChain2* This, UINT Width, UINT Height) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_sc2_event_counters[DXGI_SC2_EVENT_SETSOURCESIZE].fetch_add(1);
    HRESULT hr = IDXGISwapChain_SetSourceSize_Original(This, Width, Height);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGISwapChain2::SetSourceSize", hr, &s_err_count);
    return hr;
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetSourceSize_Detour(IDXGISwapChain2* This, UINT* pWidth, UINT* pHeight) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_sc2_event_counters[DXGI_SC2_EVENT_GETSOURCESIZE].fetch_add(1);
    HRESULT hr = IDXGISwapChain_GetSourceSize_Original(This, pWidth, pHeight);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGISwapChain2::GetSourceSize", hr, &s_err_count);
    return hr;
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_SetMaximumFrameLatency_Detour(IDXGISwapChain2* This, UINT MaxLatency) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_sc2_event_counters[DXGI_SC2_EVENT_SETMAXIMUMFRAMELATENCY].fetch_add(1);
    HRESULT hr = IDXGISwapChain_SetMaximumFrameLatency_Original(This, MaxLatency);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGISwapChain2::SetMaximumFrameLatency", hr, &s_err_count);
    return hr;
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetMaximumFrameLatency_Detour(IDXGISwapChain2* This, UINT* pMaxLatency) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_sc2_event_counters[DXGI_SC2_EVENT_GETMAXIMUMFRAMELATENCY].fetch_add(1);
    HRESULT hr = IDXGISwapChain_GetMaximumFrameLatency_Original(This, pMaxLatency);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGISwapChain2::GetMaximumFrameLatency", hr, &s_err_count);
    return hr;
}

HANDLE STDMETHODCALLTYPE IDXGISwapChain_GetFrameLatencyWaitableObject_Detour(IDXGISwapChain2* This) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_sc2_event_counters[DXGI_SC2_EVENT_GETFRAMELATENCYWAIABLEOBJECT].fetch_add(1);
    return IDXGISwapChain_GetFrameLatencyWaitableObject_Original(This);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_SetMatrixTransform_Detour(IDXGISwapChain2* This,
                                                                   const DXGI_MATRIX_3X2_F* pMatrix) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_sc2_event_counters[DXGI_SC2_EVENT_SETMATRIXTRANSFORM].fetch_add(1);
    HRESULT hr = IDXGISwapChain_SetMatrixTransform_Original(This, pMatrix);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGISwapChain2::SetMatrixTransform", hr, &s_err_count);
    return hr;
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetMatrixTransform_Detour(IDXGISwapChain2* This, DXGI_MATRIX_3X2_F* pMatrix) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_sc2_event_counters[DXGI_SC2_EVENT_GETMATRIXTRANSFORM].fetch_add(1);
    HRESULT hr = IDXGISwapChain_GetMatrixTransform_Original(This, pMatrix);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGISwapChain2::GetMatrixTransform", hr, &s_err_count);
    return hr;
}

// IDXGISwapChain3 detour functions
UINT STDMETHODCALLTYPE IDXGISwapChain_GetCurrentBackBufferIndex_Detour(IDXGISwapChain3* This) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_sc3_event_counters[DXGI_SC3_EVENT_GETCURRENTBACKBUFFERINDEX].fetch_add(1);
    return IDXGISwapChain_GetCurrentBackBufferIndex_Original(This);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_SetColorSpace1_Detour(IDXGISwapChain3* This,
                                                               DXGI_COLOR_SPACE_TYPE ColorSpace) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_sc3_event_counters[DXGI_SC3_EVENT_SETCOLORSPACE1].fetch_add(1);
    HRESULT hr = IDXGISwapChain_SetColorSpace1_Original(This, ColorSpace);
    static int s_err_count = 0;
    if (FAILED(hr) && s_err_count < 10) {
        LogError("[DXGI error] IDXGISwapChain3::SetColorSpace1(ColorSpace=%s (%d)) returned 0x%08X",
                 utils::GetDXGIColorSpaceString(ColorSpace), static_cast<int>(ColorSpace), static_cast<unsigned>(hr));
        s_err_count++;
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_ResizeBuffers1_Detour(IDXGISwapChain3* This, UINT BufferCount, UINT Width,
                                                               UINT Height, DXGI_FORMAT Format, UINT SwapChainFlags,
                                                               const UINT* pCreationNodeMask,
                                                               IUnknown* const* ppPresentQueue) {
    CALL_GUARD(utils::get_now_ns());
    g_dxgi_sc3_event_counters[DXGI_SC3_EVENT_RESIZEBUFFERS1].fetch_add(1);

    // Capture game render resolution (before any modifications) - matches Special K's render_x/render_y
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

// IDXGISwapChain4 detour functions
HRESULT STDMETHODCALLTYPE IDXGISwapChain_SetHDRMetaData_Detour(IDXGISwapChain4* This, DXGI_HDR_METADATA_TYPE Type,
                                                               UINT Size, void* pMetaData) {
    CALL_GUARD(utils::get_now_ns());
    // Increment DXGI SetHDRMetaData counter
    g_dxgi_sc4_event_counters[DXGI_SC4_EVENT_SETHDRMETADATA].fetch_add(1);

    // Log the HDR metadata call (only on first few calls to avoid spam)
    static int sethdrmetadata_log_count = 0;
    if (sethdrmetadata_log_count < 3) {
        LogInfo("SetHDRMetaData called - Type: %d, Size: %u", static_cast<int>(Type), Size);
        sethdrmetadata_log_count++;
    }

    // Call original function
    if (IDXGISwapChain_SetHDRMetaData_Original != nullptr) {
        HRESULT hr = IDXGISwapChain_SetHDRMetaData_Original(This, Type, Size, pMetaData);
        static int s_err_count = 0;
        LogDxgiErrorUpTo10("IDXGISwapChain4::SetHDRMetaData", hr, &s_err_count);
        return hr;
    }

    // Fallback to direct call if hook failed
    return This->SetHDRMetaData(Type, Size, pMetaData);
}

// Hooked IDXGIOutput functions
HRESULT STDMETHODCALLTYPE IDXGIOutput_SetGammaControl_Detour(IDXGIOutput* This, const DXGI_GAMMA_CONTROL* pArray) {
    CALL_GUARD(utils::get_now_ns());
    // Increment DXGI Output SetGammaControl counter
    g_dxgi_output_event_counters[DXGI_OUTPUT_EVENT_SETGAMMACONTROL].fetch_add(1);

    // Log the SetGammaControl call (only on first few calls to avoid spam)
    static int setgammacontrol_log_count = 0;
    if (setgammacontrol_log_count < 3) {
        LogInfo("IDXGIOutput::SetGammaControl called");
        setgammacontrol_log_count++;
    }

    // Call original function
    if (IDXGIOutput_SetGammaControl_Original != nullptr) {
        HRESULT hr = IDXGIOutput_SetGammaControl_Original(This, pArray);
        static int s_err_count = 0;
        LogDxgiErrorUpTo10("IDXGIOutput::SetGammaControl", hr, &s_err_count);
        return hr;
    }

    // Fallback to direct call if hook failed
    return This->SetGammaControl(pArray);
}

HRESULT STDMETHODCALLTYPE IDXGIOutput_GetGammaControl_Detour(IDXGIOutput* This, DXGI_GAMMA_CONTROL* pArray) {
    CALL_GUARD(utils::get_now_ns());
    // Increment DXGI Output GetGammaControl counter
    g_dxgi_output_event_counters[DXGI_OUTPUT_EVENT_GETGAMMACONTROL].fetch_add(1);

    // Log the GetGammaControl call (only on first few calls to avoid spam)
    static int getgammacontrol_log_count = 0;
    if (getgammacontrol_log_count < 3) {
        LogInfo("IDXGIOutput::GetGammaControl called");
        getgammacontrol_log_count++;
    }

    // Call original function
    if (IDXGIOutput_GetGammaControl_Original != nullptr) {
        HRESULT hr = IDXGIOutput_GetGammaControl_Original(This, pArray);
        static int s_err_count = 0;
        LogDxgiErrorUpTo10("IDXGIOutput::GetGammaControl", hr, &s_err_count);
        return hr;
    }

    // Fallback to direct call if hook failed
    return This->GetGammaControl(pArray);
}

HRESULT STDMETHODCALLTYPE IDXGIOutput_GetDesc_Detour(IDXGIOutput* This, DXGI_OUTPUT_DESC* pDesc) {
    CALL_GUARD(utils::get_now_ns());
    // Increment DXGI Output GetDesc counter
    g_dxgi_output_event_counters[DXGI_OUTPUT_EVENT_GETDESC].fetch_add(1);

    // Log the GetDesc call (only on first few calls to avoid spam)
    static int getdesc_log_count = 0;
    if (getdesc_log_count < 3) {
        LogInfo("IDXGIOutput::GetDesc called");
        getdesc_log_count++;
    }

    // Call original function
    if (IDXGIOutput_GetDesc_Original != nullptr) {
        HRESULT hr = IDXGIOutput_GetDesc_Original(This, pDesc);
        static int s_err_count = 0;
        LogDxgiErrorUpTo10("IDXGIOutput::GetDesc", hr, &s_err_count);
        return hr;
    }

    // Fallback to direct call if hook failed
    return This->GetDesc(pDesc);
}

// Hooked IDXGIOutput6::GetDesc1 function
HRESULT STDMETHODCALLTYPE IDXGIOutput6_GetDesc1_Detour(IDXGIOutput6* This, DXGI_OUTPUT_DESC1* pDesc) {
    CALL_GUARD(utils::get_now_ns());
    if (pDesc == nullptr) {
        return DXGI_ERROR_INVALID_CALL;
    }

    // Call original function
    HRESULT hr = S_OK;
    if (IDXGIOutput6_GetDesc1_Original != nullptr) {
        hr = IDXGIOutput6_GetDesc1_Original(This, pDesc);
    } else {
        // Fallback to direct call if hook failed
        hr = This->GetDesc1(pDesc);
    }
    {
        static int s_err_count = 0;
        LogDxgiErrorUpTo10("IDXGIOutput6::GetDesc1", hr, &s_err_count);
    }

    // Hide HDR capabilities if enabled (similar to Special-K's approach)
    if (SUCCEEDED(hr) && pDesc != nullptr && settings::g_advancedTabSettings.hide_hdr_capabilities.GetValue()) {
        // Change HDR10 color space to sRGB to hide HDR support
        if (pDesc->ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) {
            pDesc->ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

            static int hdr_hidden_log_count = 0;
            if (hdr_hidden_log_count < 3) {
                LogInfo("HDR hiding: IDXGIOutput6::GetDesc1 - hiding HDR10 color space, forcing to sRGB");
                hdr_hidden_log_count++;
            }
        }
    }

    return hr;
}

// Global variables to track hooked swapchains
namespace {
// Legacy variables - kept for compatibility but will be replaced by SwapchainTrackingManager
IDXGISwapChain* g_hooked_swapchain = nullptr;

// Track hooked IDXGIOutput objects to avoid duplicate hooking
std::atomic<bool> g_dxgi_output_hooks_installed{false};
}  // namespace

// Hook a specific swapchain's vtable
bool HookSwapchain(IDXGISwapChain* swapchain) {
    if (g_dx9_swapchain_detected.load()) {
        return false;
    }
    // Check if LoadLibrary hooks should be suppressed
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

    // Query for IDXGISwapChain4 before getting vtable

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain1;
    Microsoft::WRL::ComPtr<IDXGISwapChain2> swapchain2;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain3;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapchain4;
    auto vtable_version = 0;

    // Get the vtable from IDXGISwapChain4
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
    /*
    | Index | Interface | Method | Description |
    |-------|-----------|--------|-------------|
    | 0-2 | IUnknown | QueryInterface, AddRef, Release | Base COM interface methods |
    | 3-5 | IDXGIObject | SetPrivateData, SetPrivateDataInterface, GetPrivateData | Object private data management |
    | 6 | IDXGIObject | GetParent | Get parent object |
    | 7 | IDXGIDeviceSubObject | GetDevice | Get associated device |
    | 8 | IDXGISwapChain | Present | Present frame to screen |
    | 9 | IDXGISwapChain | GetBuffer | Get back buffer |
    | 10 | IDXGISwapChain | SetFullscreenState | Set fullscreen state |
    | 11 | IDXGISwapChain | GetFullscreenState | Get fullscreen state |
    | 12 | IDXGISwapChain | GetDesc | Get swapchain description |
    | 13 | IDXGISwapChain | ResizeBuffers | Resize back buffers |
    | 14 | IDXGISwapChain | ResizeTarget | Resize target window |
    | 15 | IDXGISwapChain | GetContainingOutput | Get containing output |
    | 16 | IDXGISwapChain | GetFrameStatistics | Get frame statistics |
    | 17 | IDXGISwapChain | GetLastPresentCount | Get last present count | // IDXGISwapChain up to index 17
    | 18 | IDXGISwapChain1 | GetDesc1 | Get swapchain description (v1) | // IDXGISwapChain1 18
    | 19 | IDXGISwapChain1 | GetFullscreenDesc | Get fullscreen description |
    | 20 | IDXGISwapChain1 | GetHwnd | Get window handle |
    | 21 | IDXGISwapChain1 | GetCoreWindow | Get core window |
    | 22 | IDXGISwapChain1 | Present1 | Present with parameters | // ok
    | 23 | IDXGISwapChain1 | IsTemporaryMonoSupported | Check mono support |
    | 24 | IDXGISwapChain1 | GetRestrictToOutput | Get restricted output |
    | 25 | IDXGISwapChain1 | SetBackgroundColor | Set background color |
    | 26 | IDXGISwapChain1 | GetBackgroundColor | Get background color |
    | 27 | IDXGISwapChain1 | SetRotation | Set rotation |
    | 28 | IDXGISwapChain1 | GetRotation | Get rotation | // 28 IDXGISwapChain1
    | 29 | IDXGISwapChain2 | SetSourceSize | Set source size | // 29 IDXGISwapChain2
    | 30 | IDXGISwapChain2 | GetSourceSize | Get source size |
    | 31 | IDXGISwapChain2 | SetMaximumFrameLatency | Set max frame latency |
    | 32 | IDXGISwapChain2 | GetMaximumFrameLatency | Get max frame latency |
    | 33 | IDXGISwapChain2 | GetFrameLatencyWaitableObject | Get latency waitable object |
    | 34 | IDXGISwapChain2 | SetMatrixTransform | Set matrix transform |
    | 35 | IDXGISwapChain2 | GetMatrixTransform | Get matrix transform | // 35 IDXGISwapChain2
    | **36** | **IDXGISwapChain3** | **GetCurrentBackBufferIndex** | **Get current back buffer index** |
    | **37** | **IDXGISwapChain3** | **CheckColorSpaceSupport** | **Check color space support** ⭐ |
    | **38** | **IDXGISwapChain3** | **SetColorSpace1** | **Set color space** |
    | **39** | **IDXGISwapChain3** | **ResizeBuffers1** | **Resize buffers with parameters** |
    */

    // minhook initialization
    MH_STATUS init_status = SafeInitializeMinHook(display_commanderhooks::HookType::DXGI_SWAPCHAIN);
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("[HookSwapchain] Failed to initialize MinHook for DXGI hooks - Status: %d", init_status);
        return false;
    }
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
        display_commanderhooks::HookType::DXGI_SWAPCHAIN);

    LogInfo("[HookSwapchain] IDXGISwapChain4 interface confirmed, hooking all swapchain methods");

    // ============================================================================
    // GROUP 0: IDXGISwapChain (Base Interface) - Indices 8-17
    // ============================================================================
    LogInfo("[HookSwapchain] Hooking IDXGISwapChain methods (indices 8-17)");

    {
        // Hook Present (index 8) - Critical method, always present
        if (!CreateAndEnableHook(vtable[8], IDXGISwapChain_Present_Detour, (LPVOID*)&IDXGISwapChain_Present_Original,
                                 "IDXGISwapChain::Present")) {
            LogError("[HookSwapchain] Failed to create and enable IDXGISwapChain::Present hook");
            return false;
        }

        if (!CreateAndEnableHook(vtable[9], IDXGISwapChain_GetBuffer_Detour,
                                 (LPVOID*)&IDXGISwapChain_GetBuffer_Original, "IDXGISwapChain::GetBuffer")) {
            LogError("[HookSwapchain] Failed to create and enable IDXGISwapChain::GetBuffer hook");
        }
        if (!CreateAndEnableHook(vtable[10], IDXGISwapChain_SetFullscreenState_Detour,
                                 (LPVOID*)&IDXGISwapChain_SetFullscreenState_Original,
                                 "IDXGISwapChain::SetFullscreenState")) {
            LogError("[HookSwapchain] Failed to create and enable IDXGISwapChain::SetFullscreenState hook");
        }
        if (!CreateAndEnableHook(vtable[11], IDXGISwapChain_GetFullscreenState_Detour,
                                 (LPVOID*)&IDXGISwapChain_GetFullscreenState_Original,
                                 "IDXGISwapChain::GetFullscreenState")) {
            LogError("[HookSwapchain] Failed to create and enable IDXGISwapChain::GetFullscreenState hook");
        }
        if (!CreateAndEnableHook(vtable[12], IDXGISwapChain_GetDesc_Detour, (LPVOID*)&IDXGISwapChain_GetDesc_Original,
                                 "IDXGISwapChain::GetDesc")) {
            LogError("[HookSwapchain] Failed to create and enable IDXGISwapChain::GetDesc hook");
        }
        if (!CreateAndEnableHook(vtable[13], IDXGISwapChain_ResizeBuffers_Detour,
                                 (LPVOID*)&IDXGISwapChain_ResizeBuffers_Original, "IDXGISwapChain::ResizeBuffers")) {
            LogError("[HookSwapchain] Failed to create and enable IDXGISwapChain::ResizeBuffers hook");
        }
        if (!CreateAndEnableHook(vtable[14], IDXGISwapChain_ResizeTarget_Detour,
                                 (LPVOID*)&IDXGISwapChain_ResizeTarget_Original, "IDXGISwapChain::ResizeTarget")) {
            LogError("[HookSwapchain] Failed to create and enable IDXGISwapChain::ResizeTarget hook");
        }
        if (!CreateAndEnableHook(vtable[15], IDXGISwapChain_GetContainingOutput_Detour,
                                 (LPVOID*)&IDXGISwapChain_GetContainingOutput_Original,
                                 "IDXGISwapChain::GetContainingOutput")) {
            LogError("[HookSwapchain] Failed to create and enable IDXGISwapChain::GetContainingOutput hook");
        }
        if (!CreateAndEnableHook(vtable[16], IDXGISwapChain_GetFrameStatistics_Detour,
                                 (LPVOID*)&IDXGISwapChain_GetFrameStatistics_Original,
                                 "IDXGISwapChain::GetFrameStatistics")) {
            LogError("[HookSwapchain] Failed to create and enable IDXGISwapChain::GetFrameStatistics hook");
        }
        if (!CreateAndEnableHook(vtable[17], IDXGISwapChain_GetLastPresentCount_Detour,
                                 (LPVOID*)&IDXGISwapChain_GetLastPresentCount_Original,
                                 "IDXGISwapChain::GetLastPresentCount")) {
            LogError("[HookSwapchain] Failed to create and enable IDXGISwapChain::GetLastPresentCount hook");
        }
    }

    // ============================================================================
    // GROUP 1: IDXGISwapChain1 (Extended Interface) - Indices 18-28
    // ============================================================================
    if (vtable_version >= 1) {
        LogInfo("[HookSwapchain] Hooking IDXGISwapChain1 methods (indices 18-28)");
        if (!CreateAndEnableHook(vtable[18], IDXGISwapChain_GetDesc1_Detour, (LPVOID*)&IDXGISwapChain_GetDesc1_Original,
                                 "IDXGISwapChain1::GetDesc1")) {
            LogError("[HookSwapchain] Failed to create and enable IDXGISwapChain1::GetDesc1 hook");
        }
        if (!CreateAndEnableHook(vtable[19], IDXGISwapChain_GetFullscreenDesc_Detour,
                                 (LPVOID*)&IDXGISwapChain_GetFullscreenDesc_Original,
                                 "IDXGISwapChain1::GetFullscreenDesc")) {
            LogError("[HookSwapchain] Failed to create and enable IDXGISwapChain1::GetFullscreenDesc hook");
        }
        if (!CreateAndEnableHook(vtable[20], IDXGISwapChain_GetHwnd_Detour, (LPVOID*)&IDXGISwapChain_GetHwnd_Original,
                                 "IDXGISwapChain1::GetHwnd")) {
            LogError("[HookSwapchain] Failed to create and enable IDXGISwapChain1::GetHwnd hook");
        }
        if (!CreateAndEnableHook(vtable[21], IDXGISwapChain_GetCoreWindow_Detour,
                                 (LPVOID*)&IDXGISwapChain_GetCoreWindow_Original, "IDXGISwapChain1::GetCoreWindow")) {
            LogError("[HookSwapchain] Failed to create and enable IDXGISwapChain1::GetCoreWindow hook");
        }

        // Hook Present1 (index 22) - Critical for IDXGISwapChain1
        if (!CreateAndEnableHook(vtable[22], IDXGISwapChain_Present1_Detour, (LPVOID*)&IDXGISwapChain_Present1_Original,
                                 "IDXGISwapChain1::Present1")) {
            LogError("[HookSwapchain] Failed to create and enable IDXGISwapChain1::Present1 hook");
        }
        if (!CreateAndEnableHook(vtable[23], IDXGISwapChain_IsTemporaryMonoSupported_Detour,
                                 (LPVOID*)&IDXGISwapChain_IsTemporaryMonoSupported_Original,
                                 "IDXGISwapChain1::IsTemporaryMonoSupported")) {
            LogError("[HookSwapchain] Failed to create and enable IDXGISwapChain1::IsTemporaryMonoSupported hook");
        }
        if (!CreateAndEnableHook(vtable[24], IDXGISwapChain_GetRestrictToOutput_Detour,
                                 (LPVOID*)&IDXGISwapChain_GetRestrictToOutput_Original,
                                 "IDXGISwapChain1::GetRestrictToOutput")) {
            LogError("[HookSwapchain] Failed to create and enable IDXGISwapChain1::GetRestrictToOutput hook");
        }
        if (!CreateAndEnableHook(vtable[25], IDXGISwapChain_SetBackgroundColor_Detour,
                                 (LPVOID*)&IDXGISwapChain_SetBackgroundColor_Original,
                                 "IDXGISwapChain1::SetBackgroundColor")) {
            LogError("[HookSwapchain] Failed to create and enable IDXGISwapChain1::SetBackgroundColor hook");
        }
        if (!CreateAndEnableHook(vtable[26], IDXGISwapChain_GetBackgroundColor_Detour,
                                 (LPVOID*)&IDXGISwapChain_GetBackgroundColor_Original,
                                 "IDXGISwapChain1::GetBackgroundColor")) {
            LogError("[HookSwapchain] Failed to create and enable IDXGISwapChain1::GetBackgroundColor hook");
        }
        if (!CreateAndEnableHook(vtable[27], IDXGISwapChain_SetRotation_Detour,
                                 (LPVOID*)&IDXGISwapChain_SetRotation_Original, "IDXGISwapChain1::SetRotation")) {
            LogError("[HookSwapchain] Failed to create and enable IDXGISwapChain1::SetRotation hook");
        }
        if (!CreateAndEnableHook(vtable[28], IDXGISwapChain_GetRotation_Detour,
                                 (LPVOID*)&IDXGISwapChain_GetRotation_Original, "IDXGISwapChain1::GetRotation")) {
            LogError("[HookSwapchain] Failed to create and enable IDXGISwapChain1::GetRotation hook");
        }
    }

    // ============================================================================
    // GROUP 2: IDXGISwapChain2 (Extended Interface) - Indices 29-35
    // ============================================================================
    if (vtable_version >= 2) {
        LogInfo("Hooking IDXGISwapChain2 methods (indices 29-35)");

        if (!CreateAndEnableHook(vtable[29], IDXGISwapChain_SetSourceSize_Detour,
                                 (LPVOID*)&IDXGISwapChain_SetSourceSize_Original, "IDXGISwapChain2::SetSourceSize")) {
            LogError("Failed to create and enable IDXGISwapChain2::SetSourceSize hook");
        }
        if (!CreateAndEnableHook(vtable[30], IDXGISwapChain_GetSourceSize_Detour,
                                 (LPVOID*)&IDXGISwapChain_GetSourceSize_Original, "IDXGISwapChain2::GetSourceSize")) {
            LogError("Failed to create and enable IDXGISwapChain2::GetSourceSize hook");
        }
        if (!CreateAndEnableHook(vtable[31], IDXGISwapChain_SetMaximumFrameLatency_Detour,
                                 (LPVOID*)&IDXGISwapChain_SetMaximumFrameLatency_Original,
                                 "IDXGISwapChain2::SetMaximumFrameLatency")) {
            LogError("Failed to create and enable IDXGISwapChain2::SetMaximumFrameLatency hook");
        }
        if (!CreateAndEnableHook(vtable[32], IDXGISwapChain_GetMaximumFrameLatency_Detour,
                                 (LPVOID*)&IDXGISwapChain_GetMaximumFrameLatency_Original,
                                 "IDXGISwapChain2::GetMaximumFrameLatency")) {
            LogError("Failed to create and enable IDXGISwapChain2::GetMaximumFrameLatency hook");
        }
        if (!CreateAndEnableHook(vtable[33], IDXGISwapChain_GetFrameLatencyWaitableObject_Detour,
                                 (LPVOID*)&IDXGISwapChain_GetFrameLatencyWaitableObject_Original,
                                 "IDXGISwapChain2::GetFrameLatencyWaitableObject")) {
            LogError("Failed to create and enable IDXGISwapChain2::GetFrameLatencyWaitableObject hook");
        }
        if (!CreateAndEnableHook(vtable[34], IDXGISwapChain_SetMatrixTransform_Detour,
                                 (LPVOID*)&IDXGISwapChain_SetMatrixTransform_Original,
                                 "IDXGISwapChain2::SetMatrixTransform")) {
            LogError("Failed to create and enable IDXGISwapChain2::SetMatrixTransform hook");
        }
        if (!CreateAndEnableHook(vtable[35], IDXGISwapChain_GetMatrixTransform_Detour,
                                 (LPVOID*)&IDXGISwapChain_GetMatrixTransform_Original,
                                 "IDXGISwapChain2::GetMatrixTransform")) {
            LogError("Failed to create and enable IDXGISwapChain2::GetMatrixTransform hook");
        }
    }

    // ============================================================================
    // GROUP 3: IDXGISwapChain3 (Extended Interface) - Indices 36-39
    // ============================================================================
    if (vtable_version >= 3) {
        LogInfo("Hooking IDXGISwapChain3 methods (indices 36-39)");
        if (!CreateAndEnableHook(vtable[36], IDXGISwapChain_GetCurrentBackBufferIndex_Detour,
                                 (LPVOID*)&IDXGISwapChain_GetCurrentBackBufferIndex_Original,
                                 "IDXGISwapChain3::GetCurrentBackBufferIndex")) {
            LogError("Failed to create and enable IDXGISwapChain3::GetCurrentBackBufferIndex hook");
        }
        // Hook CheckColorSpaceSupport (index 37)
        if (!CreateAndEnableHook(vtable[37], IDXGISwapChain_CheckColorSpaceSupport_Detour,
                                 (LPVOID*)&IDXGISwapChain_CheckColorSpaceSupport_Original,
                                 "IDXGISwapChain3::CheckColorSpaceSupport")) {
            LogError("Failed to create and enable IDXGISwapChain3::CheckColorSpaceSupport hook");
            // Don't return false, this is not critical for basic functionality
        }

        if (!CreateAndEnableHook(vtable[38], IDXGISwapChain_SetColorSpace1_Detour,
                                 (LPVOID*)&IDXGISwapChain_SetColorSpace1_Original, "IDXGISwapChain3::SetColorSpace1")) {
            LogError("Failed to create and enable IDXGISwapChain3::SetColorSpace1 hook");
        }
        if (!CreateAndEnableHook(vtable[39], IDXGISwapChain_ResizeBuffers1_Detour,
                                 (LPVOID*)&IDXGISwapChain_ResizeBuffers1_Original, "IDXGISwapChain3::ResizeBuffers1")) {
            LogError("Failed to create and enable IDXGISwapChain3::ResizeBuffers1 hook");
        }
    }

    // ============================================================================
    // GROUP 4: IDXGISwapChain4 (Extended Interface) - Indices 40+
    // ============================================================================
    if (vtable_version >= 4) {
        LogInfo("Hooking IDXGISwapChain4 methods (indices 40+)");
        if (!CreateAndEnableHook(vtable[40], IDXGISwapChain_SetHDRMetaData_Detour,
                                 (LPVOID*)&IDXGISwapChain_SetHDRMetaData_Original, "IDXGISwapChain4::SetHDRMetaData")) {
            LogError("Failed to create and enable IDXGISwapChain4::SetHDRMetaData hook");
        }
    }

    LogInfo("Successfully hooked IDXGISWAPCHAIN4 for swapchain: %x%p", swapchain);

    return true;
}

// Hook only Present and Present1 on a Streamline proxy swap chain (sl_proxy_dxgi_swapchain, sl_proxy_dxgi_swapchain1)
// for FPS limiter. Idempotent per vtable (first proxy of each type gets hooked).
bool HookStreamlineProxySwapchain(IDXGISwapChain* swapchain) {
    if (swapchain == nullptr) {
        return false;
    }
    if (g_dx9_swapchain_detected.load()) {
        return false;
    }
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::SL_PROXY_DXGI_SWAPCHAIN)) {
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain1;
    Microsoft::WRL::ComPtr<IDXGISwapChain2> swapchain2;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain3;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapchain4;
    void** vtable = nullptr;
    int vtable_version = 0;

    if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain4)))) {
        vtable_version = 4;
        vtable = *reinterpret_cast<void***>(swapchain4.Get());
    } else if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain3)))) {
        vtable_version = 3;
        vtable = *reinterpret_cast<void***>(swapchain3.Get());
    } else if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain2)))) {
        vtable_version = 2;
        vtable = *reinterpret_cast<void***>(swapchain2.Get());
    } else if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain1)))) {
        vtable_version = 1;
        vtable = *reinterpret_cast<void***>(swapchain1.Get());
    } else {
        vtable = *reinterpret_cast<void***>(swapchain);
    }

    if (vtable == nullptr) {
        return false;
    }

    MH_STATUS init_status = SafeInitializeMinHook(display_commanderhooks::HookType::SL_PROXY_DXGI_SWAPCHAIN);
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("HookStreamlineProxySwapchain: MinHook init failed: %d", init_status);
        return false;
    }

    static bool streamline_proxy_present_hooked = false;
    static bool streamline_proxy_present1_hooked = false;
    bool any_hooked = false;

    if (!streamline_proxy_present_hooked
        && CreateAndEnableHook(vtable[8], IDXGISwapChain_Present_Streamline_Detour,
                               reinterpret_cast<LPVOID*>(&IDXGISwapChain_Present_Streamline_Original),
                               "IDXGISwapChain::Present (Streamline proxy)")) {
        streamline_proxy_present_hooked = true;
        any_hooked = true;
        LogInfo("HookStreamlineProxySwapchain: Present hooked (sl_proxy_dxgi_swapchain)");
    }

    if (vtable_version >= 1 && !streamline_proxy_present1_hooked
        && CreateAndEnableHook(vtable[22], IDXGISwapChain_Present1_Streamline_Detour,
                               reinterpret_cast<LPVOID*>(&IDXGISwapChain_Present1_Streamline_Original),
                               "IDXGISwapChain1::Present1 (Streamline proxy)")) {
        streamline_proxy_present1_hooked = true;
        any_hooked = true;
        LogInfo("HookStreamlineProxySwapchain: Present1 hooked (sl_proxy_dxgi_swapchain1)");
    }

    return any_hooked;
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
