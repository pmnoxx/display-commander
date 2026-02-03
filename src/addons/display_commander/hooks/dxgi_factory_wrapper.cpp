#include "dxgi_factory_wrapper.hpp"
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>
#include <dxgi1_4.h>
#include <dxgi1_5.h>
#include <dxgi1_6.h>
#include <initguid.h>
#include <cmath>
#include "../globals.hpp"
#include "../settings/main_tab_settings.hpp"
#include "../swapchain_events.hpp"
#include "../utils/detour_call_tracker.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"
#include "../utils/perf_measurement.hpp"
#include "../utils/timing.hpp"
#include "dxgi/dxgi_present_hooks.hpp"

// Custom IID for DXGIFactoryWrapper interface
// {A1B2C3D4-E5F6-4789-A012-B345C678D909}
DEFINE_GUID(IID_IDXGIFactoryWrapper, 0xa1b2c3d4, 0xe5f6, 0x4789, 0xa0, 0x12, 0xb3, 0x45, 0xc6, 0x78, 0xd9, 0x09);

// Custom IID for DXGISwapChain4Wrapper interface
// {B2C3D4E5-F6A7-4890-B123-C456D789E013}
DEFINE_GUID(IID_IDXGISwapChain4Wrapper, 0xb2c3d4e5, 0xf6a7, 0x4890, 0xb1, 0x23, 0xc4, 0x56, 0xd7, 0x89, 0xe0, 0x13);

namespace display_commanderhooks {

// Helper function to flush command queue from swapchain using native DirectX APIs (DX11 only)
void FlushCommandQueueFromSwapchain(IDXGISwapChain* swapchain) {
    if (swapchain == nullptr) {
        return;
    }

    if (perf_measurement::IsSuppressionEnabled()
        && perf_measurement::IsMetricSuppressed(perf_measurement::Metric::FlushCommandQueueFromSwapchain)) {
        return;
    }

    perf_measurement::ScopedTimer perf_timer(perf_measurement::Metric::FlushCommandQueueFromSwapchain);

    // For D3D11: Get device, get immediate context, flush it
    Microsoft::WRL::ComPtr<ID3D11Device> d3d11_device;
    HRESULT hr = swapchain->GetDevice(IID_PPV_ARGS(&d3d11_device));
    if (SUCCEEDED(hr) && d3d11_device) {
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> immediate_context;
        d3d11_device->GetImmediateContext(&immediate_context);
        if (immediate_context) {
            immediate_context->Flush();
        }
    }
}

namespace {

// Helper function to track present statistics (shared between Present and Present1)
void TrackPresentStatistics(SwapChainWrapperStats* stats, std::atomic<uint64_t>& last_time_ns,
                            std::atomic<uint64_t>& total_calls, std::atomic<double>& smoothed_fps) {
    if (perf_measurement::IsSuppressionEnabled()
        && perf_measurement::IsMetricSuppressed(perf_measurement::Metric::TrackPresentStatistics)) {
        return;
    }

    perf_measurement::ScopedTimer perf_timer(perf_measurement::Metric::TrackPresentStatistics);

    uint64_t now_ns = utils::get_now_ns();
    uint64_t last_time = last_time_ns.exchange(now_ns, std::memory_order_acq_rel);

    total_calls.fetch_add(1, std::memory_order_relaxed);

    // Calculate FPS using rolling average and record frame time
    if (last_time > 0) {
        uint64_t delta_ns = now_ns - last_time;
        // Only update if time delta is reasonable (ignore if > 1 second)
        if (delta_ns < utils::SEC_TO_NS && delta_ns > 0) {
            // Calculate instantaneous FPS from delta time
            double delta_seconds = static_cast<double>(delta_ns) / utils::SEC_TO_NS;
            double instant_fps = 1.0 / delta_seconds;

            // Smooth the FPS using rolling average
            double old_fps = smoothed_fps.load(std::memory_order_acquire);
            double new_fps = UpdateRollingAverage(instant_fps, old_fps);
            smoothed_fps.store(new_fps, std::memory_order_release);
        }
    }

    // Track combined frame time (either Present or Present1 represents a frame submission)
    uint64_t last_combined = stats->last_present_combined_time_ns.load(std::memory_order_acquire);
    if (last_combined == 0 || (now_ns - last_combined) >= 1000ULL) {
        // Record frame time if significant time has passed (avoid double counting when both Present/Present1 called)
        uint64_t expected = last_combined;
        if (stats->last_present_combined_time_ns.compare_exchange_strong(expected, now_ns, std::memory_order_acq_rel)) {
            if (last_combined > 0) {
                uint64_t combined_delta_ns = now_ns - last_combined;
                if (combined_delta_ns < utils::SEC_TO_NS && combined_delta_ns > 0) {
                    float frame_time_ms = static_cast<float>(combined_delta_ns) / utils::NS_TO_MS;
                    uint32_t head = stats->frame_time_head.fetch_add(1, std::memory_order_acq_rel);
                    stats->frame_times[head & (kSwapchainFrameTimeCapacity - 1)] = frame_time_ms;
                }
            }
        }
    }
}
}  // anonymous namespace

// Helper function to create a swapchain wrapper from any swapchain interface
IDXGISwapChain4* CreateSwapChainWrapper(IDXGISwapChain4* swapchain4, SwapChainHook hookType) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    if (swapchain4 == nullptr) {
        LogWarn("CreateSwapChainWrapper: swapchain is null");
        return nullptr;
    }

    // Check if swapchain is already wrapped
    // DXGISwapChain4Wrapper* existingWrapper = QuerySwapChainWrapper(swapchain4);
    // if (existingWrapper != nullptr) {
    //    const char* hookTypeName = (hookType == SwapChainHook::Proxy) ? "Proxy" : (hookType ==
    //    SwapChainHook::NativeRaw) ? "NativeRaw" : "Native"; LogError("CreateSwapChainWrapper: Swapchain 0x%p is
    //    already wrapped, returning existing wrapper (requested hookType: %s)", swapchain4, hookTypeName);
    // AddRef since we're returning it (caller expects to own the reference)
    // existingWrapper->AddRef();
    // return existingWrapper;
    //}

    const char* hookTypeName = (hookType == SwapChainHook::Proxy)       ? "Proxy"
                               : (hookType == SwapChainHook::NativeRaw) ? "NativeRaw"
                                                                        : "Native";
    LogInfo("CreateSwapChainWrapper: Creating wrapper for swapchain: 0x%p (hookType: %s)", swapchain4, hookTypeName);

