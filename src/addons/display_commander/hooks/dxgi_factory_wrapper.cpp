#include "dxgi_factory_wrapper.hpp"
#include "../utils/logging.hpp"
#include "../globals.hpp"
#include "../utils/timing.hpp"
#include "../utils/general_utils.hpp"
#include "dxgi/dxgi_present_hooks.hpp"
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>
#include <dxgi1_4.h>
#include <dxgi1_5.h>
#include <dxgi1_6.h>

namespace display_commanderhooks {

namespace {
// Helper function to track present statistics (shared between Present and Present1)
void TrackPresentStatistics(
    SwapChainWrapperStats* stats,
    std::atomic<uint64_t>& last_time_ns,
    std::atomic<uint64_t>& total_calls,
    std::atomic<double>& smoothed_fps) {

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
} // anonymous namespace

// Helper function to create a swapchain wrapper from any swapchain interface
IDXGISwapChain4* CreateSwapChainWrapper(IDXGISwapChain* swapchain, SwapChainHook hookType) {
    if (swapchain == nullptr) {
        LogWarn("CreateSwapChainWrapper: swapchain is null");
        return nullptr;
    }

    // Try to query for IDXGISwapChain4
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain4;
    if (FAILED(swapchain->QueryInterface(IID_PPV_ARGS(&swapChain4)))) {
        LogWarn("CreateSwapChainWrapper: Failed to query IDXGISwapChain4 interface");
        return nullptr;
    }

    const char* hookTypeName = (hookType == SwapChainHook::Proxy) ? "Proxy" : "Native";
    LogInfo("CreateSwapChainWrapper: Creating wrapper for swapchain: 0x%p (hookType: %s)", swapchain, hookTypeName);

    return new DXGISwapChain4Wrapper(swapChain4.Get(), hookType);
}

// DXGISwapChain4Wrapper implementation
DXGISwapChain4Wrapper::DXGISwapChain4Wrapper(IDXGISwapChain4* originalSwapChain, SwapChainHook hookType)
    : m_originalSwapChain(originalSwapChain), m_refCount(1), m_swapChainHookType(hookType) {
    const char* hookTypeName = (hookType == SwapChainHook::Proxy) ? "Proxy" : "Native";
    LogInfo("DXGISwapChain4Wrapper: Created wrapper for IDXGISwapChain4 (hookType: %s)", hookTypeName);
}

STDMETHODIMP DXGISwapChain4Wrapper::QueryInterface(REFIID riid, void **ppvObject) {
    if (ppvObject == nullptr)
        return E_POINTER;

    // Support all swapchain interfaces
    if (riid == IID_IUnknown || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIDeviceSubObject) ||
        riid == __uuidof(IDXGISwapChain) || riid == __uuidof(IDXGISwapChain1) || riid == __uuidof(IDXGISwapChain2) ||
        riid == __uuidof(IDXGISwapChain3) || riid == __uuidof(IDXGISwapChain4)) {
        *ppvObject = static_cast<IDXGISwapChain4 *>(this);
        AddRef();
        return S_OK;
    }

