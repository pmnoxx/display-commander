#include "dxgi_factory_wrapper.hpp"
#include "../utils/logging.hpp"
#include "dxgi/dxgi_present_hooks.hpp"
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>
#include <dxgi1_4.h>
#include <dxgi1_5.h>
#include <dxgi1_6.h>

namespace display_commanderhooks {

DXGIFactoryWrapper::DXGIFactoryWrapper(IDXGIFactory7* originalFactory)
    : m_originalFactory(originalFactory), m_refCount(1), m_slGetNativeInterface(nullptr), m_slUpgradeInterface(nullptr), m_commandQueueMap(nullptr) {
    LogInfo("DXGIFactoryWrapper: Created wrapper for IDXGIFactory7");
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

            hookToNativeSwapchain(swapchain);
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

            // Convert IDXGISwapChain1 to IDXGISwapChain for hooking
            Microsoft::WRL::ComPtr<IDXGISwapChain> baseSwapchain;
            if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&baseSwapchain)))) {
                hookToNativeSwapchain(baseSwapchain.Get());
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

            // Convert IDXGISwapChain1 to IDXGISwapChain for hooking
            Microsoft::WRL::ComPtr<IDXGISwapChain> baseSwapchain;
            if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&baseSwapchain)))) {
                hookToNativeSwapchain(baseSwapchain.Get());
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

            // Convert IDXGISwapChain1 to IDXGISwapChain for hooking
            Microsoft::WRL::ComPtr<IDXGISwapChain> baseSwapchain;
            if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&baseSwapchain)))) {
                hookToNativeSwapchain(baseSwapchain.Get());
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

void DXGIFactoryWrapper::hookToNativeSwapchain(IDXGISwapChain* swapchain) {
    if (swapchain == nullptr) {
        LogWarn("DXGIFactoryWrapper::hookToNativeSwapchain: swapchain is null");
        return;
    }

    LogInfo("DXGIFactoryWrapper::hookToNativeSwapchain: Attempting to hook swapchain: 0x%p", swapchain);

    // Use the existing DXGI hooking infrastructure
    if (display_commanderhooks::dxgi::HookSwapchainNative(swapchain)) {
        LogInfo("DXGIFactoryWrapper::hookToNativeSwapchain: Successfully hooked swapchain: 0x%p", swapchain);
    } else {
        LogWarn("DXGIFactoryWrapper::hookToNativeSwapchain: Failed to hook swapchain: 0x%p", swapchain);
    }
}

} // namespace display_commanderhooks
