#include "dxgi_present_hooks.hpp"
#include "../../performance_types.hpp"
#include "../../swapchain_events.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../globals.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../settings/developer_tab_settings.hpp"
#include "../../dx11_proxy/dx11_proxy_manager.hpp"
#include "../../latent_sync/refresh_rate_monitor_integration.hpp"
#include "../hook_suppression_manager.hpp"

#include <MinHook.h>
#include <dwmapi.h>

#include <d3d11_4.h>
#include <d3d12.h>
#include <wrl/client.h>
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

        Microsoft::WRL::ComPtr<ID3D11Device> device;
        HRESULT hr = swapchain->GetDevice(IID_PPV_ARGS(&device));
        if (FAILED(hr)) {
            g_gpu_fence_failure_reason.store("D3D11: Failed to get device from swapchain");
            return;
        }

        // Try to get ID3D11Device5 for fence support
        Microsoft::WRL::ComPtr<ID3D11Device5> device5;
        hr = device.As(&device5);
        if (FAILED(hr)) {
            g_gpu_fence_failure_reason.store("D3D11: ID3D11Device5 not supported (requires D3D11.3+ / Windows 10+)");
            return; // Fences require D3D11.3+ (Windows 10+)
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
        device->GetImmediateContext(&context);

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
        g_gpu_fence_failure_reason.store(nullptr); // Clear failure reason on success
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
        g_gpu_fence_failure_reason.store(nullptr); // Clear failure reason on success
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
} // namespace

// Public API wrapper that works with ReShade swapchain
void EnqueueGPUCompletion(reshade::api::swapchain* swapchain, IDXGISwapChain* dxgi_swapchain, reshade::api::command_queue* command_queue = nullptr) {
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

namespace display_commanderhooks::dxgi {

// Helper function to detect swapchain interface version
int GetSwapchainInterfaceVersion(IDXGISwapChain* swapchain) {
    // Try IDXGISwapChain4 first (highest version)
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapchain4{};
    if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain4)))) {
        return 4;
    }

    // Try IDXGISwapChain3
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain3{};
    if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain3)))) {
        return 3;
    }

    // Try IDXGISwapChain2
    Microsoft::WRL::ComPtr<IDXGISwapChain2> swapchain2{};
    if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain2)))) {
        return 2;
    }

    // Try IDXGISwapChain1
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain1{};
    if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain1)))) {
        return 1;
    }

    // Base IDXGISwapChain
    return 0;
}

// Helper function to safely check if vtable entry exists
bool IsVTableEntryValid(void** vtable, int index) {
    if (!vtable) return false;

    // Basic bounds check - most swapchains should have at least 18 entries (IDXGISwapChain1)
    // but we'll be conservative and check for null
  //  __try {
        return vtable[index] != nullptr;
  //  }
 //   __except(EXCEPTION_EXECUTE_HANDLER) {
//        return false;
 //   }
}

// Original function pointers
IDXGISwapChain_Present_pfn IDXGISwapChain_Present_Original = nullptr;
IDXGISwapChain_Present1_pfn IDXGISwapChain_Present1_Original = nullptr;
IDXGISwapChain_GetDesc_pfn IDXGISwapChain_GetDesc_Original = nullptr;
IDXGISwapChain_GetDesc1_pfn IDXGISwapChain_GetDesc1_Original = nullptr;
IDXGISwapChain_CheckColorSpaceSupport_pfn IDXGISwapChain_CheckColorSpaceSupport_Original = nullptr;
IDXGIFactory_CreateSwapChain_pfn IDXGIFactory_CreateSwapChain_Original = nullptr;

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

    // Track the last native swapchain used in OnPresentUpdateBefore
    std::atomic<IDXGISwapChain*> g_last_present_update_swapchain{nullptr};
} // namespace

// Hooked IDXGISwapChain::Present function
HRESULT STDMETHODCALLTYPE IDXGISwapChain_Present_Detour(IDXGISwapChain *This, UINT SyncInterval, UINT Flags) {
    // Skip if this is not the swapchain used by OnPresentUpdateBefore
    IDXGISwapChain* expected_swapchain = g_last_present_update_swapchain.load();
    if (expected_swapchain != nullptr && This != expected_swapchain) {
        return IDXGISwapChain_Present_Original(This, SyncInterval, Flags);
    }

    DeviceTypeDC device_type = DeviceTypeDC::DX10;
    Microsoft::WRL::ComPtr<IUnknown> device;
    {
        IUnknown* device_raw = nullptr;
        This->GetDevice(IID_PPV_ARGS(&device_raw));
        device = device_raw;
        if (device) {
            // Try to determine if it's D3D11 or D3D12
            Microsoft::WRL::ComPtr<ID3D11Device> d3d11_device;
            Microsoft::WRL::ComPtr<ID3D12Device> d3d12_device;
            if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&d3d11_device)))) {
                device_type = DeviceTypeDC::DX11;
            } else if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&d3d12_device)))) {
                device_type = DeviceTypeDC::DX12;
            }
        }
    }

    // Increment DXGI Present counter
    g_dxgi_core_event_counters[DXGI_CORE_EVENT_PRESENT].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Prevent always on top for swapchain window if enabled
    if (settings::g_developerTabSettings.prevent_always_on_top.GetValue()) {
        HWND swapchain_hwnd = g_last_swapchain_hwnd.load();
        if (swapchain_hwnd && IsWindow(swapchain_hwnd)) {
            // Remove always on top styles from the window
            LONG_PTR current_style = GetWindowLongPtrW(swapchain_hwnd, GWL_EXSTYLE);
            if (current_style & (WS_EX_TOPMOST | WS_EX_TOOLWINDOW)) {
                LONG_PTR new_style = current_style & ~(WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
                SetWindowLongPtrW(swapchain_hwnd, GWL_EXSTYLE, new_style);
                // Only log occasionally to avoid spam
                static std::atomic<int> prevent_always_on_top_log_count{0};
                if (prevent_always_on_top_log_count.fetch_add(1) < 3) {
                    LogInfo("IDXGISwapChain_Present_Detour: Prevented always on top for window 0x%p", swapchain_hwnd);
                }
            }
        }
    }


    ::OnPresentFlags2(&Flags, device_type);

    // Record per-frame FPS sample for background aggregation
    RecordFrameTime(FrameTimeMode::kPresent);

    // Get and cache frame statistics for refresh rate monitoring
    DXGI_FRAME_STATISTICS stats = {};
    if (SUCCEEDED(This->GetFrameStatistics(&stats))) {
        // Use memory_order_release to ensure the shared_ptr is fully constructed before other threads see it
        g_cached_frame_stats.store(std::make_shared<DXGI_FRAME_STATISTICS>(stats), std::memory_order_release);
        ::dxgi::fps_limiter::ProcessFrameStatistics(stats);
    }

    dx11_proxy::DX11ProxyManager::GetInstance().CopyFrameFromGameThread(This);

    if  (IDXGISwapChain_Present_Original == nullptr) {
        LogError("IDXGISwapChain_Present_Detour: IDXGISwapChain_Present_Original is null");
        return This->Present(SyncInterval, Flags);
    }

    auto res= IDXGISwapChain_Present_Original(This, SyncInterval, Flags);

    // Query DXGI composition state (moved from ReShade present events)
    ::QueryDxgiCompositionState(This);

    // Signal refresh rate monitoring thread (after DWM flush)
    ::dxgi::fps_limiter::SignalRefreshRateMonitor();

    if (g_last_present_update_swapchain.load() == This) {
        dx11_proxy::DX11ProxyManager::GetInstance().CopyThreadLoop2();
    }

    // Note: GPU completion measurement is now enqueued earlier in OnPresentUpdateBefore
    // (before flush_command_queue) for more accurate timing

    // Get device from swapchain for latency manager
    ::OnPresentUpdateAfter2(device.Get(), device_type);

    return res;
}