    return m_originalSwapChain->QueryInterface(riid, ppvObject);
}

STDMETHODIMP_(ULONG) DXGISwapChain4Wrapper::AddRef() {
    return InterlockedIncrement(&m_refCount);
}

STDMETHODIMP_(ULONG) DXGISwapChain4Wrapper::Release() {
    ULONG refCount = InterlockedDecrement(&m_refCount);
    if (refCount == 0) {
        LogInfo("DXGISwapChain4Wrapper: Releasing wrapper");
        delete this;
    }
    return refCount;
}

// IDXGIObject methods - delegate to original
STDMETHODIMP DXGISwapChain4Wrapper::SetPrivateData(REFGUID Name, UINT DataSize, const void *pData) {
    return m_originalSwapChain->SetPrivateData(Name, DataSize, pData);
}

STDMETHODIMP DXGISwapChain4Wrapper::SetPrivateDataInterface(REFGUID Name, const IUnknown *pUnknown) {
    return m_originalSwapChain->SetPrivateDataInterface(Name, pUnknown);
}

STDMETHODIMP DXGISwapChain4Wrapper::GetPrivateData(REFGUID Name, UINT *pDataSize, void *pData) {
    return m_originalSwapChain->GetPrivateData(Name, pDataSize, pData);
}

STDMETHODIMP DXGISwapChain4Wrapper::GetParent(REFIID riid, void **ppParent) {
    return m_originalSwapChain->GetParent(riid, ppParent);
}

// IDXGIDeviceSubObject methods - delegate to original
STDMETHODIMP DXGISwapChain4Wrapper::GetDevice(REFIID riid, void **ppDevice) {
    return m_originalSwapChain->GetDevice(riid, ppDevice);
}

// IDXGISwapChain methods - delegate to original
STDMETHODIMP DXGISwapChain4Wrapper::Present(UINT SyncInterval, UINT Flags) {
    // Track statistics
    SwapChainWrapperStats* stats = (m_swapChainHookType == SwapChainHook::Proxy)
        ? &g_swapchain_wrapper_stats_proxy
        : &g_swapchain_wrapper_stats_native;

    TrackPresentStatistics(stats, stats->last_present_time_ns, stats->total_present_calls, stats->smoothed_present_fps);

    return m_originalSwapChain->Present(SyncInterval, Flags);
}

STDMETHODIMP DXGISwapChain4Wrapper::GetBuffer(UINT Buffer, REFIID riid, void **ppSurface) {
    return m_originalSwapChain->GetBuffer(Buffer, riid, ppSurface);
}

STDMETHODIMP DXGISwapChain4Wrapper::SetFullscreenState(BOOL Fullscreen, IDXGIOutput *pTarget) {
    return m_originalSwapChain->SetFullscreenState(Fullscreen, pTarget);
}

STDMETHODIMP DXGISwapChain4Wrapper::GetFullscreenState(BOOL *pFullscreen, IDXGIOutput **ppTarget) {
    return m_originalSwapChain->GetFullscreenState(pFullscreen, ppTarget);
}

STDMETHODIMP DXGISwapChain4Wrapper::GetDesc(DXGI_SWAP_CHAIN_DESC *pDesc) {
    return m_originalSwapChain->GetDesc(pDesc);
}

STDMETHODIMP DXGISwapChain4Wrapper::ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format, UINT SwapChainFlags) {
    return m_originalSwapChain->ResizeBuffers(BufferCount, Width, Height, Format, SwapChainFlags);
}

STDMETHODIMP DXGISwapChain4Wrapper::ResizeTarget(const DXGI_MODE_DESC *pNewTargetParameters) {
    return m_originalSwapChain->ResizeTarget(pNewTargetParameters);
}

STDMETHODIMP DXGISwapChain4Wrapper::GetContainingOutput(IDXGIOutput **ppOutput) {
    return m_originalSwapChain->GetContainingOutput(ppOutput);
}

STDMETHODIMP DXGISwapChain4Wrapper::GetFrameStatistics(DXGI_FRAME_STATISTICS *pStats) {
    return m_originalSwapChain->GetFrameStatistics(pStats);
}

STDMETHODIMP DXGISwapChain4Wrapper::GetLastPresentCount(UINT *pLastPresentCount) {
    return m_originalSwapChain->GetLastPresentCount(pLastPresentCount);
}

// IDXGISwapChain1 methods - delegate to original
STDMETHODIMP DXGISwapChain4Wrapper::GetDesc1(DXGI_SWAP_CHAIN_DESC1 *pDesc) {
    return m_originalSwapChain->GetDesc1(pDesc);
}

STDMETHODIMP DXGISwapChain4Wrapper::GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pDesc) {
    return m_originalSwapChain->GetFullscreenDesc(pDesc);
}

STDMETHODIMP DXGISwapChain4Wrapper::GetHwnd(HWND *pHwnd) {
    return m_originalSwapChain->GetHwnd(pHwnd);
}

STDMETHODIMP DXGISwapChain4Wrapper::GetCoreWindow(REFIID refiid, void **ppUnk) {
    return m_originalSwapChain->GetCoreWindow(refiid, ppUnk);
}