    return new DXGISwapChain4Wrapper(swapchain4, hookType);
}

// DXGISwapChain4Wrapper implementation
DXGISwapChain4Wrapper::DXGISwapChain4Wrapper(IDXGISwapChain4* originalSwapChain, SwapChainHook hookType)
    : m_originalSwapChain(originalSwapChain), m_refCount(1), m_swapChainHookType(hookType) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    const char* hookTypeName = (hookType == SwapChainHook::Proxy)       ? "Proxy"
                               : (hookType == SwapChainHook::NativeRaw) ? "NativeRaw"
                                                                        : "Native";
    LogInfo("DXGISwapChain4Wrapper: Created wrapper for IDXGISwapChain4 (hookType: %s)", hookTypeName);
}

STDMETHODIMP DXGISwapChain4Wrapper::QueryInterface(REFIID riid, void** ppvObject) {
    if (ppvObject == nullptr) return E_POINTER;

    // Support querying for the wrapper interface itself
    if (riid == IID_IDXGISwapChain4Wrapper) {
        *ppvObject = static_cast<DXGISwapChain4Wrapper*>(this);
        AddRef();
        return S_OK;
    }

    // Support all swapchain interfaces
    if (riid == IID_IUnknown || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIDeviceSubObject)
        || riid == __uuidof(IDXGISwapChain) || riid == __uuidof(IDXGISwapChain1) || riid == __uuidof(IDXGISwapChain2)
        || riid == __uuidof(IDXGISwapChain3) || riid == __uuidof(IDXGISwapChain4)) {
        *ppvObject = static_cast<IDXGISwapChain4*>(this);
        AddRef();
        return S_OK;
    }

    return m_originalSwapChain->QueryInterface(riid, ppvObject);
}

STDMETHODIMP_(ULONG) DXGISwapChain4Wrapper::AddRef() {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    InterlockedIncrement(&m_refCount);
    return m_refCount;
}

STDMETHODIMP_(ULONG) DXGISwapChain4Wrapper::Release() {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    InterlockedDecrement(&m_refCount);

    if (m_refCount == 0) {
        LogInfo("DXGISwapChain4Wrapper: Releasing wrapper, original swapchain ref count: %lu", m_refCount);
        m_originalSwapChain->Release();
        delete this;
    }
    return m_refCount;
}

// IDXGIObject methods - delegate to original
STDMETHODIMP DXGISwapChain4Wrapper::SetPrivateData(REFGUID Name, UINT DataSize, const void* pData) {
    return m_originalSwapChain->SetPrivateData(Name, DataSize, pData);
}

STDMETHODIMP DXGISwapChain4Wrapper::SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnknown) {
    return m_originalSwapChain->SetPrivateDataInterface(Name, pUnknown);
}

STDMETHODIMP DXGISwapChain4Wrapper::GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData) {
    return m_originalSwapChain->GetPrivateData(Name, pDataSize, pData);
}

STDMETHODIMP DXGISwapChain4Wrapper::GetParent(REFIID riid, void** ppParent) {
    return m_originalSwapChain->GetParent(riid, ppParent);
}

// IDXGIDeviceSubObject methods - delegate to original
STDMETHODIMP DXGISwapChain4Wrapper::GetDevice(REFIID riid, void** ppDevice) {
    return m_originalSwapChain->GetDevice(riid, ppDevice);
}

// IDXGISwapChain methods - delegate to original
STDMETHODIMP DXGISwapChain4Wrapper::Present(UINT SyncInterval, UINT Flags) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    if (m_swapChainHookType == SwapChainHook::NativeRaw) {
        return m_originalSwapChain->Present(SyncInterval, Flags);
    }
    // Mark that Present has been called at least once
    g_swapchain_wrapper_present_called.store(true, std::memory_order_relaxed);

    // Track statistics
    SwapChainWrapperStats* stats = (m_swapChainHookType == SwapChainHook::Proxy) ? &g_swapchain_wrapper_stats_proxy
                                                                                 : &g_swapchain_wrapper_stats_native;

    TrackPresentStatistics(stats, stats->last_present_time_ns, stats->total_present_calls, stats->smoothed_present_fps);

    // For native swapchains, execute common present logic (HandlePresentBefore/OnPresentFlags2/HandlePresentAfter)
    // This avoids duplicate execution in the detour functions
    Microsoft::WRL::ComPtr<IDXGISwapChain> baseSwapChain;
    auto flagsCopy = Flags;  // to fix crash
    ChooseFpsLimiter(g_global_frame_id.load(std::memory_order_relaxed), FpsLimiterCallSite::dxgi_factory_wrapper);
    auto use_fps_limiter = GetChosenFpsLimiter(FpsLimiterCallSite::dxgi_factory_wrapper);

    if (use_fps_limiter) {
        if (SUCCEEDED(QueryInterface(IID_PPV_ARGS(&baseSwapChain)))) {
            OnPresentFlags2(false,
                            true);  // Called from wrapper, not present_detour

            // Flush command queue from swapchain using native DirectX APIs
            FlushCommandQueueFromSwapchain(baseSwapChain.Get());
        }
        // Record native frame time for frames shown to display
        // RecordNativeFrameTime();
        // display_commanderhooks::dxgi::HandlePresentBefore2();
    }

    HRESULT res = m_originalSwapChain->Present(SyncInterval, Flags);

    if (use_fps_limiter && baseSwapChain.Get() != nullptr) {
        display_commanderhooks::dxgi::HandlePresentAfter(true);
    }

    return res;
}

STDMETHODIMP DXGISwapChain4Wrapper::GetBuffer(UINT Buffer, REFIID riid, void** ppSurface) {
    return m_originalSwapChain->GetBuffer(Buffer, riid, ppSurface);
}

STDMETHODIMP DXGISwapChain4Wrapper::SetFullscreenState(BOOL Fullscreen, IDXGIOutput* pTarget) {
    return m_originalSwapChain->SetFullscreenState(Fullscreen, pTarget);
}

STDMETHODIMP DXGISwapChain4Wrapper::GetFullscreenState(BOOL* pFullscreen, IDXGIOutput** ppTarget) {
    return m_originalSwapChain->GetFullscreenState(pFullscreen, ppTarget);
}