// Hooked IDXGISwapChain1::Present1 function
HRESULT STDMETHODCALLTYPE IDXGISwapChain_Present1_Detour(IDXGISwapChain1 *This, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS *pPresentParameters) {
    // Skip if this is not the swapchain used by OnPresentUpdateBefore
    IDXGISwapChain* expected_swapchain = g_last_present_update_swapchain.load();
    if (expected_swapchain != nullptr && reinterpret_cast<IDXGISwapChain*>(This) != expected_swapchain) {
        return IDXGISwapChain_Present1_Original(This, SyncInterval, PresentFlags, pPresentParameters);
    }
    DeviceTypeDC device_type = DeviceTypeDC::DX10; // Default to DX11 for DXGI
    Microsoft::WRL::ComPtr<IUnknown> device;
    {
        IUnknown* device_raw = nullptr;
        This->GetDevice(IID_PPV_ARGS(&device_raw));
        device = device_raw;
        if (device) {
            // Try to determine if it's D3D11 or D3D12
            Microsoft::WRL::ComPtr<ID3D10Device> d3d10_device;
            Microsoft::WRL::ComPtr<ID3D11Device> d3d11_device;
            Microsoft::WRL::ComPtr<ID3D12Device> d3d12_device;
            if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&d3d11_device)))) {
                device_type = DeviceTypeDC::DX11;
            } else if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&d3d12_device)))) {
                device_type = DeviceTypeDC::DX12;
            } else if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&d3d10_device)))) {
                device_type = DeviceTypeDC::DX10;
            }
        }
    }

    // Increment DXGI Present1 counter
    g_dxgi_sc1_event_counters[DXGI_SC1_EVENT_PRESENT1].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Prevent always on top for swapchain window if enabled
    if (settings::g_developerTabSettings.prevent_always_on_top.GetValue()) {
        HWND swapchain_hwnd = g_last_swapchain_hwnd.load();
        if (swapchain_hwnd && IsWindow(swapchain_hwnd)) {
            // Remove always on top styles from the window
            LONG_PTR current_style = GetWindowLongPtrW(swapchain_hwnd, GWL_EXSTYLE);
            if (current_style & (WS_EX_TOPMOST | WS_EX_TOOLWINDOW)) {
                LONG_PTR new_style = current_style & ~(WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
                SetWindowLongPtrW(swapchain_hwnd, GWL_EXSTYLE, new_style);
                // Only log occasionally to avoid spam
                static std::atomic<int> prevent_always_on_top_log_count_present1{0};
                if (prevent_always_on_top_log_count_present1.fetch_add(1) < 3) {
                    LogInfo("IDXGISwapChain_Present1_Detour: Prevented always on top for window 0x%p", swapchain_hwnd);
                }
            }
        }
    }

    ::OnPresentFlags2(&PresentFlags, device_type);

    // Record per-frame FPS sample for background aggregation
    RecordFrameTime(FrameTimeMode::kPresent);

    // Get and cache frame statistics for refresh rate monitoring
    DXGI_FRAME_STATISTICS stats = {};
    if (SUCCEEDED(This->GetFrameStatistics(&stats))) {
        // Use memory_order_release to ensure the shared_ptr is fully constructed before other threads see it
        g_cached_frame_stats.store(std::make_shared<DXGI_FRAME_STATISTICS>(stats), std::memory_order_release);
        ::dxgi::fps_limiter::ProcessFrameStatistics(stats);
    }

    dx11_proxy::DX11ProxyManager::GetInstance().CopyFrameFromGameThread(This);

    if  (IDXGISwapChain_Present1_Original == nullptr) {
        LogError("IDXGISwapChain_Present1_Detour: IDXGISwapChain_Present1_Original is null");
        return This->Present1(SyncInterval, PresentFlags, pPresentParameters);
    }

    auto res = IDXGISwapChain_Present1_Original(This, SyncInterval, PresentFlags, pPresentParameters);

    // Query DXGI composition state (moved from ReShade present events)
    ::QueryDxgiCompositionState(This);

    // Signal refresh rate monitoring thread (after DWM flush)
    ::dxgi::fps_limiter::SignalRefreshRateMonitor();

    // Note: GPU completion measurement is now enqueued earlier in OnPresentUpdateBefore
    // (before flush_command_queue) for more accurate timing
//   dx11_proxy::DX11ProxyManager::GetInstance().CopyThreadLoop();

    // Get device from swapchain for latency manager
    ::OnPresentUpdateAfter2(device.Get(), device_type);

    return res;
}

// Hooked IDXGISwapChain::GetDesc function
HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetDesc_Detour(IDXGISwapChain *This, DXGI_SWAP_CHAIN_DESC *pDesc) {

    // Increment DXGI GetDesc counter
    g_dxgi_core_event_counters[DXGI_CORE_EVENT_GETDESC].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Call original function
    if (IDXGISwapChain_GetDesc_Original != nullptr) {
        HRESULT hr = IDXGISwapChain_GetDesc_Original(This, pDesc);
/*
        // Hide HDR capabilities if enabled
        if (SUCCEEDED(hr) && pDesc != nullptr && s_hide_hdr_capabilities.load()) {
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
                LogInfo("SwapChain Desc - Width: %u, Height: %u, Format: %u, RefreshRate: %u/%u, BufferCount: %u",
                       pDesc->BufferDesc.Width, pDesc->BufferDesc.Height, pDesc->BufferDesc.Format,
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
HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetDesc1_Detour(IDXGISwapChain1 *This, DXGI_SWAP_CHAIN_DESC1 *pDesc) {
    // Increment DXGI GetDesc1 counter
    g_dxgi_sc1_event_counters[DXGI_SC1_EVENT_GETDESC1].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Call original function
    if (IDXGISwapChain_GetDesc1_Original != nullptr) {
        HRESULT hr = IDXGISwapChain_GetDesc1_Original(This, pDesc);
/*
        // Hide HDR capabilities if enabled
        if (SUCCEEDED(hr) && pDesc != nullptr && s_hide_hdr_capabilities.load()) {
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
                LogInfo("SwapChain Desc1 - Width: %u, Height: %u, Format: %u, BufferCount: %u, SwapEffect: %u, Scaling: %u",
                       pDesc->Width, pDesc->Height, pDesc->Format,
                       pDesc->BufferCount, pDesc->SwapEffect, pDesc->Scaling);
                getdesc1_log_count++;
            }
        }
*/
        return hr;
    }

    // Fallback to direct call if hook failed
    return This->GetDesc1(pDesc);
}

// Hooked IDXGISwapChain3::CheckColorSpaceSupport function
HRESULT STDMETHODCALLTYPE IDXGISwapChain_CheckColorSpaceSupport_Detour(IDXGISwapChain3 *This, DXGI_COLOR_SPACE_TYPE ColorSpace, UINT *pColorSpaceSupport) {
    // Increment DXGI CheckColorSpaceSupport counter
    g_dxgi_sc3_event_counters[DXGI_SC3_EVENT_CHECKCOLORSPACESUPPORT].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Log the color space check (only on first few calls to avoid spam)
    static int checkcolorspace_log_count = 0;
    if (checkcolorspace_log_count < 3) {
        LogInfo("CheckColorSpaceSupport called for ColorSpace: %d", static_cast<int>(ColorSpace));
        checkcolorspace_log_count++;
    }

    // Call original function
    if (IDXGISwapChain_CheckColorSpaceSupport_Original != nullptr) {
        HRESULT hr = IDXGISwapChain_CheckColorSpaceSupport_Original(This, ColorSpace, pColorSpaceSupport);

        // Hide HDR capabilities if enabled
        if (SUCCEEDED(hr) && pColorSpaceSupport != nullptr && s_hide_hdr_capabilities.load()) {
            // Check if this is an HDR color space
            bool is_hdr_colorspace = false;
            switch (ColorSpace) {
                case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:      // HDR10
                case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:   // HDR10
                case DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020: // HDR10
                case DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709:    // HDR10
                case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:      // HDR10
                    is_hdr_colorspace = true;
                    break;
                default:
                    break;
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
                LogInfo("CheckColorSpaceSupport result: ColorSpace %d support = 0x%x",
                       static_cast<int>(ColorSpace), *pColorSpaceSupport);
                checkcolorspace_result_log_count++;
            }
        }

        return hr;
    }

    // Fallback to direct call if hook failed
    return This->CheckColorSpaceSupport(ColorSpace, pColorSpaceSupport);
}

