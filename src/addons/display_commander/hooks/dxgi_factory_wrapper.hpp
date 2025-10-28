#pragma once

#include <dxgi1_6.h>
#include <wrl/client.h>
#include <atomic>

namespace display_commanderhooks {

/**
 * DXGIFactoryWrapper - A wrapper for IDXGIFactory7 that integrates with Streamline
 *
 * This wrapper intercepts swapchain creation calls to ensure they don't pass
 * active command queues that could interfere with Streamline's frame generation.
 */
class DXGIFactoryWrapper : public IDXGIFactory7 {
private:
    Microsoft::WRL::ComPtr<IDXGIFactory7> m_originalFactory;
    volatile LONG m_refCount;

public:
    explicit DXGIFactoryWrapper(IDXGIFactory7* originalFactory);
    virtual ~DXGIFactoryWrapper() = default;

    // IUnknown methods
    STDMETHOD(QueryInterface)(REFIID riid, void **ppvObject) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    // IDXGIObject methods
    STDMETHOD(SetPrivateData)(REFGUID Name, UINT DataSize, const void *pData) override;
    STDMETHOD(SetPrivateDataInterface)(REFGUID Name, const IUnknown *pUnknown) override;
    STDMETHOD(GetPrivateData)(REFGUID Name, UINT *pDataSize, void *pData) override;
    STDMETHOD(GetParent)(REFIID riid, void **ppParent) override;

    // IDXGIDeviceSubObject methods - GetDevice is not part of IDXGIFactory7

    // IDXGIFactory methods
    STDMETHOD(EnumAdapters)(UINT Adapter, IDXGIAdapter **ppAdapter) override;
    STDMETHOD(MakeWindowAssociation)(HWND WindowHandle, UINT Flags) override;
    STDMETHOD(GetWindowAssociation)(HWND *pWindowHandle) override;
    STDMETHOD(CreateSwapChain)(IUnknown *pDevice, DXGI_SWAP_CHAIN_DESC *pDesc, IDXGISwapChain **ppSwapChain) override;
    STDMETHOD(CreateSoftwareAdapter)(HMODULE Module, IDXGIAdapter **ppAdapter) override;

    // IDXGIFactory1 methods
    STDMETHOD(EnumAdapters1)(UINT Adapter, IDXGIAdapter1 **ppAdapter) override;
    STDMETHOD_(BOOL, IsCurrent)() override;

    // IDXGIFactory2 methods
    STDMETHOD_(BOOL, IsWindowedStereoEnabled)() override;
    STDMETHOD(CreateSwapChainForHwnd)(IUnknown *pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1 *pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc, IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain) override;
    STDMETHOD(CreateSwapChainForCoreWindow)(IUnknown *pDevice, IUnknown *pWindow, const DXGI_SWAP_CHAIN_DESC1 *pDesc, IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain) override;
    STDMETHOD(GetSharedResourceAdapterLuid)(HANDLE hResource, LUID *pLuid) override;
    STDMETHOD(RegisterStereoStatusWindow)(HWND WindowHandle, UINT wMsg, DWORD *pdwCookie) override;
    STDMETHOD(RegisterStereoStatusEvent)(HANDLE hEvent, DWORD *pdwCookie) override;
    STDMETHOD_(void, UnregisterStereoStatus)(DWORD dwCookie) override;
    STDMETHOD(RegisterOcclusionStatusWindow)(HWND WindowHandle, UINT wMsg, DWORD *pdwCookie) override;
    STDMETHOD(RegisterOcclusionStatusEvent)(HANDLE hEvent, DWORD *pdwCookie) override;
    STDMETHOD_(void, UnregisterOcclusionStatus)(DWORD dwCookie) override;
    STDMETHOD(CreateSwapChainForComposition)(IUnknown *pDevice, const DXGI_SWAP_CHAIN_DESC1 *pDesc, IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain) override;

    // IDXGIFactory3 methods
    STDMETHOD_(UINT, GetCreationFlags)() override;

    // IDXGIFactory4 methods
    STDMETHOD(EnumAdapterByLuid)(LUID AdapterLuid, REFIID riid, void **ppvAdapter) override;
    STDMETHOD(EnumWarpAdapter)(REFIID riid, void **ppvAdapter) override;

    // IDXGIFactory5 methods
    STDMETHOD(CheckFeatureSupport)(DXGI_FEATURE Feature, void *pFeatureSupportData, UINT FeatureSupportDataSize) override;

    // IDXGIFactory6 methods
    STDMETHOD(EnumAdapterByGpuPreference)(UINT Adapter, DXGI_GPU_PREFERENCE GpuPreference, REFIID riid, void **ppvAdapter) override;

    // IDXGIFactory7 methods
    STDMETHOD(RegisterAdaptersChangedEvent)(HANDLE hEvent, DWORD *pdwCookie) override;
    STDMETHOD(UnregisterAdaptersChangedEvent)(DWORD dwCookie) override;

    // Additional methods for Streamline integration
    void SetSLGetNativeInterface(void* slGetNativeInterface);
    void SetSLUpgradeInterface(void* slUpgradeInterface);
    void SetCommandQueueMap(void* commandQueueMap);

    // Swapchain hooking method
    void hookToNativeSwapchain(IDXGISwapChain* swapchain);

private:
    // Streamline integration pointers
    void* m_slGetNativeInterface;
    void* m_slUpgradeInterface;
    void* m_commandQueueMap;

    // Helper method to check if we should intercept swapchain creation
    bool ShouldInterceptSwapChainCreation() const;
};

} // namespace display_commanderhooks