STDMETHODIMP DXGISwapChain4Wrapper::GetDesc(DXGI_SWAP_CHAIN_DESC* pDesc) { return m_originalSwapChain->GetDesc(pDesc); }

STDMETHODIMP DXGISwapChain4Wrapper::ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format,
                                                  UINT SwapChainFlags) {
    return m_originalSwapChain->ResizeBuffers(BufferCount, Width, Height, Format, SwapChainFlags);
}

STDMETHODIMP DXGISwapChain4Wrapper::ResizeTarget(const DXGI_MODE_DESC* pNewTargetParameters) {
    return m_originalSwapChain->ResizeTarget(pNewTargetParameters);
}

STDMETHODIMP DXGISwapChain4Wrapper::GetContainingOutput(IDXGIOutput** ppOutput) {
    return m_originalSwapChain->GetContainingOutput(ppOutput);
}

STDMETHODIMP DXGISwapChain4Wrapper::GetFrameStatistics(DXGI_FRAME_STATISTICS* pStats) {
    return m_originalSwapChain->GetFrameStatistics(pStats);
}

STDMETHODIMP DXGISwapChain4Wrapper::GetLastPresentCount(UINT* pLastPresentCount) {
    return m_originalSwapChain->GetLastPresentCount(pLastPresentCount);
}

// IDXGISwapChain1 methods - delegate to original
STDMETHODIMP DXGISwapChain4Wrapper::GetDesc1(DXGI_SWAP_CHAIN_DESC1* pDesc) {
    return m_originalSwapChain->GetDesc1(pDesc);
}

STDMETHODIMP DXGISwapChain4Wrapper::GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pDesc) {
    return m_originalSwapChain->GetFullscreenDesc(pDesc);
}

STDMETHODIMP DXGISwapChain4Wrapper::GetHwnd(HWND* pHwnd) { return m_originalSwapChain->GetHwnd(pHwnd); }

STDMETHODIMP DXGISwapChain4Wrapper::GetCoreWindow(REFIID refiid, void** ppUnk) {
    return m_originalSwapChain->GetCoreWindow(refiid, ppUnk);
}

STDMETHODIMP DXGISwapChain4Wrapper::Present1(UINT SyncInterval, UINT PresentFlags,
                                             const DXGI_PRESENT_PARAMETERS* pPresentParameters) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    if (m_swapChainHookType == SwapChainHook::NativeRaw) {
        return m_originalSwapChain->Present1(SyncInterval, PresentFlags, pPresentParameters);
    }
    // Mark that Present1 has been called at least once
    g_swapchain_wrapper_present_called.store(true, std::memory_order_relaxed);
    g_swapchain_wrapper_present1_called.store(true, std::memory_order_relaxed);

    // Track statistics
    SwapChainWrapperStats* stats = (m_swapChainHookType == SwapChainHook::Proxy) ? &g_swapchain_wrapper_stats_proxy
                                                                                 : &g_swapchain_wrapper_stats_native;

    TrackPresentStatistics(stats, stats->last_present1_time_ns, stats->total_present1_calls,
                           stats->smoothed_present1_fps);

    // For native swapchains, execute common present logic (HandlePresentBefore/OnPresentFlags2/HandlePresentAfter)
    // This avoids duplicate execution in the detour functions
    Microsoft::WRL::ComPtr<IDXGISwapChain> baseSwapChain;
    auto flagsCopy = PresentFlags;  // to fix crash

    ChooseFpsLimiter(g_global_frame_id.load(std::memory_order_relaxed), FpsLimiterCallSite::dxgi_factory_wrapper);
    auto use_fps_limiter = GetChosenFpsLimiter(FpsLimiterCallSite::dxgi_factory_wrapper);
    if (use_fps_limiter) {
        if (SUCCEEDED(QueryInterface(IID_PPV_ARGS(&baseSwapChain)))) {
            OnPresentFlags2(false, true);  // Called from wrapper, not present_detour

            // Flush command queue from swapchain using native DirectX APIs
            FlushCommandQueueFromSwapchain(baseSwapChain.Get());
        }
        // Record native frame time for frames shown to display
        // RecordNativeFrameTime();
        // display_commanderhooks::dxgi::HandlePresentBefore2();
    }

    HRESULT res = m_originalSwapChain->Present1(SyncInterval, PresentFlags, pPresentParameters);

    if (use_fps_limiter && baseSwapChain.Get() != nullptr) {
        display_commanderhooks::dxgi::HandlePresentAfter(true);
    }

    return res;
}

STDMETHODIMP_(BOOL) DXGISwapChain4Wrapper::IsTemporaryMonoSupported() {
    return m_originalSwapChain->IsTemporaryMonoSupported();
}

STDMETHODIMP DXGISwapChain4Wrapper::GetRestrictToOutput(IDXGIOutput** ppRestrictToOutput) {
    return m_originalSwapChain->GetRestrictToOutput(ppRestrictToOutput);
}

STDMETHODIMP DXGISwapChain4Wrapper::SetBackgroundColor(const DXGI_RGBA* pColor) {
    return m_originalSwapChain->SetBackgroundColor(pColor);
}

STDMETHODIMP DXGISwapChain4Wrapper::GetBackgroundColor(DXGI_RGBA* pColor) {
    return m_originalSwapChain->GetBackgroundColor(pColor);
}

STDMETHODIMP DXGISwapChain4Wrapper::SetRotation(DXGI_MODE_ROTATION Rotation) {
    return m_originalSwapChain->SetRotation(Rotation);
}

STDMETHODIMP DXGISwapChain4Wrapper::GetRotation(DXGI_MODE_ROTATION* pRotation) {
    return m_originalSwapChain->GetRotation(pRotation);
}

// IDXGISwapChain2 methods - delegate to original
STDMETHODIMP DXGISwapChain4Wrapper::SetSourceSize(UINT Width, UINT Height) {
    return m_originalSwapChain->SetSourceSize(Width, Height);
}

STDMETHODIMP DXGISwapChain4Wrapper::GetSourceSize(UINT* pWidth, UINT* pHeight) {
    return m_originalSwapChain->GetSourceSize(pWidth, pHeight);
}

STDMETHODIMP DXGISwapChain4Wrapper::SetMaximumFrameLatency(UINT MaxLatency) {
    return m_originalSwapChain->SetMaximumFrameLatency(MaxLatency);
}