STDMETHODIMP DXGISwapChain4Wrapper::Present1(UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS *pPresentParameters) {
    // Track statistics
    SwapChainWrapperStats* stats = (m_swapChainHookType == SwapChainHook::Proxy)
        ? &g_swapchain_wrapper_stats_proxy
        : &g_swapchain_wrapper_stats_native;

    TrackPresentStatistics(stats, stats->last_present1_time_ns, stats->total_present1_calls, stats->smoothed_present1_fps);

    return m_originalSwapChain->Present1(SyncInterval, PresentFlags, pPresentParameters);
}

STDMETHODIMP_(BOOL) DXGISwapChain4Wrapper::IsTemporaryMonoSupported() {
    return m_originalSwapChain->IsTemporaryMonoSupported();
}

STDMETHODIMP DXGISwapChain4Wrapper::GetRestrictToOutput(IDXGIOutput **ppRestrictToOutput) {
    return m_originalSwapChain->GetRestrictToOutput(ppRestrictToOutput);
}

STDMETHODIMP DXGISwapChain4Wrapper::SetBackgroundColor(const DXGI_RGBA *pColor) {
    return m_originalSwapChain->SetBackgroundColor(pColor);
}

STDMETHODIMP DXGISwapChain4Wrapper::GetBackgroundColor(DXGI_RGBA *pColor) {
    return m_originalSwapChain->GetBackgroundColor(pColor);
}

STDMETHODIMP DXGISwapChain4Wrapper::SetRotation(DXGI_MODE_ROTATION Rotation) {
    return m_originalSwapChain->SetRotation(Rotation);
}

STDMETHODIMP DXGISwapChain4Wrapper::GetRotation(DXGI_MODE_ROTATION *pRotation) {
    return m_originalSwapChain->GetRotation(pRotation);
}

// IDXGISwapChain2 methods - delegate to original
STDMETHODIMP DXGISwapChain4Wrapper::SetSourceSize(UINT Width, UINT Height) {
    return m_originalSwapChain->SetSourceSize(Width, Height);
}

STDMETHODIMP DXGISwapChain4Wrapper::GetSourceSize(UINT *pWidth, UINT *pHeight) {
    return m_originalSwapChain->GetSourceSize(pWidth, pHeight);
}

STDMETHODIMP DXGISwapChain4Wrapper::SetMaximumFrameLatency(UINT MaxLatency) {
    return m_originalSwapChain->SetMaximumFrameLatency(MaxLatency);
}

STDMETHODIMP DXGISwapChain4Wrapper::GetMaximumFrameLatency(UINT *pMaxLatency) {
    return m_originalSwapChain->GetMaximumFrameLatency(pMaxLatency);
}

STDMETHODIMP_(HANDLE) DXGISwapChain4Wrapper::GetFrameLatencyWaitableObject() {
    return m_originalSwapChain->GetFrameLatencyWaitableObject();
}

STDMETHODIMP DXGISwapChain4Wrapper::SetMatrixTransform(const DXGI_MATRIX_3X2_F *pMatrix) {
    return m_originalSwapChain->SetMatrixTransform(pMatrix);
}

STDMETHODIMP DXGISwapChain4Wrapper::GetMatrixTransform(DXGI_MATRIX_3X2_F *pMatrix) {
    return m_originalSwapChain->GetMatrixTransform(pMatrix);
}

// IDXGISwapChain3 methods - delegate to original
STDMETHODIMP_(UINT) DXGISwapChain4Wrapper::GetCurrentBackBufferIndex() {
    return m_originalSwapChain->GetCurrentBackBufferIndex();
}

STDMETHODIMP DXGISwapChain4Wrapper::CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE ColorSpace, UINT *pColorSpaceSupport) {
    return m_originalSwapChain->CheckColorSpaceSupport(ColorSpace, pColorSpaceSupport);
}

STDMETHODIMP DXGISwapChain4Wrapper::SetColorSpace1(DXGI_COLOR_SPACE_TYPE ColorSpace) {
    return m_originalSwapChain->SetColorSpace1(ColorSpace);
}