// Hooked IDXGIFactory::CreateSwapChain function
HRESULT STDMETHODCALLTYPE IDXGIFactory_CreateSwapChain_Detour(IDXGIFactory *This, IUnknown *pDevice, DXGI_SWAP_CHAIN_DESC *pDesc, IDXGISwapChain **ppSwapChain) {
    // Increment DXGI Factory CreateSwapChain counter
    g_dxgi_factory_event_counters[DXGI_FACTORY_EVENT_CREATESWAPCHAIN].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Log the swapchain creation parameters (only on first few calls to avoid spam)
    static int createswapchain_log_count = 0;
    if (createswapchain_log_count < 3 && pDesc != nullptr) {
        LogInfo("IDXGIFactory::CreateSwapChain - Width: %u, Height: %u, Format: %u, BufferCount: %u, SwapEffect: %u, Windowed: %s",
               pDesc->BufferDesc.Width, pDesc->BufferDesc.Height, pDesc->BufferDesc.Format,
               pDesc->BufferCount, pDesc->SwapEffect, pDesc->Windowed ? "true" : "false");
        createswapchain_log_count++;
    }

    // Prevent always on top for swapchain window if enabled
    if (settings::g_developerTabSettings.prevent_always_on_top.GetValue() && pDesc != nullptr && pDesc->OutputWindow) {
        // Remove always on top styles from the window
        LONG_PTR current_style = GetWindowLongPtrW(pDesc->OutputWindow, GWL_EXSTYLE);
        if (current_style & (WS_EX_TOPMOST | WS_EX_TOOLWINDOW)) {
            LONG_PTR new_style = current_style & ~(WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
            SetWindowLongPtrW(pDesc->OutputWindow, GWL_EXSTYLE, new_style);
            LogInfo("IDXGIFactory_CreateSwapChain_Detour: Prevented always on top for swapchain window 0x%p", pDesc->OutputWindow);
        }
    }

    // Call original function
    if (IDXGIFactory_CreateSwapChain_Original != nullptr) {
        HRESULT hr = IDXGIFactory_CreateSwapChain_Original(This, pDevice, pDesc, ppSwapChain);

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

// Additional DXGI detour functions
HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetBuffer_Detour(IDXGISwapChain *This, UINT Buffer, REFIID riid, void **ppSurface) {
    g_dxgi_core_event_counters[DXGI_CORE_EVENT_GETBUFFER].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_GetBuffer_Original(This, Buffer, riid, ppSurface);
}

std::atomic<int> g_last_set_fullscreen_state{-1}; // -1 for not set, 0 for false, 1 for true
std::atomic<IDXGIOutput*> g_last_set_fullscreen_target{nullptr};
HRESULT STDMETHODCALLTYPE IDXGISwapChain_SetFullscreenState_Detour(IDXGISwapChain *This, BOOL Fullscreen, IDXGIOutput *pTarget) {


    g_dxgi_core_event_counters[DXGI_CORE_EVENT_SETFULLSCREENSTATE].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    if (Fullscreen == g_last_set_fullscreen_state.load() && pTarget == g_last_set_fullscreen_target.load()) {
        return S_OK;
    }

    g_last_set_fullscreen_target.store(pTarget);
    g_last_set_fullscreen_state.store(Fullscreen);

    // Check if fullscreen prevention is enabled and we're trying to go fullscreen
    if (settings::g_developerTabSettings.prevent_fullscreen.GetValue()) {
        return IDXGISwapChain_SetFullscreenState_Original(This, false, pTarget);
    }

    return IDXGISwapChain_SetFullscreenState_Original(This, Fullscreen, pTarget);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetFullscreenState_Detour(IDXGISwapChain *This, BOOL *pFullscreen, IDXGIOutput **ppTarget) {
    g_dxgi_core_event_counters[DXGI_CORE_EVENT_GETFULLSCREENSTATE].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    auto hr = IDXGISwapChain_GetFullscreenState_Original(This, pFullscreen, ppTarget);

    // NOTE: we assume that ppTarget is g_last_set_fullscreen_target.load()
    if (settings::g_developerTabSettings.prevent_fullscreen.GetValue() && g_last_set_fullscreen_state.load() != -1) {
        *pFullscreen = g_last_set_fullscreen_state.load();
    }

    return hr;
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_ResizeBuffers_Detour(IDXGISwapChain *This, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    g_dxgi_core_event_counters[DXGI_CORE_EVENT_RESIZEBUFFERS].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_ResizeBuffers_Original(This, BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_ResizeTarget_Detour(IDXGISwapChain *This, const DXGI_MODE_DESC *pNewTargetParameters) {
    g_dxgi_core_event_counters[DXGI_CORE_EVENT_RESIZETARGET].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_ResizeTarget_Original(This, pNewTargetParameters);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetContainingOutput_Detour(IDXGISwapChain *This, IDXGIOutput **ppOutput) {
    g_dxgi_core_event_counters[DXGI_CORE_EVENT_GETCONTAININGOUTPUT].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    HRESULT hr = IDXGISwapChain_GetContainingOutput_Original(This, ppOutput);

    // Hook the IDXGIOutput if we successfully got one
    if (SUCCEEDED(hr) && ppOutput && *ppOutput) {
        HookIDXGIOutput(*ppOutput);
    }

    return hr;
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetFrameStatistics_Detour(IDXGISwapChain *This, DXGI_FRAME_STATISTICS *pStats) {
    g_dxgi_core_event_counters[DXGI_CORE_EVENT_GETFRAMESTATISTICS].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_GetFrameStatistics_Original(This, pStats);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetLastPresentCount_Detour(IDXGISwapChain *This, UINT *pLastPresentCount) {
    g_dxgi_core_event_counters[DXGI_CORE_EVENT_GETLASTPRESENTCOUNT].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_GetLastPresentCount_Original(This, pLastPresentCount);
}

// IDXGISwapChain1 detour functions
HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetFullscreenDesc_Detour(IDXGISwapChain1 *This, DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pDesc) {
    g_dxgi_sc1_event_counters[DXGI_SC1_EVENT_GETFULLSCREENDESC].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_GetFullscreenDesc_Original(This, pDesc);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetHwnd_Detour(IDXGISwapChain1 *This, HWND *pHwnd) {
    g_dxgi_sc1_event_counters[DXGI_SC1_EVENT_GETHWND].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    HRESULT hr = IDXGISwapChain_GetHwnd_Original(This, pHwnd);

    // Prevent always on top for the returned window handle if enabled
    if (SUCCEEDED(hr) && settings::g_developerTabSettings.prevent_always_on_top.GetValue() && pHwnd && *pHwnd) {
        // Remove always on top styles from the window
        LONG_PTR current_style = GetWindowLongPtrW(*pHwnd, GWL_EXSTYLE);
        if (current_style & (WS_EX_TOPMOST | WS_EX_TOOLWINDOW)) {
            LONG_PTR new_style = current_style & ~(WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
            SetWindowLongPtrW(*pHwnd, GWL_EXSTYLE, new_style);
            LogInfo("IDXGISwapChain_GetHwnd_Detour: Prevented always on top for window 0x%p", *pHwnd);
        }
    }

    return hr;
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetCoreWindow_Detour(IDXGISwapChain1 *This, REFIID refiid, void **ppUnk) {
    g_dxgi_sc1_event_counters[DXGI_SC1_EVENT_GETCOREWINDOW].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_GetCoreWindow_Original(This, refiid, ppUnk);
}

BOOL STDMETHODCALLTYPE IDXGISwapChain_IsTemporaryMonoSupported_Detour(IDXGISwapChain1 *This) {
    g_dxgi_sc1_event_counters[DXGI_SC1_EVENT_ISTEMPORARYMONOSUPPORTED].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_IsTemporaryMonoSupported_Original(This);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetRestrictToOutput_Detour(IDXGISwapChain1 *This, IDXGIOutput **ppRestrictToOutput) {
    g_dxgi_sc1_event_counters[DXGI_SC1_EVENT_GETRESTRICTTOOUTPUT].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_GetRestrictToOutput_Original(This, ppRestrictToOutput);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_SetBackgroundColor_Detour(IDXGISwapChain1 *This, const DXGI_RGBA *pColor) {
    g_dxgi_sc1_event_counters[DXGI_SC1_EVENT_SETBACKGROUNDCOLOR].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_SetBackgroundColor_Original(This, pColor);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetBackgroundColor_Detour(IDXGISwapChain1 *This, DXGI_RGBA *pColor) {
    g_dxgi_sc1_event_counters[DXGI_SC1_EVENT_GETBACKGROUNDCOLOR].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_GetBackgroundColor_Original(This, pColor);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_SetRotation_Detour(IDXGISwapChain1 *This, DXGI_MODE_ROTATION Rotation) {
    g_dxgi_sc1_event_counters[DXGI_SC1_EVENT_SETROTATION].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_SetRotation_Original(This, Rotation);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetRotation_Detour(IDXGISwapChain1 *This, DXGI_MODE_ROTATION *pRotation) {
    g_dxgi_sc1_event_counters[DXGI_SC1_EVENT_GETROTATION].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_GetRotation_Original(This, pRotation);
}

// IDXGISwapChain2 detour functions
HRESULT STDMETHODCALLTYPE IDXGISwapChain_SetSourceSize_Detour(IDXGISwapChain2 *This, UINT Width, UINT Height) {
    g_dxgi_sc2_event_counters[DXGI_SC2_EVENT_SETSOURCESIZE].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_SetSourceSize_Original(This, Width, Height);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetSourceSize_Detour(IDXGISwapChain2 *This, UINT *pWidth, UINT *pHeight) {
    g_dxgi_sc2_event_counters[DXGI_SC2_EVENT_GETSOURCESIZE].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_GetSourceSize_Original(This, pWidth, pHeight);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_SetMaximumFrameLatency_Detour(IDXGISwapChain2 *This, UINT MaxLatency) {
    g_dxgi_sc2_event_counters[DXGI_SC2_EVENT_SETMAXIMUMFRAMELATENCY].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_SetMaximumFrameLatency_Original(This, MaxLatency);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetMaximumFrameLatency_Detour(IDXGISwapChain2 *This, UINT *pMaxLatency) {
    g_dxgi_sc2_event_counters[DXGI_SC2_EVENT_GETMAXIMUMFRAMELATENCY].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_GetMaximumFrameLatency_Original(This, pMaxLatency);
}

HANDLE STDMETHODCALLTYPE IDXGISwapChain_GetFrameLatencyWaitableObject_Detour(IDXGISwapChain2 *This) {
    g_dxgi_sc2_event_counters[DXGI_SC2_EVENT_GETFRAMELATENCYWAIABLEOBJECT].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_GetFrameLatencyWaitableObject_Original(This);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_SetMatrixTransform_Detour(IDXGISwapChain2 *This, const DXGI_MATRIX_3X2_F *pMatrix) {
    g_dxgi_sc2_event_counters[DXGI_SC2_EVENT_SETMATRIXTRANSFORM].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_SetMatrixTransform_Original(This, pMatrix);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_GetMatrixTransform_Detour(IDXGISwapChain2 *This, DXGI_MATRIX_3X2_F *pMatrix) {
    g_dxgi_sc2_event_counters[DXGI_SC2_EVENT_GETMATRIXTRANSFORM].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_GetMatrixTransform_Original(This, pMatrix);
}

// IDXGISwapChain3 detour functions
UINT STDMETHODCALLTYPE IDXGISwapChain_GetCurrentBackBufferIndex_Detour(IDXGISwapChain3 *This) {
    g_dxgi_sc3_event_counters[DXGI_SC3_EVENT_GETCURRENTBACKBUFFERINDEX].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_GetCurrentBackBufferIndex_Original(This);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_SetColorSpace1_Detour(IDXGISwapChain3 *This, DXGI_COLOR_SPACE_TYPE ColorSpace) {
    g_dxgi_sc3_event_counters[DXGI_SC3_EVENT_SETCOLORSPACE1].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_SetColorSpace1_Original(This, ColorSpace);
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_ResizeBuffers1_Detour(IDXGISwapChain3 *This, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format, UINT SwapChainFlags, const UINT *pCreationNodeMask, IUnknown *const *ppPresentQueue) {
    g_dxgi_sc3_event_counters[DXGI_SC3_EVENT_RESIZEBUFFERS1].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);
    return IDXGISwapChain_ResizeBuffers1_Original(This, BufferCount, Width, Height, Format, SwapChainFlags, pCreationNodeMask, ppPresentQueue);
}

// IDXGISwapChain4 detour functions
HRESULT STDMETHODCALLTYPE IDXGISwapChain_SetHDRMetaData_Detour(IDXGISwapChain4 *This, DXGI_HDR_METADATA_TYPE Type, UINT Size, void *pMetaData) {
    // Increment DXGI SetHDRMetaData counter
    g_dxgi_sc4_event_counters[DXGI_SC4_EVENT_SETHDRMETADATA].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Log the HDR metadata call (only on first few calls to avoid spam)
    static int sethdrmetadata_log_count = 0;
    if (sethdrmetadata_log_count < 3) {
        LogInfo("SetHDRMetaData called - Type: %d, Size: %u", static_cast<int>(Type), Size);
        sethdrmetadata_log_count++;
    }

    // Call original function
    if (IDXGISwapChain_SetHDRMetaData_Original != nullptr) {
        return IDXGISwapChain_SetHDRMetaData_Original(This, Type, Size, pMetaData);
    }

    // Fallback to direct call if hook failed
    return This->SetHDRMetaData(Type, Size, pMetaData);
}

// Hooked IDXGIOutput functions
HRESULT STDMETHODCALLTYPE IDXGIOutput_SetGammaControl_Detour(IDXGIOutput *This, const DXGI_GAMMA_CONTROL *pArray) {
    // Increment DXGI Output SetGammaControl counter
    g_dxgi_output_event_counters[DXGI_OUTPUT_EVENT_SETGAMMACONTROL].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Log the SetGammaControl call (only on first few calls to avoid spam)
    static int setgammacontrol_log_count = 0;
    if (setgammacontrol_log_count < 3) {
        LogInfo("IDXGIOutput::SetGammaControl called");
        setgammacontrol_log_count++;
    }

    // Call original function
    if (IDXGIOutput_SetGammaControl_Original != nullptr) {
        return IDXGIOutput_SetGammaControl_Original(This, pArray);
    }

    // Fallback to direct call if hook failed
    return This->SetGammaControl(pArray);
}

HRESULT STDMETHODCALLTYPE IDXGIOutput_GetGammaControl_Detour(IDXGIOutput *This, DXGI_GAMMA_CONTROL *pArray) {
    // Increment DXGI Output GetGammaControl counter
    g_dxgi_output_event_counters[DXGI_OUTPUT_EVENT_GETGAMMACONTROL].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Log the GetGammaControl call (only on first few calls to avoid spam)
    static int getgammacontrol_log_count = 0;
    if (getgammacontrol_log_count < 3) {
        LogInfo("IDXGIOutput::GetGammaControl called");
        getgammacontrol_log_count++;
    }

    // Call original function
    if (IDXGIOutput_GetGammaControl_Original != nullptr) {
        return IDXGIOutput_GetGammaControl_Original(This, pArray);
    }

    // Fallback to direct call if hook failed
    return This->GetGammaControl(pArray);
}

HRESULT STDMETHODCALLTYPE IDXGIOutput_GetDesc_Detour(IDXGIOutput *This, DXGI_OUTPUT_DESC *pDesc) {
    // Increment DXGI Output GetDesc counter
    g_dxgi_output_event_counters[DXGI_OUTPUT_EVENT_GETDESC].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Log the GetDesc call (only on first few calls to avoid spam)
    static int getdesc_log_count = 0;
    if (getdesc_log_count < 3) {
        LogInfo("IDXGIOutput::GetDesc called");
        getdesc_log_count++;
    }

    // Call original function
    if (IDXGIOutput_GetDesc_Original != nullptr) {
        return IDXGIOutput_GetDesc_Original(This, pDesc);
    }

    // Fallback to direct call if hook failed
    return This->GetDesc(pDesc);
}

// Hooked IDXGIOutput6::GetDesc1 function
HRESULT STDMETHODCALLTYPE IDXGIOutput6_GetDesc1_Detour(IDXGIOutput6 *This, DXGI_OUTPUT_DESC1 *pDesc) {
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

    // Hide HDR capabilities if enabled (similar to Special-K's approach)
    if (SUCCEEDED(hr) && pDesc != nullptr && s_hide_hdr_capabilities.load()) {
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
    IDXGISwapChain *g_hooked_swapchain = nullptr;

    // Track hooked IDXGIOutput objects to avoid duplicate hooking
    std::atomic<bool> g_dxgi_output_hooks_installed{false};
} // namespace

// Hook IDXGIOutput methods
bool HookIDXGIOutput(IDXGIOutput *output) {
    if (!output) {
        return false;
    }

    // Check if we already hooked this output
    static std::atomic<bool> output_hooked{false};
    if (output_hooked.load()) {
        return true;
    }

    // Get the vtable
    void **vtable = *(void ***)output;

    // IDXGIOutput vtable layout:
    // [0-2]   IUnknown methods
    // [3-5]   IDXGIObject methods
    // [6-7]   IDXGIDeviceSubObject methods
    // [8]     IDXGIOutput::GetDesc
    // [9]     IDXGIOutput::GetDisplayModeList
    // [10]    IDXGIOutput::FindClosestMatchingMode
    // [11]    IDXGIOutput::WaitForVBlank
    // [12]    IDXGIOutput::TakeOwnership
    // [13]    IDXGIOutput::ReleaseOwnership
    // [14]    IDXGIOutput::GetGammaControlCapabilities
    // [15]    IDXGIOutput::SetGammaControl
    // [16]    IDXGIOutput::GetGammaControl
    // [17]    IDXGIOutput::SetDisplaySurface
    // [18]    IDXGIOutput::GetDisplaySurfaceData
    // [19]    IDXGIOutput::GetFrameStatistics

    LogInfo("Hooking IDXGIOutput methods");

    // Hook SetGammaControl (index 15)
    if (IsVTableEntryValid(vtable, 15)) {
        if (MH_CreateHook(vtable[15], IDXGIOutput_SetGammaControl_Detour, (LPVOID *)&IDXGIOutput_SetGammaControl_Original) != MH_OK) {
            LogError("Failed to create IDXGIOutput::SetGammaControl hook");
        } else {
            LogInfo("IDXGIOutput::SetGammaControl hook created successfully");
        }
    }

    // Hook GetGammaControl (index 16)
    if (IsVTableEntryValid(vtable, 16)) {
        if (MH_CreateHook(vtable[16], IDXGIOutput_GetGammaControl_Detour, (LPVOID *)&IDXGIOutput_GetGammaControl_Original) != MH_OK) {
            LogError("Failed to create IDXGIOutput::GetGammaControl hook");
        } else {
            LogInfo("IDXGIOutput::GetGammaControl hook created successfully");
        }
    }

    // Hook GetDesc (index 8)
    if (IsVTableEntryValid(vtable, 8)) {
        if (MH_CreateHook(vtable[8], IDXGIOutput_GetDesc_Detour, (LPVOID *)&IDXGIOutput_GetDesc_Original) != MH_OK) {
            LogError("Failed to create IDXGIOutput::GetDesc hook");
        } else {
            LogInfo("IDXGIOutput::GetDesc hook created successfully");
        }
    }

    // Hook IDXGIOutput6::GetDesc1 (index 27) - for HDR hiding
    // First, QueryInterface for IDXGIOutput6 to get the extended interface
    Microsoft::WRL::ComPtr<IDXGIOutput6> output6;
    if (SUCCEEDED(output->QueryInterface(IID_PPV_ARGS(&output6))) && output6 != nullptr) {
        void **output6_vtable = *(void ***)output6.Get();

        // IDXGIOutput6::GetDesc1 is at index 27 in the vtable
        if (IsVTableEntryValid(output6_vtable, 27)) {
            if (MH_CreateHook(output6_vtable[27], IDXGIOutput6_GetDesc1_Detour, (LPVOID *)&IDXGIOutput6_GetDesc1_Original) != MH_OK) {
                LogError("Failed to create IDXGIOutput6::GetDesc1 hook");
            } else {
                if (MH_EnableHook(output6_vtable[27]) != MH_OK) {
                    LogError("Failed to enable IDXGIOutput6::GetDesc1 hook");
                } else {
                    LogInfo("IDXGIOutput6::GetDesc1 hook created and enabled successfully");
                }
            }
        } else {
            LogWarn("IDXGIOutput6::GetDesc1 vtable entry (index 27) is not valid");
        }
    } else {
        LogInfo("IDXGIOutput6 interface not available, skipping GetDesc1 hook");
    }

    output_hooked.store(true);
    g_dxgi_output_hooks_installed.store(true);

    return true;
}

// VTable hooking functions
bool HookFactoryVTable(IDXGIFactory *factory);


bool HookSwapchainNative(IDXGISwapChain *swapchain) {

    LogInfo("Hooking swapchain native: 0x%p (WIP - Not implemented)", swapchain);

    return true;
}

// Hook a specific swapchain's vtable
bool HookSwapchain(IDXGISwapChain *swapchain) {
    if (g_swapchainTrackingManager.IsSwapchainTracked(swapchain)) {
        return false;
    }
    LogInfo("Hooking swapchain: 0x%p", swapchain);
    static bool installed = false;
    if (installed) {
        LogInfo("IDXGISwapChain hooks already installed");
        return true;
    }
    installed = true;

    g_hooked_swapchain = swapchain;
    g_swapchainTrackingManager.AddSwapchain(swapchain);

    // Get the vtable
    void **vtable = *(void ***)swapchain;
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
    // Check if LoadLibrary hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(display_commanderhooks::HookType::DXGI_SWAPCHAIN)) {
        LogInfo("HookSwapchain installation suppressed by user setting");
        return false;
    }


    //minhook initialization
    MH_STATUS init_status = SafeInitializeMinHook(display_commanderhooks::HookType::DXGI_SWAPCHAIN);
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for DXGI hooks - Status: %d", init_status);
        return false;
    }
    if (init_status == MH_ERROR_ALREADY_INITIALIZED) {
        LogInfo("MinHook already initialized, proceeding with DXGI hooks");
    }

    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(display_commanderhooks::HookType::DXGI_SWAPCHAIN);

    // Detect swapchain interface version for safe vtable access
    int interfaceVersion = GetSwapchainInterfaceVersion(swapchain);
    LogInfo("Detected swapchain interface version: %d", interfaceVersion);

    // ============================================================================
    // GROUP 0: IDXGISwapChain (Base Interface) - Indices 8-17
    // ============================================================================
    LogInfo("Hooking IDXGISwapChain methods (indices 8-17)");


    // Hook Present (index 8) - Critical method, always present
    if (MH_CreateHook(vtable[8], IDXGISwapChain_Present_Detour, (LPVOID *)&IDXGISwapChain_Present_Original) != MH_OK) {
        MH_DisableHook(vtable[8]);
        LogError("Failed to create IDXGISwapChain::Present hook");
        return false;
    }

    // Hook other IDXGISwapChain methods (9-11, 13-17) with safe vtable access
    if (IsVTableEntryValid(vtable, 9)) {
        if (MH_CreateHook(vtable[9], IDXGISwapChain_GetBuffer_Detour, (LPVOID *)&IDXGISwapChain_GetBuffer_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain::GetBuffer hook");
        }
    }
    if (IsVTableEntryValid(vtable, 10)) {
        if (MH_CreateHook(vtable[10], IDXGISwapChain_SetFullscreenState_Detour, (LPVOID *)&IDXGISwapChain_SetFullscreenState_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain::SetFullscreenState hook");
        }
    }
    if (IsVTableEntryValid(vtable, 11)) {
        if (MH_CreateHook(vtable[11], IDXGISwapChain_GetFullscreenState_Detour, (LPVOID *)&IDXGISwapChain_GetFullscreenState_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain::GetFullscreenState hook");
        }
    }
    // Hook GetDesc (index 12) - Always present in base interface
    if (MH_CreateHook(vtable[12], IDXGISwapChain_GetDesc_Detour, (LPVOID *)&IDXGISwapChain_GetDesc_Original) != MH_OK) {
        LogError("Failed to create IDXGISwapChain::GetDesc hook");
        // Don't return false, this is not critical
    }
    if (IsVTableEntryValid(vtable, 13)) {
        if (MH_CreateHook(vtable[13], IDXGISwapChain_ResizeBuffers_Detour, (LPVOID *)&IDXGISwapChain_ResizeBuffers_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain::ResizeBuffers hook");
        }
    }
    if (IsVTableEntryValid(vtable, 14)) {
        if (MH_CreateHook(vtable[14], IDXGISwapChain_ResizeTarget_Detour, (LPVOID *)&IDXGISwapChain_ResizeTarget_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain::ResizeTarget hook");
        }
    }
    if (IsVTableEntryValid(vtable, 15)) {
        if (MH_CreateHook(vtable[15], IDXGISwapChain_GetContainingOutput_Detour, (LPVOID *)&IDXGISwapChain_GetContainingOutput_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain::GetContainingOutput hook");
        }
    }
    if (IsVTableEntryValid(vtable, 16)) {
        if (MH_CreateHook(vtable[16], IDXGISwapChain_GetFrameStatistics_Detour, (LPVOID *)&IDXGISwapChain_GetFrameStatistics_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain::GetFrameStatistics hook");
        }
    }
    if (IsVTableEntryValid(vtable, 17)) {
        if (MH_CreateHook(vtable[17], IDXGISwapChain_GetLastPresentCount_Detour, (LPVOID *)&IDXGISwapChain_GetLastPresentCount_Original) != MH_OK) {
            LogError("Failed to create IDXGISwapChain::GetLastPresentCount hook");
        }
    }

    // ============================================================================
    // GROUP 1: IDXGISwapChain1 (Extended Interface) - Indices 18-28
    // ============================================================================
    if (interfaceVersion >= 1) {
        LogInfo("Hooking IDXGISwapChain1 methods (indices 18-28)");

        // Hook GetDesc1 (index 18) - Critical for IDXGISwapChain1
        if (IsVTableEntryValid(vtable, 18)) {
            if (MH_CreateHook(vtable[18], IDXGISwapChain_GetDesc1_Detour, (LPVOID *)&IDXGISwapChain_GetDesc1_Original) != MH_OK) {
                LogError("Failed to create IDXGISwapChain1::GetDesc1 hook");
                return false;
            }
        }

        // Hook Present1 (index 22) - Critical for IDXGISwapChain1
        if (IsVTableEntryValid(vtable, 22)) {
            if (MH_CreateHook(vtable[22], IDXGISwapChain_Present1_Detour, (LPVOID *)&IDXGISwapChain_Present1_Original) != MH_OK) {
                LogError("Failed to create IDXGISwapChain1::Present1 hook");
                return false;
            }
        }

        // Hook other IDXGISwapChain1 methods (19-21, 23-28)
        if (IsVTableEntryValid(vtable, 19)) {
            if (MH_CreateHook(vtable[19], IDXGISwapChain_GetFullscreenDesc_Detour, (LPVOID *)&IDXGISwapChain_GetFullscreenDesc_Original) != MH_OK) {
                LogError("Failed to create IDXGISwapChain1::GetFullscreenDesc hook");
            }
        }
        if (IsVTableEntryValid(vtable, 20)) {
            if (MH_CreateHook(vtable[20], IDXGISwapChain_GetHwnd_Detour, (LPVOID *)&IDXGISwapChain_GetHwnd_Original) != MH_OK) {
                LogError("Failed to create IDXGISwapChain1::GetHwnd hook");
            }
        }
        if (IsVTableEntryValid(vtable, 21)) {
            if (MH_CreateHook(vtable[21], IDXGISwapChain_GetCoreWindow_Detour, (LPVOID *)&IDXGISwapChain_GetCoreWindow_Original) != MH_OK) {
                LogError("Failed to create IDXGISwapChain1::GetCoreWindow hook");
            }
        }
        if (IsVTableEntryValid(vtable, 23)) {
            if (MH_CreateHook(vtable[23], IDXGISwapChain_IsTemporaryMonoSupported_Detour, (LPVOID *)&IDXGISwapChain_IsTemporaryMonoSupported_Original) != MH_OK) {
                LogError("Failed to create IDXGISwapChain1::IsTemporaryMonoSupported hook");
            }
        }
        if (IsVTableEntryValid(vtable, 24)) {
            if (MH_CreateHook(vtable[24], IDXGISwapChain_GetRestrictToOutput_Detour, (LPVOID *)&IDXGISwapChain_GetRestrictToOutput_Original) != MH_OK) {
                LogError("Failed to create IDXGISwapChain1::GetRestrictToOutput hook");
            }
        }
        if (IsVTableEntryValid(vtable, 25)) {
            if (MH_CreateHook(vtable[25], IDXGISwapChain_SetBackgroundColor_Detour, (LPVOID *)&IDXGISwapChain_SetBackgroundColor_Original) != MH_OK) {
                LogError("Failed to create IDXGISwapChain1::SetBackgroundColor hook");
            }
        }
        if (IsVTableEntryValid(vtable, 26)) {
            if (MH_CreateHook(vtable[26], IDXGISwapChain_GetBackgroundColor_Detour, (LPVOID *)&IDXGISwapChain_GetBackgroundColor_Original) != MH_OK) {
                LogError("Failed to create IDXGISwapChain1::GetBackgroundColor hook");
            }
        }
        if (IsVTableEntryValid(vtable, 27)) {
            if (MH_CreateHook(vtable[27], IDXGISwapChain_SetRotation_Detour, (LPVOID *)&IDXGISwapChain_SetRotation_Original) != MH_OK) {
                LogError("Failed to create IDXGISwapChain1::SetRotation hook");
            }
        }
        if (IsVTableEntryValid(vtable, 28)) {
            if (MH_CreateHook(vtable[28], IDXGISwapChain_GetRotation_Detour, (LPVOID *)&IDXGISwapChain_GetRotation_Original) != MH_OK) {
                LogError("Failed to create IDXGISwapChain1::GetRotation hook");
            }
        }
    } else {
        LogInfo("Skipping IDXGISwapChain1 methods - interface not supported");
    }

    // ============================================================================
    // GROUP 2: IDXGISwapChain2 (Extended Interface) - Indices 29-35
    // ============================================================================
    if (interfaceVersion >= 2) {
        LogInfo("Hooking IDXGISwapChain2 methods (indices 29-35)");

        // Hook IDXGISwapChain2 methods (29-35)
        if (IsVTableEntryValid(vtable, 29)) {
            if (MH_CreateHook(vtable[29], IDXGISwapChain_SetSourceSize_Detour, (LPVOID *)&IDXGISwapChain_SetSourceSize_Original) != MH_OK) {
                LogError("Failed to create IDXGISwapChain2::SetSourceSize hook");
            }
        }
        if (IsVTableEntryValid(vtable, 30)) {
            if (MH_CreateHook(vtable[30], IDXGISwapChain_GetSourceSize_Detour, (LPVOID *)&IDXGISwapChain_GetSourceSize_Original) != MH_OK) {
                LogError("Failed to create IDXGISwapChain2::GetSourceSize hook");
            }
        }
        if (IsVTableEntryValid(vtable, 31)) {
            if (MH_CreateHook(vtable[31], IDXGISwapChain_SetMaximumFrameLatency_Detour, (LPVOID *)&IDXGISwapChain_SetMaximumFrameLatency_Original) != MH_OK) {
                LogError("Failed to create IDXGISwapChain2::SetMaximumFrameLatency hook");
            }
        }
        if (IsVTableEntryValid(vtable, 32)) {
            if (MH_CreateHook(vtable[32], IDXGISwapChain_GetMaximumFrameLatency_Detour, (LPVOID *)&IDXGISwapChain_GetMaximumFrameLatency_Original) != MH_OK) {
                LogError("Failed to create IDXGISwapChain2::GetMaximumFrameLatency hook");
            }
        }
        if (IsVTableEntryValid(vtable, 33)) {
            if (MH_CreateHook(vtable[33], IDXGISwapChain_GetFrameLatencyWaitableObject_Detour, (LPVOID *)&IDXGISwapChain_GetFrameLatencyWaitableObject_Original) != MH_OK) {
                LogError("Failed to create IDXGISwapChain2::GetFrameLatencyWaitableObject hook");
            }
        }
        if (IsVTableEntryValid(vtable, 34)) {
            if (MH_CreateHook(vtable[34], IDXGISwapChain_SetMatrixTransform_Detour, (LPVOID *)&IDXGISwapChain_SetMatrixTransform_Original) != MH_OK) {
                LogError("Failed to create IDXGISwapChain2::SetMatrixTransform hook");
            }
        }
        if (IsVTableEntryValid(vtable, 35)) {
            if (MH_CreateHook(vtable[35], IDXGISwapChain_GetMatrixTransform_Detour, (LPVOID *)&IDXGISwapChain_GetMatrixTransform_Original) != MH_OK) {
                LogError("Failed to create IDXGISwapChain2::GetMatrixTransform hook");
            }
        }
    } else {
        LogInfo("Skipping IDXGISwapChain2 methods - interface not supported");
    }

    // ============================================================================
    // GROUP 3: IDXGISwapChain3 (Extended Interface) - Indices 36-39
    // ============================================================================
    if (interfaceVersion >= 3) {
        LogInfo("Hooking IDXGISwapChain3 methods (indices 36-39)");

        // Hook IDXGISwapChain3 methods (36-39) with extra safety for high indices
        if (IsVTableEntryValid(vtable, 36)) {
            if (MH_CreateHook(vtable[36], IDXGISwapChain_GetCurrentBackBufferIndex_Detour, (LPVOID *)&IDXGISwapChain_GetCurrentBackBufferIndex_Original) != MH_OK) {
                LogError("Failed to create IDXGISwapChain3::GetCurrentBackBufferIndex hook");
            }
        }

        // Hook CheckColorSpaceSupport (index 37) - The method you were concerned about
        if (IsVTableEntryValid(vtable, 37)) {
            if (MH_CreateHook(vtable[37], IDXGISwapChain_CheckColorSpaceSupport_Detour, (LPVOID *)&IDXGISwapChain_CheckColorSpaceSupport_Original) != MH_OK) {
                LogError("Failed to create IDXGISwapChain3::CheckColorSpaceSupport hook");
                // Don't return false, this is not critical for basic functionality
            }
        }

        if (IsVTableEntryValid(vtable, 38)) {
            if (MH_CreateHook(vtable[38], IDXGISwapChain_SetColorSpace1_Detour, (LPVOID *)&IDXGISwapChain_SetColorSpace1_Original) != MH_OK) {
                LogError("Failed to create IDXGISwapChain3::SetColorSpace1 hook");
            }
        }
        if (IsVTableEntryValid(vtable, 39)) {
            if (MH_CreateHook(vtable[39], IDXGISwapChain_ResizeBuffers1_Detour, (LPVOID *)&IDXGISwapChain_ResizeBuffers1_Original) != MH_OK) {
                LogError("Failed to create IDXGISwapChain3::ResizeBuffers1 hook");
            }
        }
    } else {
        LogInfo("Skipping IDXGISwapChain3 methods - interface not supported");
    }

    // ============================================================================
    // GROUP 4: IDXGISwapChain4 (Extended Interface) - Indices 40+
    // ============================================================================
    if (interfaceVersion >= 4) {
        LogInfo("Hooking IDXGISwapChain4 methods (indices 40+)");

        // Hook SetHDRMetaData (index 40) - HDR metadata setting
        if (IsVTableEntryValid(vtable, 40)) {
            if (MH_CreateHook(vtable[40], IDXGISwapChain_SetHDRMetaData_Detour, (LPVOID *)&IDXGISwapChain_SetHDRMetaData_Original) != MH_OK) {
                LogError("Failed to create IDXGISwapChain4::SetHDRMetaData hook");
                // Don't return false, this is not critical for basic functionality
            }
        }
    } else {
        LogInfo("Skipping IDXGISwapChain4 methods - interface not supported");
    }

    // ============================================================================
    // ENABLE ALL HOOKS - Organized by Interface Version
    // ============================================================================
    LogInfo("Enabling all created hooks...");

    // Enable GROUP 0: IDXGISwapChain hooks (always enabled)
    if (IsVTableEntryValid(vtable, 8) && MH_EnableHook(vtable[8]) != MH_OK) {
        LogError("Failed to enable IDXGISwapChain::Present hook");
    }

    if (IsVTableEntryValid(vtable, 9)) MH_EnableHook(vtable[9]);
    if (IsVTableEntryValid(vtable, 10)) MH_EnableHook(vtable[10]);
    if (IsVTableEntryValid(vtable, 11)) MH_EnableHook(vtable[11]);
    if (IsVTableEntryValid(vtable, 12)) MH_EnableHook(vtable[12]);
    if (IsVTableEntryValid(vtable, 13)) MH_EnableHook(vtable[13]);
    if (IsVTableEntryValid(vtable, 14)) MH_EnableHook(vtable[14]);
    if (IsVTableEntryValid(vtable, 15)) MH_EnableHook(vtable[15]);
    if (IsVTableEntryValid(vtable, 16)) MH_EnableHook(vtable[16]);
    if (IsVTableEntryValid(vtable, 17)) MH_EnableHook(vtable[17]);


    // Enable GROUP 1: IDXGISwapChain1 hooks (if supported)
    if (interfaceVersion >= 1) {
        if (IsVTableEntryValid(vtable, 18)) MH_EnableHook(vtable[18]);
        if (IsVTableEntryValid(vtable, 19)) MH_EnableHook(vtable[19]);
        if (IsVTableEntryValid(vtable, 20)) MH_EnableHook(vtable[20]);
        if (IsVTableEntryValid(vtable, 21)) MH_EnableHook(vtable[21]);
        if (IsVTableEntryValid(vtable, 22)) MH_EnableHook(vtable[22]);
        if (IsVTableEntryValid(vtable, 23)) MH_EnableHook(vtable[23]);
        if (IsVTableEntryValid(vtable, 24)) MH_EnableHook(vtable[24]);
        if (IsVTableEntryValid(vtable, 25)) MH_EnableHook(vtable[25]);
        if (IsVTableEntryValid(vtable, 26)) MH_EnableHook(vtable[26]);
        if (IsVTableEntryValid(vtable, 27)) MH_EnableHook(vtable[27]);
        if (IsVTableEntryValid(vtable, 28)) MH_EnableHook(vtable[28]);
    }

    // Enable GROUP 2: IDXGISwapChain2 hooks (if supported)
    if (interfaceVersion >= 2) {
        if (IsVTableEntryValid(vtable, 29)) MH_EnableHook(vtable[29]);
        if (IsVTableEntryValid(vtable, 30)) MH_EnableHook(vtable[30]);
        if (IsVTableEntryValid(vtable, 31)) MH_EnableHook(vtable[31]);
        if (IsVTableEntryValid(vtable, 32)) MH_EnableHook(vtable[32]);
        if (IsVTableEntryValid(vtable, 33)) MH_EnableHook(vtable[33]);
        if (IsVTableEntryValid(vtable, 34)) MH_EnableHook(vtable[34]);
        if (IsVTableEntryValid(vtable, 35)) MH_EnableHook(vtable[35]);
    }

    // Enable GROUP 3: IDXGISwapChain3 hooks (if supported)
    if (interfaceVersion >= 3) {
        if (IsVTableEntryValid(vtable, 36)) MH_EnableHook(vtable[36]);
        if (IsVTableEntryValid(vtable, 37)) MH_EnableHook(vtable[37]);
        if (IsVTableEntryValid(vtable, 38)) MH_EnableHook(vtable[38]);
        if (IsVTableEntryValid(vtable, 39)) MH_EnableHook(vtable[39]);
    }

    // Enable GROUP 4: IDXGISwapChain4 hooks (if supported)
    if (interfaceVersion >= 4) {
        if (IsVTableEntryValid(vtable, 40)) MH_EnableHook(vtable[40]);
    }

    // ============================================================================
    // BUILD SUCCESS MESSAGE - Organized by Interface Version
    // ============================================================================
    std::string hook_message = "Successfully hooked DXGI methods for interface version " + std::to_string(interfaceVersion) + ": ";

    // GROUP 0: IDXGISwapChain methods
    hook_message += "Present, GetDesc";
    if (IsVTableEntryValid(vtable, 9)) hook_message += ", GetBuffer";
    if (IsVTableEntryValid(vtable, 10)) hook_message += ", SetFullscreenState";
    if (IsVTableEntryValid(vtable, 11)) hook_message += ", GetFullscreenState";
    if (IsVTableEntryValid(vtable, 13)) hook_message += ", ResizeBuffers";
    if (IsVTableEntryValid(vtable, 14)) hook_message += ", ResizeTarget";
    if (IsVTableEntryValid(vtable, 15)) hook_message += ", GetContainingOutput";
    if (IsVTableEntryValid(vtable, 16)) hook_message += ", GetFrameStatistics";
    if (IsVTableEntryValid(vtable, 17)) hook_message += ", GetLastPresentCount";

    // GROUP 1: IDXGISwapChain1 methods
    if (interfaceVersion >= 1) {
        if (IsVTableEntryValid(vtable, 18)) hook_message += ", GetDesc1";
        if (IsVTableEntryValid(vtable, 19)) hook_message += ", GetFullscreenDesc";
        if (IsVTableEntryValid(vtable, 20)) hook_message += ", GetHwnd";
        if (IsVTableEntryValid(vtable, 21)) hook_message += ", GetCoreWindow";
        if (IsVTableEntryValid(vtable, 22)) hook_message += ", Present1";
        if (IsVTableEntryValid(vtable, 23)) hook_message += ", IsTemporaryMonoSupported";
        if (IsVTableEntryValid(vtable, 24)) hook_message += ", GetRestrictToOutput";
        if (IsVTableEntryValid(vtable, 25)) hook_message += ", SetBackgroundColor";
        if (IsVTableEntryValid(vtable, 26)) hook_message += ", GetBackgroundColor";
        if (IsVTableEntryValid(vtable, 27)) hook_message += ", SetRotation";
        if (IsVTableEntryValid(vtable, 28)) hook_message += ", GetRotation";
    }

    // GROUP 2: IDXGISwapChain2 methods
    if (interfaceVersion >= 2) {
        if (IsVTableEntryValid(vtable, 29)) hook_message += ", SetSourceSize";
        if (IsVTableEntryValid(vtable, 30)) hook_message += ", GetSourceSize";
        if (IsVTableEntryValid(vtable, 31)) hook_message += ", SetMaximumFrameLatency";
        if (IsVTableEntryValid(vtable, 32)) hook_message += ", GetMaximumFrameLatency";
        if (IsVTableEntryValid(vtable, 33)) hook_message += ", GetFrameLatencyWaitableObject";
        if (IsVTableEntryValid(vtable, 34)) hook_message += ", SetMatrixTransform";
        if (IsVTableEntryValid(vtable, 35)) hook_message += ", GetMatrixTransform";
    }

    // GROUP 3: IDXGISwapChain3 methods
    if (interfaceVersion >= 3) {
        if (IsVTableEntryValid(vtable, 36)) hook_message += ", GetCurrentBackBufferIndex";
        if (IsVTableEntryValid(vtable, 37)) hook_message += ", CheckColorSpaceSupport";
        if (IsVTableEntryValid(vtable, 38)) hook_message += ", SetColorSpace1";
        if (IsVTableEntryValid(vtable, 39)) hook_message += ", ResizeBuffers1";
    }

    // GROUP 4: IDXGISwapChain4 methods
    if (interfaceVersion >= 4) {
        if (IsVTableEntryValid(vtable, 40)) hook_message += ", SetHDRMetaData";
    }

    hook_message += " for swapchain: 0x%p";
    LogInfo(hook_message.c_str(), swapchain);

    return true;
}

// Hook a specific factory's vtable
bool HookFactoryVTable(IDXGIFactory *factory) {
    if (true) {
        LogInfo("DXGI factory hooking suppressed by user setting");
        return false;
    }
    if (factory == nullptr) {
        return false;
    }

    // Check if we've already hooked the CreateSwapChain vtable
    if (g_createswapchain_vtable_hooked.load()) {
        LogInfo("IDXGIFactory::CreateSwapChain vtable already hooked, skipping");
        return true;
    }

    // Get the vtable
    void **vtable = *(void ***)factory;

    /*
     * IDXGIFactory VTable Layout Reference
     * ===================================
     *
     * IDXGIFactory inherits from IDXGIObject, which inherits from IUnknown.
     * The vtable contains all methods from the inheritance chain.
     *
     * VTable Indices 0-5:
     * [0-2]   IUnknown methods (QueryInterface, AddRef, Release)
     * [3-5]   IDXGIObject methods (SetPrivateData, SetPrivateDataInterface, GetPrivateData)
     * [6]     IDXGIObject methods (GetParent)
     * [7-11]  IDXGIFactory methods (EnumAdapters, MakeWindowAssociation, GetWindowAssociation, CreateSwapChain, CreateSoftwareAdapter)
     *
     * CreateSwapChain is at index 10 in the IDXGIFactory vtable.
     */

    // Hook CreateSwapChain (index 10 in IDXGIFactory vtable)
    if (vtable[10] != nullptr) {
        LogInfo("Attempting to hook IDXGIFactory::CreateSwapChain at vtable[10] = 0x%p", vtable[10]);

        // Check if we've already set the original function pointer (indicates already hooked)
        if (IDXGIFactory_CreateSwapChain_Original != nullptr) {
            LogWarn("IDXGIFactory::CreateSwapChain already hooked, skipping");
            g_createswapchain_vtable_hooked.store(true);
            return true; // Consider this success since it's already hooked
        }

        if (!CreateAndEnableHook(vtable[10], IDXGIFactory_CreateSwapChain_Detour, (LPVOID *)&IDXGIFactory_CreateSwapChain_Original, "IDXGIFactory::CreateSwapChain")) {
            LogError("Failed to create and enable IDXGIFactory::CreateSwapChain hook");

            return false;
        }

        LogInfo("Successfully hooked IDXGIFactory::CreateSwapChain for factory: 0x%p", factory);
        g_createswapchain_vtable_hooked.store(true);
        return true;
    }

    LogError("IDXGIFactory::CreateSwapChain method not found in vtable");
    return false;
}

// Public function to hook a factory when it's created
bool HookFactory(IDXGIFactory *factory) {
    return HookFactoryVTable(factory);
}


// Record the native swapchain used in OnPresentUpdateBefore
void RecordPresentUpdateSwapchain(IDXGISwapChain *swapchain) {
    g_last_present_update_swapchain.store(swapchain);
}

// Swapchain tracking management functions
bool IsSwapchainTracked(IDXGISwapChain *swapchain) {
    return g_swapchainTrackingManager.IsSwapchainTracked(swapchain);
}

bool AddSwapchainToTracking(IDXGISwapChain *swapchain) {
    return g_swapchainTrackingManager.AddSwapchain(swapchain);
}

bool RemoveSwapchainFromTracking(IDXGISwapChain *swapchain) {
    return g_swapchainTrackingManager.RemoveSwapchain(swapchain);
}

std::vector<IDXGISwapChain*> GetAllTrackedSwapchains() {
    return g_swapchainTrackingManager.GetAllTrackedSwapchains();
}

size_t GetTrackedSwapchainCount() {
    return g_swapchainTrackingManager.GetTrackedSwapchainCount();
}

void ClearAllTrackedSwapchains() {
    g_swapchainTrackingManager.ClearAll();
}

bool HasTrackedSwapchains() {
    return g_swapchainTrackingManager.HasTrackedSwapchains();
}

} // namespace display_commanderhooks::dxgi