STDMETHODIMP DXGISwapChain4Wrapper::GetMaximumFrameLatency(UINT* pMaxLatency) {
    return m_originalSwapChain->GetMaximumFrameLatency(pMaxLatency);
}

STDMETHODIMP_(HANDLE) DXGISwapChain4Wrapper::GetFrameLatencyWaitableObject() {
    return m_originalSwapChain->GetFrameLatencyWaitableObject();
}

STDMETHODIMP DXGISwapChain4Wrapper::SetMatrixTransform(const DXGI_MATRIX_3X2_F* pMatrix) {
    return m_originalSwapChain->SetMatrixTransform(pMatrix);
}

STDMETHODIMP DXGISwapChain4Wrapper::GetMatrixTransform(DXGI_MATRIX_3X2_F* pMatrix) {
    return m_originalSwapChain->GetMatrixTransform(pMatrix);
}

// IDXGISwapChain3 methods - delegate to original
STDMETHODIMP_(UINT) DXGISwapChain4Wrapper::GetCurrentBackBufferIndex() {
    return m_originalSwapChain->GetCurrentBackBufferIndex();
}

STDMETHODIMP DXGISwapChain4Wrapper::CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE ColorSpace, UINT* pColorSpaceSupport) {
    return m_originalSwapChain->CheckColorSpaceSupport(ColorSpace, pColorSpaceSupport);
}

STDMETHODIMP DXGISwapChain4Wrapper::SetColorSpace1(DXGI_COLOR_SPACE_TYPE ColorSpace) {
    return m_originalSwapChain->SetColorSpace1(ColorSpace);
}

STDMETHODIMP DXGISwapChain4Wrapper::ResizeBuffers1(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format,
                                                   UINT SwapChainFlags, const UINT* pNodeMask,
                                                   IUnknown* const* ppPresentQueue) {
    return m_originalSwapChain->ResizeBuffers1(BufferCount, Width, Height, Format, SwapChainFlags, pNodeMask,
                                               ppPresentQueue);
}

// IDXGISwapChain4 methods - delegate to original
STDMETHODIMP DXGISwapChain4Wrapper::SetHDRMetaData(DXGI_HDR_METADATA_TYPE Type, UINT Size, void* pMetaData) {
    return m_originalSwapChain->SetHDRMetaData(Type, Size, pMetaData);
}

DXGIFactoryWrapper::DXGIFactoryWrapper(IDXGIFactory7* originalFactory, SwapChainHook hookType)
    : m_originalFactory(originalFactory),
      m_refCount(1),
      m_swapChainHookType(hookType),
      m_slGetNativeInterface(nullptr),
      m_slUpgradeInterface(nullptr),
      m_commandQueueMap(nullptr) {
    const char* hookTypeName = (hookType == SwapChainHook::Proxy)       ? "Proxy"
                               : (hookType == SwapChainHook::NativeRaw) ? "NativeRaw"
                                                                        : "Native";
    LogInfo("DXGIFactoryWrapper: Created wrapper for IDXGIFactory7 (hookType: %s)", hookTypeName);
}

STDMETHODIMP DXGIFactoryWrapper::QueryInterface(REFIID riid, void** ppvObject) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    if (ppvObject == nullptr) return E_POINTER;

    // Support querying for the wrapper interface itself
    if (riid == IID_IDXGIFactoryWrapper) {
        *ppvObject = static_cast<DXGIFactoryWrapper*>(this);
        AddRef();
        return S_OK;
    }

    if (riid == IID_IUnknown || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIDeviceSubObject)
        || riid == __uuidof(IDXGIFactory) || riid == __uuidof(IDXGIFactory1) || riid == __uuidof(IDXGIFactory2)
        || riid == __uuidof(IDXGIFactory3) || riid == __uuidof(IDXGIFactory4) || riid == __uuidof(IDXGIFactory5)
        || riid == __uuidof(IDXGIFactory6) || riid == __uuidof(IDXGIFactory7)) {
        *ppvObject = static_cast<IDXGIFactory7*>(this);
        AddRef();
        return S_OK;
    }

    return m_originalFactory->QueryInterface(riid, ppvObject);
}

STDMETHODIMP_(ULONG) DXGIFactoryWrapper::AddRef() {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    InterlockedIncrement(&m_refCount);
    return m_refCount;
    // return InterlockedIncrement(&m_refCount);
}

STDMETHODIMP_(ULONG) DXGIFactoryWrapper::Release() {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    InterlockedDecrement(&m_refCount);

    if (m_refCount == 0) {
        LogInfo("DXGIFactoryWrapper: Releasing wrapper, original factory ref count: %lu", m_refCount);
        m_originalFactory->Release();
        delete this;
    }
    return m_refCount;
}

// IDXGIObject methods - delegate to original
STDMETHODIMP DXGIFactoryWrapper::SetPrivateData(REFGUID Name, UINT DataSize, const void* pData) {
    return m_originalFactory->SetPrivateData(Name, DataSize, pData);
}

STDMETHODIMP DXGIFactoryWrapper::SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnknown) {
    return m_originalFactory->SetPrivateDataInterface(Name, pUnknown);
}

STDMETHODIMP DXGIFactoryWrapper::GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData) {
    return m_originalFactory->GetPrivateData(Name, pDataSize, pData);
}

STDMETHODIMP DXGIFactoryWrapper::GetParent(REFIID riid, void** ppParent) {
    return m_originalFactory->GetParent(riid, ppParent);
}

// IDXGIDeviceSubObject methods - GetDevice is not part of IDXGIFactory7

// IDXGIFactory methods - delegate to original
STDMETHODIMP DXGIFactoryWrapper::EnumAdapters(UINT Adapter, IDXGIAdapter** ppAdapter) {
    return m_originalFactory->EnumAdapters(Adapter, ppAdapter);
}

STDMETHODIMP DXGIFactoryWrapper::MakeWindowAssociation(HWND WindowHandle, UINT Flags) {
    return m_originalFactory->MakeWindowAssociation(WindowHandle, Flags);
}

STDMETHODIMP DXGIFactoryWrapper::GetWindowAssociation(HWND* pWindowHandle) {
    return m_originalFactory->GetWindowAssociation(pWindowHandle);
}