STDMETHODIMP DXGISwapChain4Wrapper::ResizeBuffers1(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format, UINT SwapChainFlags, const UINT *pNodeMask, IUnknown *const *ppPresentQueue) {
    return m_originalSwapChain->ResizeBuffers1(BufferCount, Width, Height, Format, SwapChainFlags, pNodeMask, ppPresentQueue);
}

// IDXGISwapChain4 methods - delegate to original
STDMETHODIMP DXGISwapChain4Wrapper::SetHDRMetaData(DXGI_HDR_METADATA_TYPE Type, UINT Size, void *pMetaData) {
    return m_originalSwapChain->SetHDRMetaData(Type, Size, pMetaData);
}

DXGIFactoryWrapper::DXGIFactoryWrapper(IDXGIFactory7* originalFactory, SwapChainHook hookType)
    : m_originalFactory(originalFactory), m_refCount(1), m_swapChainHookType(hookType), m_slGetNativeInterface(nullptr), m_slUpgradeInterface(nullptr), m_commandQueueMap(nullptr) {
    const char* hookTypeName = (hookType == SwapChainHook::Proxy) ? "Proxy" : "Native";
    LogInfo("DXGIFactoryWrapper: Created wrapper for IDXGIFactory7 (hookType: %s)", hookTypeName);
}

STDMETHODIMP DXGIFactoryWrapper::QueryInterface(REFIID riid, void **ppvObject) {
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == IID_IUnknown || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIDeviceSubObject) ||
        riid == __uuidof(IDXGIFactory) || riid == __uuidof(IDXGIFactory1) || riid == __uuidof(IDXGIFactory2) ||
        riid == __uuidof(IDXGIFactory3) || riid == __uuidof(IDXGIFactory4) || riid == __uuidof(IDXGIFactory5) ||
        riid == __uuidof(IDXGIFactory6) || riid == __uuidof(IDXGIFactory7)) {
        *ppvObject = static_cast<IDXGIFactory7 *>(this);
        AddRef();
        return S_OK;
    }

    return m_originalFactory->QueryInterface(riid, ppvObject);
}

STDMETHODIMP_(ULONG) DXGIFactoryWrapper::AddRef() {
    return InterlockedIncrement(&m_refCount);
}

STDMETHODIMP_(ULONG) DXGIFactoryWrapper::Release() {
    ULONG refCount = InterlockedDecrement(&m_refCount);
    if (refCount == 0) {
        LogInfo("DXGIFactoryWrapper: Releasing wrapper");
        delete this;
    }
    return refCount;
}

// IDXGIObject methods - delegate to original
STDMETHODIMP DXGIFactoryWrapper::SetPrivateData(REFGUID Name, UINT DataSize, const void *pData) {
    return m_originalFactory->SetPrivateData(Name, DataSize, pData);
}

STDMETHODIMP DXGIFactoryWrapper::SetPrivateDataInterface(REFGUID Name, const IUnknown *pUnknown) {
    return m_originalFactory->SetPrivateDataInterface(Name, pUnknown);
}

STDMETHODIMP DXGIFactoryWrapper::GetPrivateData(REFGUID Name, UINT *pDataSize, void *pData) {
    return m_originalFactory->GetPrivateData(Name, pDataSize, pData);
}

STDMETHODIMP DXGIFactoryWrapper::GetParent(REFIID riid, void **ppParent) {
    return m_originalFactory->GetParent(riid, ppParent);
}

// IDXGIDeviceSubObject methods - GetDevice is not part of IDXGIFactory7

// IDXGIFactory methods - delegate to original
STDMETHODIMP DXGIFactoryWrapper::EnumAdapters(UINT Adapter, IDXGIAdapter **ppAdapter) {
    return m_originalFactory->EnumAdapters(Adapter, ppAdapter);
}

STDMETHODIMP DXGIFactoryWrapper::MakeWindowAssociation(HWND WindowHandle, UINT Flags) {
    return m_originalFactory->MakeWindowAssociation(WindowHandle, Flags);
}

STDMETHODIMP DXGIFactoryWrapper::GetWindowAssociation(HWND *pWindowHandle) {
    return m_originalFactory->GetWindowAssociation(pWindowHandle);
}

