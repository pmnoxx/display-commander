#pragma once

#include <dxgi1_6.h>
#include <wrl/client.h>
#include <atomic>
#include "../globals.hpp"

namespace display_commanderhooks {

/**
 * SwapChainHook - Enum to distinguish between proxy and native swapchain hooks
 */
enum class SwapChainHook {
    Proxy,  // Proxy swapchain (ReShade wrapper)
    Native  // Native swapchain (game's original)
};

/**
 * DXGISwapChain4Wrapper - A wrapper for IDXGISwapChain4 that integrates with Streamline
 *
 * This wrapper proxies all swapchain operations while maintaining the hook type
 * information for Combine compatibility.
 */
class DXGISwapChain4Wrapper : public IDXGISwapChain4 {
private:
    Microsoft::WRL::ComPtr<IDXGISwapChain4> m_originalSwapChain;
    volatile LONG m_refCount;
    SwapChainHook m_swapChainHookType;

public:
    explicit DXGISwapChain4Wrapper(IDXGISwapChain4* originalSwapChain, SwapChainHook hookType);
    virtual ~DXGISwapChain4Wrapper() = default;

    // IUnknown methods
    STDMETHOD(QueryInterface)(REFIID riid, void **ppvObject) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    // IDXGIObject methods
    STDMETHOD(SetPrivateData)(REFGUID Name, UINT DataSize, const void *pData) override;
    STDMETHOD(SetPrivateDataInterface)(REFGUID Name, const IUnknown *pUnknown) override;
    STDMETHOD(GetPrivateData)(REFGUID Name, UINT *pDataSize, void *pData) override;
    STDMETHOD(GetParent)(REFIID riid, void **ppParent) override;

    // IDXGIDeviceSubObject methods
    STDMETHOD(GetDevice)(REFIID riid, void **ppDevice) override;

    // IDXGISwapChain methods
    STDMETHOD(Present)(UINT SyncInterval, UINT Flags) override;
    STDMETHOD(GetBuffer)(UINT Buffer, REFIID riid, void **ppSurface) override;
    STDMETHOD(SetFullscreenState)(BOOL Fullscreen, IDXGIOutput *pTarget) override;
    STDMETHOD(GetFullscreenState)(BOOL *pFullscreen, IDXGIOutput **ppTarget) override;
    STDMETHOD(GetDesc)(DXGI_SWAP_CHAIN_DESC *pDesc) override;
    STDMETHOD(ResizeBuffers)(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format, UINT SwapChainFlags) override;
    STDMETHOD(ResizeTarget)(const DXGI_MODE_DESC *pNewTargetParameters) override;
    STDMETHOD(GetContainingOutput)(IDXGIOutput **ppOutput) override;
    STDMETHOD(GetFrameStatistics)(DXGI_FRAME_STATISTICS *pStats) override;
    STDMETHOD(GetLastPresentCount)(UINT *pLastPresentCount) override;

    // IDXGISwapChain1 methods
    STDMETHOD(GetDesc1)(DXGI_SWAP_CHAIN_DESC1 *pDesc) override;
    STDMETHOD(GetFullscreenDesc)(DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pDesc) override;
    STDMETHOD(GetHwnd)(HWND *pHwnd) override;
    STDMETHOD(GetCoreWindow)(REFIID refiid, void **ppUnk) override;
    STDMETHOD(Present1)(UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS *pPresentParameters) override;
    STDMETHOD_(BOOL, IsTemporaryMonoSupported)() override;
    STDMETHOD(GetRestrictToOutput)(IDXGIOutput **ppRestrictToOutput) override;
    STDMETHOD(SetBackgroundColor)(const DXGI_RGBA *pColor) override;
    STDMETHOD(GetBackgroundColor)(DXGI_RGBA *pColor) override;
    STDMETHOD(SetRotation)(DXGI_MODE_ROTATION Rotation) override;
    STDMETHOD(GetRotation)(DXGI_MODE_ROTATION *pRotation) override;

    // IDXGISwapChain2 methods
    STDMETHOD(SetSourceSize)(UINT Width, UINT Height) override;
    STDMETHOD(GetSourceSize)(UINT *pWidth, UINT *pHeight) override;
    STDMETHOD(SetMaximumFrameLatency)(UINT MaxLatency) override;
    STDMETHOD(GetMaximumFrameLatency)(UINT *pMaxLatency) override;
    STDMETHOD_(HANDLE, GetFrameLatencyWaitableObject)() override;
    STDMETHOD(SetMatrixTransform)(const DXGI_MATRIX_3X2_F *pMatrix) override;
    STDMETHOD(GetMatrixTransform)(DXGI_MATRIX_3X2_F *pMatrix) override;

    // IDXGISwapChain3 methods
    STDMETHOD_(UINT, GetCurrentBackBufferIndex)() override;
    STDMETHOD(CheckColorSpaceSupport)(DXGI_COLOR_SPACE_TYPE ColorSpace, UINT *pColorSpaceSupport) override;
    STDMETHOD(SetColorSpace1)(DXGI_COLOR_SPACE_TYPE ColorSpace) override;
    STDMETHOD(ResizeBuffers1)(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format, UINT SwapChainFlags, const UINT *pNodeMask, IUnknown *const *ppPresentQueue) override;

    // IDXGISwapChain4 methods
    STDMETHOD(SetHDRMetaData)(DXGI_HDR_METADATA_TYPE Type, UINT Size, void *pMetaData) override;
};

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
    SwapChainHook m_swapChainHookType;

public:
    explicit DXGIFactoryWrapper(IDXGIFactory7* originalFactory, SwapChainHook hookType = SwapChainHook::Native);
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

private:
    // Streamline integration pointers
    void* m_slGetNativeInterface;
    void* m_slUpgradeInterface;
    void* m_commandQueueMap;

    // Helper method to check if we should intercept swapchain creation
    bool ShouldInterceptSwapChainCreation() const;
};

// Helper function to create a swapchain wrapper
IDXGISwapChain4* CreateSwapChainWrapper(IDXGISwapChain* swapchain, SwapChainHook hookType);

// Helper function to flush command queue from swapchain using native DirectX APIs (DX11 only)
void FlushCommandQueueFromSwapchain(IDXGISwapChain* swapchain, DeviceTypeDC device_type);

} // namespace display_commanderhooks