STDMETHODIMP DXGIFactoryWrapper::CreateSwapChain(IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc,
                                                 IDXGISwapChain** ppSwapChain) {
    LogInfo("DXGIFactoryWrapper::CreateSwapChain called");

    // Capture game render resolution (before any modifications) - matches Special K's render_x/render_y
    if (pDesc != nullptr) {
        g_game_render_width.store(pDesc->BufferDesc.Width);
        g_game_render_height.store(pDesc->BufferDesc.Height);
        LogInfo("DXGIFactoryWrapper::CreateSwapChain - Game render resolution: %ux%u", pDesc->BufferDesc.Width,
                pDesc->BufferDesc.Height);
    }

    if (ShouldInterceptSwapChainCreation()) {
        LogInfo("DXGIFactoryWrapper: Intercepting swapchain creation for Streamline compatibility");
        // TODO(user): Implement swapchain interception logic
    }

    auto result = m_originalFactory->CreateSwapChain(pDevice, pDesc, ppSwapChain);
    if (SUCCEEDED(result)) {
        auto* swapchain = *ppSwapChain;
        if (swapchain != nullptr) {
            LogInfo("DXGIFactoryWrapper::CreateSwapChain succeeded swapchain: 0x%p", swapchain);

            // Create wrapper instead of hooking

            // Convert IDXGISwapChain to IDXGISwapChain4 for wrapper creation
            Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain4;
            if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&swapChain4)))) {
                IDXGISwapChain4* wrappedSwapChain = CreateSwapChainWrapper(swapChain4.Get(), m_swapChainHookType);
                if (wrappedSwapChain != nullptr) {
                    swapChain4->AddRef();
                    swapchain->Release();
                    *ppSwapChain = wrappedSwapChain;
                }
            }
        }
    }

    return result;
}

STDMETHODIMP DXGIFactoryWrapper::CreateSoftwareAdapter(HMODULE Module, IDXGIAdapter** ppAdapter) {
    return m_originalFactory->CreateSoftwareAdapter(Module, ppAdapter);
}

// IDXGIFactory1 methods - delegate to original
STDMETHODIMP DXGIFactoryWrapper::EnumAdapters1(UINT Adapter, IDXGIAdapter1** ppAdapter) {
    return m_originalFactory->EnumAdapters1(Adapter, ppAdapter);
}

STDMETHODIMP_(BOOL) DXGIFactoryWrapper::IsCurrent() { return m_originalFactory->IsCurrent(); }

// IDXGIFactory2 methods - delegate to original
STDMETHODIMP_(BOOL) DXGIFactoryWrapper::IsWindowedStereoEnabled() {
    return m_originalFactory->IsWindowedStereoEnabled();
}

STDMETHODIMP DXGIFactoryWrapper::CreateSwapChainForHwnd(IUnknown* pDevice, HWND hWnd,
                                                        const DXGI_SWAP_CHAIN_DESC1* pDesc,
                                                        const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
                                                        IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain) {
    LogInfo("DXGIFactoryWrapper::CreateSwapChainForHwnd called");

    // Capture game render resolution (before any modifications) - matches Special K's render_x/render_y
    if (pDesc != nullptr) {
        g_game_render_width.store(pDesc->Width);
        g_game_render_height.store(pDesc->Height);
        LogInfo("DXGIFactoryWrapper::CreateSwapChainForHwnd - Game render resolution: %ux%u", pDesc->Width,
                pDesc->Height);
    }

    if (ShouldInterceptSwapChainCreation()) {
        LogInfo("DXGIFactoryWrapper: Intercepting CreateSwapChainForHwnd for Streamline compatibility");
        // TODO(user): Implement swapchain interception logic
    }

    auto result = m_originalFactory->CreateSwapChainForHwnd(pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput,
                                                            ppSwapChain);
    if (SUCCEEDED(result)) {
        auto* swapchain = *ppSwapChain;
        if (swapchain != nullptr) {
            LogInfo("DXGIFactoryWrapper::CreateSwapChainForHwnd succeeded swapchain: 0x%p", swapchain);

            Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain4;
            if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&swapChain4)))) {
                IDXGISwapChain4* wrappedSwapChain = CreateSwapChainWrapper(swapChain4.Get(), m_swapChainHookType);
                if (wrappedSwapChain != nullptr) {
                    swapChain4->AddRef();
                    swapchain->Release();
                    *ppSwapChain = wrappedSwapChain;
                }
            }
        }
    }

    return result;
}

STDMETHODIMP DXGIFactoryWrapper::CreateSwapChainForCoreWindow(IUnknown* pDevice, IUnknown* pWindow,
                                                              const DXGI_SWAP_CHAIN_DESC1* pDesc,
                                                              IDXGIOutput* pRestrictToOutput,
                                                              IDXGISwapChain1** ppSwapChain) {
    LogInfo("DXGIFactoryWrapper::CreateSwapChainForCoreWindow called");

    // Capture game render resolution (before any modifications) - matches Special K's render_x/render_y
    if (pDesc != nullptr) {
        g_game_render_width.store(pDesc->Width);
        g_game_render_height.store(pDesc->Height);
        LogInfo("DXGIFactoryWrapper::CreateSwapChainForCoreWindow - Game render resolution: %ux%u", pDesc->Width,
                pDesc->Height);
    }

    if (ShouldInterceptSwapChainCreation()) {
        LogInfo("DXGIFactoryWrapper: Intercepting CreateSwapChainForCoreWindow for Streamline compatibility");
        // TODO(user): Implement swapchain interception logic
    }

    auto result =
        m_originalFactory->CreateSwapChainForCoreWindow(pDevice, pWindow, pDesc, pRestrictToOutput, ppSwapChain);
    if (SUCCEEDED(result)) {
        auto* swapchain = *ppSwapChain;
        if (swapchain != nullptr) {
            LogInfo("DXGIFactoryWrapper::CreateSwapChainForCoreWindow succeeded swapchain: 0x%p", swapchain);

            Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain4;
            if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&swapChain4)))) {
                IDXGISwapChain4* wrappedSwapChain = CreateSwapChainWrapper(swapChain4.Get(), m_swapChainHookType);
                if (wrappedSwapChain != nullptr) {
                    swapChain4->AddRef();
                    swapchain->Release();
                    *ppSwapChain = wrappedSwapChain;
                }
            }
        }
    }

    return result;
}