STDMETHODIMP DXGIFactoryWrapper::CreateSwapChain(IUnknown *pDevice, DXGI_SWAP_CHAIN_DESC *pDesc, IDXGISwapChain **ppSwapChain) {
    LogInfo("DXGIFactoryWrapper::CreateSwapChain called");

    if (ShouldInterceptSwapChainCreation()) {
        LogInfo("DXGIFactoryWrapper: Intercepting swapchain creation for Streamline compatibility");
        // TODO(user): Implement swapchain interception logic
    }

    auto result = m_originalFactory->CreateSwapChain(pDevice, pDesc, ppSwapChain);
    if (SUCCEEDED(result)) {
        auto *swapchain = *ppSwapChain;
        if (swapchain != nullptr) {
            LogInfo("DXGIFactoryWrapper::CreateSwapChain succeeded swapchain: 0x%p", swapchain);

            // Create wrapper instead of hooking
            IDXGISwapChain4* wrappedSwapChain = CreateSwapChainWrapper(swapchain, m_swapChainHookType);
            if (wrappedSwapChain != nullptr) {
                // Release the original swapchain and replace with wrapper
                swapchain->Release();
                *ppSwapChain = wrappedSwapChain;
                wrappedSwapChain->AddRef();
            }
        }
    }

    return result;
}

STDMETHODIMP DXGIFactoryWrapper::CreateSoftwareAdapter(HMODULE Module, IDXGIAdapter **ppAdapter) {
    return m_originalFactory->CreateSoftwareAdapter(Module, ppAdapter);
}

// IDXGIFactory1 methods - delegate to original
STDMETHODIMP DXGIFactoryWrapper::EnumAdapters1(UINT Adapter, IDXGIAdapter1 **ppAdapter) {
    return m_originalFactory->EnumAdapters1(Adapter, ppAdapter);
}

STDMETHODIMP_(BOOL) DXGIFactoryWrapper::IsCurrent() {
    return m_originalFactory->IsCurrent();
}

// IDXGIFactory2 methods - delegate to original
STDMETHODIMP_(BOOL) DXGIFactoryWrapper::IsWindowedStereoEnabled() {
    return m_originalFactory->IsWindowedStereoEnabled();
}

STDMETHODIMP DXGIFactoryWrapper::CreateSwapChainForHwnd(IUnknown *pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1 *pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc, IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain) {
    LogInfo("DXGIFactoryWrapper::CreateSwapChainForHwnd called");

    if (ShouldInterceptSwapChainCreation()) {
        LogInfo("DXGIFactoryWrapper: Intercepting CreateSwapChainForHwnd for Streamline compatibility");
        // TODO(user): Implement swapchain interception logic
    }

    auto result = m_originalFactory->CreateSwapChainForHwnd(pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
    if (SUCCEEDED(result)) {
        auto *swapchain = *ppSwapChain;
        if (swapchain != nullptr) {
            LogInfo("DXGIFactoryWrapper::CreateSwapChainForHwnd succeeded swapchain: 0x%p", swapchain);

            // Convert IDXGISwapChain1 to IDXGISwapChain for wrapper creation
            Microsoft::WRL::ComPtr<IDXGISwapChain> baseSwapchain;
            if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&baseSwapchain)))) {
                IDXGISwapChain4* wrappedSwapChain = CreateSwapChainWrapper(baseSwapchain.Get(), m_swapChainHookType);
                if (wrappedSwapChain != nullptr) {
                    // Release the original swapchain and replace with wrapper
                    swapchain->Release();
                    // Query wrapper for IDXGISwapChain1
                    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
                    if (SUCCEEDED(wrappedSwapChain->QueryInterface(IID_PPV_ARGS(&swapChain1)))) {
                        *ppSwapChain = swapChain1.Detach();
                    } else {
                        // Fallback: keep original if query fails
                        wrappedSwapChain->Release();
                        *ppSwapChain = swapchain;
                        swapchain->AddRef();
                    }
                }
            }
        }
    }

    return result;
}

