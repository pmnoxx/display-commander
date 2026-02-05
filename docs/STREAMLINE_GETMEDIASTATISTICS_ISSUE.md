# Streamline GetMediaStatistics Issue Analysis

## Problem Statement

DXGI `GetFrameStatisticsMedia` (via `IDXGISwapChainMedia::GetFrameStatisticsMedia`) fails when a game uses NVIDIA Streamline, even though the underlying native swapchain supports this interface.

## Root Cause Analysis

### Architecture Overview

When Streamline is active, the swapchain hierarchy becomes:

```
Game's Native Swapchain (supports IDXGISwapChainMedia)
    ↓ wrapped by
Streamline's Proxy Swapchain (sl::interposer::DXGISwapChain)
    ↓ potentially wrapped by
Display Commander's Wrapper (DXGISwapChain4Wrapper)
```

### Streamline's QueryInterface Implementation

Looking at `external-src/streamline/source/core/sl.interposer/dxgi/dxgiSwapchain.cpp`:

```106:127:external-src/streamline/source/core/sl.interposer/dxgi/dxgiSwapchain.cpp
HRESULT STDMETHODCALLTYPE DXGISwapChain::QueryInterface(REFIID riid, void** ppvObj)
{
    if (ppvObj == nullptr)
        return E_POINTER;

    // SL Special case, we are requesting base interface
    if (riid == __uuidof(StreamlineRetrieveBaseInterface))
    {
        *ppvObj = m_base;
        m_base->AddRef();
        return S_OK;
    }

    if (checkAndUpgradeInterface(riid))
    {
        AddRef();
        *ppvObj = this;
        return S_OK;
    }

    return m_base->QueryInterface(riid, ppvObj);
}
```

### The Issue

1. **Interface Not Recognized**: `IDXGISwapChainMedia` is not part of the standard swapchain interface hierarchy (`IDXGISwapChain` → `IDXGISwapChain1` → ... → `IDXGISwapChain4`). It's a separate interface that can be queried from `IDXGISwapChain1`.

2. **Forwarding Chain**: When querying for `IDXGISwapChainMedia`:
   - Display Commander's wrapper forwards to Streamline's proxy (line 130 in `dxgi_factory_wrapper.cpp`)
   - Streamline's proxy should forward to `m_base->QueryInterface(riid, ppvObj)` (line 126)
   - However, this forwarding may fail if:
     - The proxy's `m_base` pointer doesn't properly support the interface
     - There's a reference counting or lifetime issue
     - The interface query requires a specific swapchain state that the proxy doesn't maintain

3. **Missing Interface Support**: Streamline's `checkAndUpgradeInterface` only handles swapchain interfaces up to `IDXGISwapChain4`. It doesn't handle auxiliary interfaces like `IDXGISwapChainMedia`, which are queried separately.

### Why It Fails

The most likely failure points:

1. **QueryInterface Forwarding**: When Streamline's proxy forwards `QueryInterface` for `IDXGISwapChainMedia` to `m_base`, the native swapchain might require the query to be made on a specific interface version (e.g., `IDXGISwapChain1`), but the proxy forwards it directly to the base `IDXGISwapChain*`.

2. **Interface State**: `IDXGISwapChainMedia` may require the swapchain to be in a specific state (e.g., after at least one `Present` call), and the proxy might not maintain this state correctly.

3. **Reference Counting**: The proxy's reference counting might interfere with the interface query, causing the native swapchain to return an error.

## Solution Approaches

### Option 1: Use Streamline's Base Interface Retrieval (Recommended)

Streamline provides a special mechanism to retrieve the base (native) interface. The interface is defined in `external-src/streamline/source/core/sl.api/internal.h`:

```cpp
// GUID: ADEC44E2-61F0-45C3-AD9F-1B37379284FF
struct DECLSPEC_UUID("ADEC44E2-61F0-45C3-AD9F-1B37379284FF") StreamlineRetrieveBaseInterface : IUnknown
{
};
```

Streamline's proxy swapchain handles this in its `QueryInterface` (line 112-116 of `dxgiSwapchain.cpp`):

```cpp
if (riid == __uuidof(StreamlineRetrieveBaseInterface))
{
    *ppvObj = m_base;
    m_base->AddRef();
    return S_OK;
}
```

**Implementation**: Modify `GetIndependentFlipState` in `dxgi/dxgi_management.cpp` to:

1. Check if the swapchain is a Streamline proxy by querying for `StreamlineRetrieveBaseInterface`
2. If so, retrieve the base interface using this mechanism
3. Query `IDXGISwapChainMedia` on the base interface directly

### Option 2: Query Through IDXGISwapChain1

Instead of querying `IDXGISwapChainMedia` directly, first get `IDXGISwapChain1` from the base interface, then query `IDXGISwapChainMedia` from that.

### Option 3: Bypass Wrappers for Media Queries

When Streamline is detected, maintain a reference to the native swapchain and use it directly for media statistics queries, bypassing all wrapper layers.

## Recommended Implementation

The cleanest solution is to use Streamline's base interface retrieval mechanism:

```cpp
// Forward declaration for Streamline's base interface retrieval
// GUID: ADEC44E2-61F0-45C3-AD9F-1B37379284FF
struct DECLSPEC_UUID("ADEC44E2-61F0-45C3-AD9F-1B37379284FF") StreamlineRetrieveBaseInterface : IUnknown {};

DxgiBypassMode GetIndependentFlipState(IDXGISwapChain *dxgi_swapchain) {
    // ... existing null check ...

    // Try to get base interface if this is a Streamline proxy
    Microsoft::WRL::ComPtr<IDXGISwapChain> base_swapchain;
    {
        // Check if this is a Streamline proxy by querying for StreamlineRetrieveBaseInterface
        IUnknown* base_unknown = nullptr;
        if (SUCCEEDED(dxgi_swapchain->QueryInterface(__uuidof(StreamlineRetrieveBaseInterface), reinterpret_cast<void**>(&base_unknown)))) {
            // This is a Streamline proxy, use the base interface
            base_unknown->QueryInterface(IID_PPV_ARGS(&base_swapchain));
            base_unknown->Release();
        } else {
            // Not a Streamline proxy, use the swapchain directly
            base_swapchain = dxgi_swapchain;
        }
    }

    // Now query IDXGISwapChain1 from the base swapchain
    Microsoft::WRL::ComPtr<IDXGISwapChain1> sc1;
    HRESULT hr = base_swapchain->QueryInterface(IID_PPV_ARGS(&sc1));
    // ... rest of implementation ...
}
```

## Testing Considerations

1. **Verify with Streamline**: Test with games that use Streamline (DLSS, DLSS-G, etc.)
2. **Verify without Streamline**: Ensure the fix doesn't break functionality when Streamline is not present
3. **Multiple Wrapper Layers**: Test with both Display Commander wrapper and Streamline proxy active
4. **Edge Cases**: Test with swapchains that don't support `IDXGISwapChainMedia` to ensure proper error handling

## Related Code Locations

- **Streamline Proxy**: `external-src/streamline/source/core/sl.interposer/dxgi/dxgiSwapchain.cpp`
- **Display Commander Query**: `src/addons/display_commander/dxgi/dxgi_management.cpp` (GetIndependentFlipState)
- **Display Commander Wrapper**: `src/addons/display_commander/hooks/dxgi_factory_wrapper.cpp` (DXGISwapChain4Wrapper::QueryInterface)