STDMETHODIMP DXGIFactoryWrapper::GetSharedResourceAdapterLuid(HANDLE hResource, LUID* pLuid) {
    return m_originalFactory->GetSharedResourceAdapterLuid(hResource, pLuid);
}

STDMETHODIMP DXGIFactoryWrapper::RegisterStereoStatusWindow(HWND WindowHandle, UINT wMsg, DWORD* pdwCookie) {
    return m_originalFactory->RegisterStereoStatusWindow(WindowHandle, wMsg, pdwCookie);
}

STDMETHODIMP DXGIFactoryWrapper::RegisterStereoStatusEvent(HANDLE hEvent, DWORD* pdwCookie) {
    return m_originalFactory->RegisterStereoStatusEvent(hEvent, pdwCookie);
}

STDMETHODIMP_(void) DXGIFactoryWrapper::UnregisterStereoStatus(DWORD dwCookie) {
    m_originalFactory->UnregisterStereoStatus(dwCookie);
}

STDMETHODIMP DXGIFactoryWrapper::RegisterOcclusionStatusWindow(HWND WindowHandle, UINT wMsg, DWORD* pdwCookie) {
    return m_originalFactory->RegisterOcclusionStatusWindow(WindowHandle, wMsg, pdwCookie);
}

STDMETHODIMP DXGIFactoryWrapper::RegisterOcclusionStatusEvent(HANDLE hEvent, DWORD* pdwCookie) {
    return m_originalFactory->RegisterOcclusionStatusEvent(hEvent, pdwCookie);
}

STDMETHODIMP_(void) DXGIFactoryWrapper::UnregisterOcclusionStatus(DWORD dwCookie) {
    m_originalFactory->UnregisterOcclusionStatus(dwCookie);
}

STDMETHODIMP DXGIFactoryWrapper::CreateSwapChainForComposition(IUnknown* pDevice, const DXGI_SWAP_CHAIN_DESC1* pDesc,
                                                               IDXGIOutput* pRestrictToOutput,
                                                               IDXGISwapChain1** ppSwapChain) {
    LogInfo("DXGIFactoryWrapper::CreateSwapChainForComposition called");

    // Capture game render resolution (before any modifications) - matches Special K's render_x/render_y
    if (pDesc != nullptr) {
        g_game_render_width.store(pDesc->Width);
        g_game_render_height.store(pDesc->Height);
        LogInfo("DXGIFactoryWrapper::CreateSwapChainForComposition - Game render resolution: %ux%u", pDesc->Width,
                pDesc->Height);
    }

    if (ShouldInterceptSwapChainCreation()) {
        LogInfo("DXGIFactoryWrapper: Intercepting CreateSwapChainForComposition for Streamline compatibility");
        // TODO(user): Implement swapchain interception logic
    }

    auto result = m_originalFactory->CreateSwapChainForComposition(pDevice, pDesc, pRestrictToOutput, ppSwapChain);
    if (SUCCEEDED(result)) {
        auto* swapchain = *ppSwapChain;
        if (swapchain != nullptr) {
            LogInfo("DXGIFactoryWrapper::CreateSwapChainForComposition succeeded swapchain: 0x%p", swapchain);

            Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain4;
            if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&swapChain4)))) {
                IDXGISwapChain4* wrappedSwapChain = CreateSwapChainWrapper(swapChain4.Get(), m_swapChainHookType);
                if (wrappedSwapChain != nullptr) {
                    swapChain4->AddRef();
                    swapchain->Release();
                    *ppSwapChain = wrappedSwapChain;
                }
            }
        }
    }

    return result;
}

// IDXGIFactory3 methods - delegate to original
STDMETHODIMP_(UINT) DXGIFactoryWrapper::GetCreationFlags() { return m_originalFactory->GetCreationFlags(); }

// IDXGIFactory4 methods - delegate to original
STDMETHODIMP DXGIFactoryWrapper::EnumAdapterByLuid(LUID AdapterLuid, REFIID riid, void** ppvAdapter) {
    return m_originalFactory->EnumAdapterByLuid(AdapterLuid, riid, ppvAdapter);
}

STDMETHODIMP DXGIFactoryWrapper::EnumWarpAdapter(REFIID riid, void** ppvAdapter) {
    return m_originalFactory->EnumWarpAdapter(riid, ppvAdapter);
}

// IDXGIFactory5 methods - delegate to original
STDMETHODIMP DXGIFactoryWrapper::CheckFeatureSupport(DXGI_FEATURE Feature, void* pFeatureSupportData,
                                                     UINT FeatureSupportDataSize) {
    return m_originalFactory->CheckFeatureSupport(Feature, pFeatureSupportData, FeatureSupportDataSize);
}

// IDXGIFactory6 methods - delegate to original
STDMETHODIMP DXGIFactoryWrapper::EnumAdapterByGpuPreference(UINT Adapter, DXGI_GPU_PREFERENCE GpuPreference,
                                                            REFIID riid, void** ppvAdapter) {
    return m_originalFactory->EnumAdapterByGpuPreference(Adapter, GpuPreference, riid, ppvAdapter);
}

// IDXGIFactory7 methods - delegate to original
STDMETHODIMP DXGIFactoryWrapper::RegisterAdaptersChangedEvent(HANDLE hEvent, DWORD* pdwCookie) {
    return m_originalFactory->RegisterAdaptersChangedEvent(hEvent, pdwCookie);
}

STDMETHODIMP DXGIFactoryWrapper::UnregisterAdaptersChangedEvent(DWORD dwCookie) {
    return m_originalFactory->UnregisterAdaptersChangedEvent(dwCookie);
}

// Additional methods for Streamline integration
void DXGIFactoryWrapper::SetSLGetNativeInterface(void* slGetNativeInterface) {
    m_slGetNativeInterface = slGetNativeInterface;
}

void DXGIFactoryWrapper::SetSLUpgradeInterface(void* slUpgradeInterface) { m_slUpgradeInterface = slUpgradeInterface; }

void DXGIFactoryWrapper::SetCommandQueueMap(void* commandQueueMap) { m_commandQueueMap = commandQueueMap; }

bool DXGIFactoryWrapper::ShouldInterceptSwapChainCreation() const {
    // Check if Streamline integration is enabled
    return m_slGetNativeInterface != nullptr && m_slUpgradeInterface != nullptr;
}