STDMETHODIMP DXGIFactoryWrapper::CreateSwapChainForCoreWindow(IUnknown *pDevice, IUnknown *pWindow, const DXGI_SWAP_CHAIN_DESC1 *pDesc, IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain) {
    LogInfo("DXGIFactoryWrapper::CreateSwapChainForCoreWindow called");

    if (ShouldInterceptSwapChainCreation()) {
        LogInfo("DXGIFactoryWrapper: Intercepting CreateSwapChainForCoreWindow for Streamline compatibility");
        // TODO(user): Implement swapchain interception logic
    }

    auto result = m_originalFactory->CreateSwapChainForCoreWindow(pDevice, pWindow, pDesc, pRestrictToOutput, ppSwapChain);
    if (SUCCEEDED(result)) {
        auto *swapchain = *ppSwapChain;
        if (swapchain != nullptr) {
            LogInfo("DXGIFactoryWrapper::CreateSwapChainForCoreWindow succeeded swapchain: 0x%p", swapchain);

            // Convert IDXGISwapChain1 to IDXGISwapChain for wrapper creation
            Microsoft::WRL::ComPtr<IDXGISwapChain> baseSwapchain;
            if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&baseSwapchain)))) {
                IDXGISwapChain4* wrappedSwapChain = CreateSwapChainWrapper(baseSwapchain.Get(), m_swapChainHookType);
                if (wrappedSwapChain != nullptr) {
                    // Release the original swapchain and replace with wrapper
                    swapchain->Release();
                    // Query wrapper for IDXGISwapChain1
                    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
                    if (SUCCEEDED(wrappedSwapChain->QueryInterface(IID_PPV_ARGS(&swapChain1)))) {
                        *ppSwapChain = swapChain1.Detach();
                    } else {
                        // Fallback: keep original if query fails
                        wrappedSwapChain->Release();
                        *ppSwapChain = swapchain;
                        swapchain->AddRef();
                    }
                }
            }
        }
    }

    return result;
}

STDMETHODIMP DXGIFactoryWrapper::GetSharedResourceAdapterLuid(HANDLE hResource, LUID *pLuid) {
    return m_originalFactory->GetSharedResourceAdapterLuid(hResource, pLuid);
}

STDMETHODIMP DXGIFactoryWrapper::RegisterStereoStatusWindow(HWND WindowHandle, UINT wMsg, DWORD *pdwCookie) {
    return m_originalFactory->RegisterStereoStatusWindow(WindowHandle, wMsg, pdwCookie);
}

STDMETHODIMP DXGIFactoryWrapper::RegisterStereoStatusEvent(HANDLE hEvent, DWORD *pdwCookie) {
    return m_originalFactory->RegisterStereoStatusEvent(hEvent, pdwCookie);
}

STDMETHODIMP_(void) DXGIFactoryWrapper::UnregisterStereoStatus(DWORD dwCookie) {
    m_originalFactory->UnregisterStereoStatus(dwCookie);
}

STDMETHODIMP DXGIFactoryWrapper::RegisterOcclusionStatusWindow(HWND WindowHandle, UINT wMsg, DWORD *pdwCookie) {
    return m_originalFactory->RegisterOcclusionStatusWindow(WindowHandle, wMsg, pdwCookie);
}

STDMETHODIMP DXGIFactoryWrapper::RegisterOcclusionStatusEvent(HANDLE hEvent, DWORD *pdwCookie) {
    return m_originalFactory->RegisterOcclusionStatusEvent(hEvent, pdwCookie);
}

STDMETHODIMP_(void) DXGIFactoryWrapper::UnregisterOcclusionStatus(DWORD dwCookie) {
    m_originalFactory->UnregisterOcclusionStatus(dwCookie);
}