// Helper function to create an output wrapper
IDXGIOutput6* CreateOutputWrapper(IDXGIOutput* output) {
    // TODO: (fixme), this is wrong way to do it, there is memory leak here
    // don't do anything if hide native hdr is disabled
    if (!s_hide_hdr_capabilities.load()) {
        return nullptr;
    }

    if (output == nullptr) {
        LogWarn("CreateOutputWrapper: output is null");
        return nullptr;
    }

    // Try to query for IDXGIOutput6
    Microsoft::WRL::ComPtr<IDXGIOutput6> output6;
    if (FAILED(output->QueryInterface(IID_PPV_ARGS(&output6)))) {
        LogWarn("CreateOutputWrapper: Failed to query IDXGIOutput6 interface");
        return nullptr;
    }

    LogInfo("CreateOutputWrapper: Creating wrapper for output: 0x%p", output);
    output6->AddRef();
    auto result = new IDXGIOutput6Wrapper(output6.Get());
    output->Release();
    return result;
}

// IDXGIOutput6Wrapper implementation
IDXGIOutput6Wrapper::IDXGIOutput6Wrapper(IDXGIOutput6* originalOutput)
    : m_originalOutput(originalOutput), m_refCount(1) {
    LogInfo("IDXGIOutput6Wrapper: Created wrapper for IDXGIOutput6");
}

STDMETHODIMP IDXGIOutput6Wrapper::QueryInterface(REFIID riid, void** ppvObject) {
    if (ppvObject == nullptr) return E_POINTER;

    // Support all output interfaces
    if (riid == IID_IUnknown || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIDeviceSubObject)
        || riid == __uuidof(IDXGIOutput) || riid == __uuidof(IDXGIOutput1) || riid == __uuidof(IDXGIOutput2)
        || riid == __uuidof(IDXGIOutput3) || riid == __uuidof(IDXGIOutput4) || riid == __uuidof(IDXGIOutput5)
        || riid == __uuidof(IDXGIOutput6)) {
        *ppvObject = static_cast<IDXGIOutput6*>(this);
        AddRef();
        return S_OK;
    }

    return m_originalOutput->QueryInterface(riid, ppvObject);
}

STDMETHODIMP_(ULONG) IDXGIOutput6Wrapper::AddRef() {
    return m_originalOutput->AddRef();
    // return InterlockedIncrement(&m_refCount);
}

STDMETHODIMP_(ULONG) IDXGIOutput6Wrapper::Release() {
    ULONG refCount = m_originalOutput->Release();
    if (refCount == 0) {
        LogInfo("IDXGIOutput6Wrapper: Releasing wrapper");
        delete this;
    }
    return refCount;
}

// IDXGIObject methods - delegate to original
STDMETHODIMP IDXGIOutput6Wrapper::SetPrivateData(REFGUID Name, UINT DataSize, const void* pData) {
    return m_originalOutput->SetPrivateData(Name, DataSize, pData);
}

STDMETHODIMP IDXGIOutput6Wrapper::SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnknown) {
    return m_originalOutput->SetPrivateDataInterface(Name, pUnknown);
}

STDMETHODIMP IDXGIOutput6Wrapper::GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData) {
    return m_originalOutput->GetPrivateData(Name, pDataSize, pData);
}

STDMETHODIMP IDXGIOutput6Wrapper::GetParent(REFIID riid, void** ppParent) {
    return m_originalOutput->GetParent(riid, ppParent);
}

// IDXGIDeviceSubObject methods - delegate to original
// Note: GetDevice is inherited through IDXGIOutput -> IDXGIDeviceSubObject
STDMETHODIMP IDXGIOutput6Wrapper::GetDevice(REFIID riid, void** ppDevice) {
    // Query for IDXGIDeviceSubObject to call GetDevice
    Microsoft::WRL::ComPtr<IDXGIDeviceSubObject> deviceSubObject;
    if (SUCCEEDED(m_originalOutput->QueryInterface(IID_PPV_ARGS(&deviceSubObject)))) {
        return deviceSubObject->GetDevice(riid, ppDevice);
    }
    return E_FAIL;
}

// IDXGIOutput methods - override the ones we care about
STDMETHODIMP IDXGIOutput6Wrapper::GetDesc(DXGI_OUTPUT_DESC* pDesc) {
    // Increment counter
    g_dxgi_output_event_counters[DXGI_OUTPUT_EVENT_GETDESC].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Log the GetDesc call (only on first few calls to avoid spam)
    static int getdesc_log_count = 0;
    if (getdesc_log_count < 3) {
        LogInfo("IDXGIOutput::GetDesc called");
        getdesc_log_count++;
    }

    return m_originalOutput->GetDesc(pDesc);
}

STDMETHODIMP IDXGIOutput6Wrapper::GetDisplayModeList(DXGI_FORMAT EnumFormat, UINT Flags, UINT* pNumModes,
                                                     DXGI_MODE_DESC* pDesc) {
    return m_originalOutput->GetDisplayModeList(EnumFormat, Flags, pNumModes, pDesc);
}

STDMETHODIMP IDXGIOutput6Wrapper::FindClosestMatchingMode(const DXGI_MODE_DESC* pModeToMatch,
                                                          DXGI_MODE_DESC* pClosestMatch, IUnknown* pConcernedDevice) {
    return m_originalOutput->FindClosestMatchingMode(pModeToMatch, pClosestMatch, pConcernedDevice);
}

STDMETHODIMP IDXGIOutput6Wrapper::WaitForVBlank() { return m_originalOutput->WaitForVBlank(); }

STDMETHODIMP IDXGIOutput6Wrapper::TakeOwnership(IUnknown* pDevice, BOOL Exclusive) {
    return m_originalOutput->TakeOwnership(pDevice, Exclusive);
}

STDMETHODIMP_(void) IDXGIOutput6Wrapper::ReleaseOwnership() { m_originalOutput->ReleaseOwnership(); }

STDMETHODIMP IDXGIOutput6Wrapper::GetGammaControlCapabilities(DXGI_GAMMA_CONTROL_CAPABILITIES* pGammaCaps) {
    return m_originalOutput->GetGammaControlCapabilities(pGammaCaps);
}

STDMETHODIMP IDXGIOutput6Wrapper::SetGammaControl(const DXGI_GAMMA_CONTROL* pArray) {
    // Increment counter
    g_dxgi_output_event_counters[DXGI_OUTPUT_EVENT_SETGAMMACONTROL].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Log the SetGammaControl call (only on first few calls to avoid spam)
    static int setgammacontrol_log_count = 0;
    if (setgammacontrol_log_count < 3) {
        LogInfo("IDXGIOutput::SetGammaControl called");
        setgammacontrol_log_count++;
    }

    return m_originalOutput->SetGammaControl(pArray);
}

STDMETHODIMP IDXGIOutput6Wrapper::GetGammaControl(DXGI_GAMMA_CONTROL* pArray) {
    // Increment counter
    g_dxgi_output_event_counters[DXGI_OUTPUT_EVENT_GETGAMMACONTROL].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Log the GetGammaControl call (only on first few calls to avoid spam)
    static int getgammacontrol_log_count = 0;
    if (getgammacontrol_log_count < 3) {
        LogInfo("IDXGIOutput::GetGammaControl called");
        getgammacontrol_log_count++;
    }

    return m_originalOutput->GetGammaControl(pArray);
}

STDMETHODIMP IDXGIOutput6Wrapper::SetDisplaySurface(IDXGISurface* pScanoutSurface) {
    return m_originalOutput->SetDisplaySurface(pScanoutSurface);
}

STDMETHODIMP IDXGIOutput6Wrapper::GetDisplaySurfaceData(IDXGISurface* pDestination) {
    return m_originalOutput->GetDisplaySurfaceData(pDestination);
}

STDMETHODIMP IDXGIOutput6Wrapper::GetFrameStatistics(DXGI_FRAME_STATISTICS* pStats) {
    return m_originalOutput->GetFrameStatistics(pStats);
}

// IDXGIOutput1 methods - delegate to original
STDMETHODIMP IDXGIOutput6Wrapper::GetDisplayModeList1(DXGI_FORMAT EnumFormat, UINT Flags, UINT* pNumModes,
                                                      DXGI_MODE_DESC1* pDesc) {
    return m_originalOutput->GetDisplayModeList1(EnumFormat, Flags, pNumModes, pDesc);
}

STDMETHODIMP IDXGIOutput6Wrapper::FindClosestMatchingMode1(const DXGI_MODE_DESC1* pModeToMatch,
                                                           DXGI_MODE_DESC1* pClosestMatch, IUnknown* pConcernedDevice) {
    return m_originalOutput->FindClosestMatchingMode1(pModeToMatch, pClosestMatch, pConcernedDevice);
}

STDMETHODIMP IDXGIOutput6Wrapper::GetDisplaySurfaceData1(IDXGIResource* pDestination) {
    return m_originalOutput->GetDisplaySurfaceData1(pDestination);
}

STDMETHODIMP IDXGIOutput6Wrapper::DuplicateOutput(IUnknown* pDevice, IDXGIOutputDuplication** ppOutputDuplication) {
    return m_originalOutput->DuplicateOutput(pDevice, ppOutputDuplication);
}

// IDXGIOutput2 methods - delegate to original
STDMETHODIMP_(BOOL) IDXGIOutput6Wrapper::SupportsOverlays() { return m_originalOutput->SupportsOverlays(); }

// IDXGIOutput3 methods - delegate to original
STDMETHODIMP IDXGIOutput6Wrapper::CheckOverlaySupport(DXGI_FORMAT EnumFormat, IUnknown* pConcernedDevice,
                                                      UINT* pFlags) {
    return m_originalOutput->CheckOverlaySupport(EnumFormat, pConcernedDevice, pFlags);
}

// IDXGIOutput4 methods - delegate to original
STDMETHODIMP IDXGIOutput6Wrapper::CheckOverlayColorSpaceSupport(DXGI_FORMAT Format, DXGI_COLOR_SPACE_TYPE ColorSpace,
                                                                IUnknown* pConcernedDevice, UINT* pFlags) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Query for IDXGIOutput4 to call CheckOverlayColorSpaceSupport
    Microsoft::WRL::ComPtr<IDXGIOutput4> output4;
    if (SUCCEEDED(m_originalOutput->QueryInterface(IID_PPV_ARGS(&output4)))) {
        return output4->CheckOverlayColorSpaceSupport(Format, ColorSpace, pConcernedDevice, pFlags);
    }
    return E_FAIL;
}

// IDXGIOutput5 methods - delegate to original
STDMETHODIMP IDXGIOutput6Wrapper::DuplicateOutput1(IUnknown* pDevice, UINT Flags, UINT SupportedFormatsCount,
                                                   const DXGI_FORMAT* pSupportedFormats,
                                                   IDXGIOutputDuplication** ppOutputDuplication) {
    return m_originalOutput->DuplicateOutput1(pDevice, Flags, SupportedFormatsCount, pSupportedFormats,
                                              ppOutputDuplication);
}

// IDXGIOutput6 methods - override GetDesc1 for HDR hiding
STDMETHODIMP IDXGIOutput6Wrapper::GetDesc1(DXGI_OUTPUT_DESC1* pDesc) {
    if (pDesc == nullptr) {
        return DXGI_ERROR_INVALID_CALL;
    }

    // Call original function
    HRESULT hr = m_originalOutput->GetDesc1(pDesc);

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

STDMETHODIMP IDXGIOutput6Wrapper::CheckHardwareCompositionSupport(UINT* pFlags) {
    return m_originalOutput->CheckHardwareCompositionSupport(pFlags);
}

// Helper function to check if a factory is a DXGIFactoryWrapper
DXGIFactoryWrapper* QueryFactoryWrapper(IDXGIFactory* factory) {
    if (factory == nullptr) {
        return nullptr;
    }

    DXGIFactoryWrapper* wrapper = nullptr;
    if (SUCCEEDED(factory->QueryInterface(IID_IDXGIFactoryWrapper, reinterpret_cast<void**>(&wrapper)))) {
        return wrapper;
    }

    return nullptr;
}

// Helper function to check if a swapchain is a DXGISwapChain4Wrapper
DXGISwapChain4Wrapper* QuerySwapChainWrapper(IUnknown* swapchain) {
    if (swapchain == nullptr) {
        return nullptr;
    }

    DXGISwapChain4Wrapper* wrapper = nullptr;
    if (SUCCEEDED(swapchain->QueryInterface(IID_IDXGISwapChain4Wrapper, reinterpret_cast<void**>(&wrapper)))) {
        return wrapper;
    }

    return nullptr;
}

}  // namespace display_commanderhooks