STDMETHODIMP DXGIFactoryWrapper::CreateSwapChainForComposition(IUnknown *pDevice, const DXGI_SWAP_CHAIN_DESC1 *pDesc, IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain) {
    LogInfo("DXGIFactoryWrapper::CreateSwapChainForComposition called");

    if (ShouldInterceptSwapChainCreation()) {
        LogInfo("DXGIFactoryWrapper: Intercepting CreateSwapChainForComposition for Streamline compatibility");
        // TODO(user): Implement swapchain interception logic
    }

    auto result = m_originalFactory->CreateSwapChainForComposition(pDevice, pDesc, pRestrictToOutput, ppSwapChain);
    if (SUCCEEDED(result)) {
        auto *swapchain = *ppSwapChain;
        if (swapchain != nullptr) {
            LogInfo("DXGIFactoryWrapper::CreateSwapChainForComposition succeeded swapchain: 0x%p", swapchain);

            // Convert IDXGISwapChain1 to IDXGISwapChain for wrapper creation
            Microsoft::WRL::ComPtr<IDXGISwapChain> baseSwapchain;
            if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&baseSwapchain)))) {
                IDXGISwapChain4* wrappedSwapChain = CreateSwapChainWrapper(baseSwapchain.Get(), m_swapChainHookType);
                if (wrappedSwapChain != nullptr) {
                    // Release the original swapchain and replace with wrapper
                    swapchain->Release();
                    // Query wrapper for IDXGISwapChain1
                    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
                    if (SUCCEEDED(wrappedSwapChain->QueryInterface(IID_PPV_ARGS(&swapChain1)))) {
                        *ppSwapChain = swapChain1.Detach();
                    } else {
                        // Fallback: keep original if query fails
                        wrappedSwapChain->Release();
                        *ppSwapChain = swapchain;
                        swapchain->AddRef();
                    }
                }
            }
        }
    }

    return result;
}

// IDXGIFactory3 methods - delegate to original
STDMETHODIMP_(UINT) DXGIFactoryWrapper::GetCreationFlags() {
    return m_originalFactory->GetCreationFlags();
}

// IDXGIFactory4 methods - delegate to original
STDMETHODIMP DXGIFactoryWrapper::EnumAdapterByLuid(LUID AdapterLuid, REFIID riid, void **ppvAdapter) {
    return m_originalFactory->EnumAdapterByLuid(AdapterLuid, riid, ppvAdapter);
}

STDMETHODIMP DXGIFactoryWrapper::EnumWarpAdapter(REFIID riid, void **ppvAdapter) {
    return m_originalFactory->EnumWarpAdapter(riid, ppvAdapter);
}

// IDXGIFactory5 methods - delegate to original
STDMETHODIMP DXGIFactoryWrapper::CheckFeatureSupport(DXGI_FEATURE Feature, void *pFeatureSupportData, UINT FeatureSupportDataSize) {
    return m_originalFactory->CheckFeatureSupport(Feature, pFeatureSupportData, FeatureSupportDataSize);
}

// IDXGIFactory6 methods - delegate to original
STDMETHODIMP DXGIFactoryWrapper::EnumAdapterByGpuPreference(UINT Adapter, DXGI_GPU_PREFERENCE GpuPreference, REFIID riid, void **ppvAdapter) {
    return m_originalFactory->EnumAdapterByGpuPreference(Adapter, GpuPreference, riid, ppvAdapter);
}

// IDXGIFactory7 methods - delegate to original
STDMETHODIMP DXGIFactoryWrapper::RegisterAdaptersChangedEvent(HANDLE hEvent, DWORD *pdwCookie) {
    return m_originalFactory->RegisterAdaptersChangedEvent(hEvent, pdwCookie);
}

STDMETHODIMP DXGIFactoryWrapper::UnregisterAdaptersChangedEvent(DWORD dwCookie) {
    return m_originalFactory->UnregisterAdaptersChangedEvent(dwCookie);
}

// Additional methods for Streamline integration
void DXGIFactoryWrapper::SetSLGetNativeInterface(void* slGetNativeInterface) {
    m_slGetNativeInterface = slGetNativeInterface;
}

void DXGIFactoryWrapper::SetSLUpgradeInterface(void* slUpgradeInterface) {
    m_slUpgradeInterface = slUpgradeInterface;
}

void DXGIFactoryWrapper::SetCommandQueueMap(void* commandQueueMap) {
    m_commandQueueMap = commandQueueMap;
}

bool DXGIFactoryWrapper::ShouldInterceptSwapChainCreation() const {
    // Check if Streamline integration is enabled
    return m_slGetNativeInterface != nullptr && m_slUpgradeInterface != nullptr;
}

} // namespace display_commanderhooks
