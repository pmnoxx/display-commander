# DXGI VTable Indices Documentation

This document provides the correct vtable indices for DXGI interfaces, verified against the official Windows SDK headers.

## IDXGISwapChain3 VTable Structure

The `IDXGISwapChain3` interface inherits from `IDXGISwapChain2` → `IDXGISwapChain1` → `IDXGISwapChain` → `IDXGIDeviceSubObject` → `IDXGIObject` → `IUnknown`.

### Complete VTable Index Reference

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
| 17 | IDXGISwapChain | GetLastPresentCount | Get last present count |
| 18 | IDXGISwapChain1 | GetDesc1 | Get swapchain description (v1) |
| 19 | IDXGISwapChain1 | GetFullscreenDesc | Get fullscreen description |
| 20 | IDXGISwapChain1 | GetHwnd | Get window handle |
| 21 | IDXGISwapChain1 | GetCoreWindow | Get core window |
| 22 | IDXGISwapChain1 | Present1 | Present with parameters |
| 23 | IDXGISwapChain1 | IsTemporaryMonoSupported | Check mono support |
| 24 | IDXGISwapChain1 | GetRestrictToOutput | Get restricted output |
| 25 | IDXGISwapChain1 | SetBackgroundColor | Set background color |
| 26 | IDXGISwapChain1 | GetBackgroundColor | Get background color |
| 27 | IDXGISwapChain1 | SetRotation | Set rotation |
| 28 | IDXGISwapChain1 | GetRotation | Get rotation |
| 29 | IDXGISwapChain2 | SetSourceSize | Set source size |
| 30 | IDXGISwapChain2 | GetSourceSize | Get source size |
| 31 | IDXGISwapChain2 | SetMaximumFrameLatency | Set max frame latency |
| 32 | IDXGISwapChain2 | GetMaximumFrameLatency | Get max frame latency |
| 33 | IDXGISwapChain2 | GetFrameLatencyWaitableObject | Get latency waitable object |
| 34 | IDXGISwapChain2 | SetMatrixTransform | Set matrix transform |
| 35 | IDXGISwapChain2 | GetMatrixTransform | Get matrix transform |
| **36** | **IDXGISwapChain3** | **GetCurrentBackBufferIndex** | **Get current back buffer index** |
| **37** | **IDXGISwapChain3** | **CheckColorSpaceSupport** | **Check color space support** ⭐ |
| **38** | **IDXGISwapChain3** | **SetColorSpace1** | **Set color space** |
| **39** | **IDXGISwapChain3** | **ResizeBuffers1** | **Resize buffers with parameters** |

## Verification Sources

### Windows SDK Header Files
- **Primary Source**: `C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared\dxgi1_4.h`
- **Interface Definition**: `IDXGISwapChain3` interface (lines ~200-400)
- **VTable Structure**: `IDXGISwapChain3Vtbl` struct definition

### Key Verification Points

1. **Interface Inheritance Chain**:
   ```
   IDXGISwapChain3 : public IDXGISwapChain2
   IDXGISwapChain2 : public IDXGISwapChain1
   IDXGISwapChain1 : public IDXGISwapChain
   IDXGISwapChain : public IDXGIDeviceSubObject
   IDXGIDeviceSubObject : public IDXGIObject
   IDXGIObject : public IUnknown
   ```

2. **Method Declaration Order** (from dxgi1_4.h):
   ```cpp
   // IDXGISwapChain3 specific methods
   virtual UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex(void) = 0;
   virtual HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(
       DXGI_COLOR_SPACE_TYPE ColorSpace, UINT *pColorSpaceSupport) = 0;
   virtual HRESULT STDMETHODCALLTYPE SetColorSpace1(
       DXGI_COLOR_SPACE_TYPE ColorSpace) = 0;
   virtual HRESULT STDMETHODCALLTYPE ResizeBuffers1(...) = 0;
   ```

3. **VTable Structure** (from dxgi1_4.h):
   ```cpp
   typedef struct IDXGISwapChain3Vtbl {
       // ... inherited methods (indices 0-35) ...
       DECLSPEC_XFGVIRT(IDXGISwapChain3, GetCurrentBackBufferIndex)  // Index 36
       DECLSPEC_XFGVIRT(IDXGISwapChain3, CheckColorSpaceSupport)     // Index 37
       DECLSPEC_XFGVIRT(IDXGISwapChain3, SetColorSpace1)             // Index 38
       DECLSPEC_XFGVIRT(IDXGISwapChain3, ResizeBuffers1)             // Index 39
   } IDXGISwapChain3Vtbl;
   ```

## Implementation Notes

### Correct Usage in Code
```cpp
// ✅ CORRECT: Use vtable index 37 for CheckColorSpaceSupport
if (vtable[37] != nullptr) {
    if (MH_CreateHook(vtable[37], IDXGISwapChain_CheckColorSpaceSupport_Detour,
                     (LPVOID *)&IDXGISwapChain_CheckColorSpaceSupport_Original) != MH_OK) {
        LogError("Failed to create IDXGISwapChain3::CheckColorSpaceSupport hook");
        return false;
    }
}
```

### Common Mistakes
```cpp
// ❌ WRONG: Using incorrect vtable index 27
if (vtable[27] != nullptr) {  // This will never work!
    // CheckColorSpaceSupport is not at index 27
}
```

## Testing and Verification

To verify the correct vtable index:

1. **Debug Logging**:
   ```cpp
   LogInfo("Vtable[36] = 0x%p (GetCurrentBackBufferIndex)", vtable[36]);
   LogInfo("Vtable[37] = 0x%p (CheckColorSpaceSupport)", vtable[37]);
   LogInfo("Vtable[38] = 0x%p (SetColorSpace1)", vtable[38]);
   ```

2. **Interface Query Test**:
   ```cpp
   com_ptr<IDXGISwapChain3> swapchain3;
   if (SUCCEEDED(swapchain->QueryInterface(&swapchain3))) {
       LogInfo("Swapchain supports IDXGISwapChain3 - vtable hooking should work");
   }
   ```

3. **Hook Verification**:
   ```cpp
   // Add this to your detour function
   static bool first_call = true;
   if (first_call) {
       LogInfo("*** CheckColorSpaceSupport HOOK CALLED! ***");
       first_call = false;
   }
   ```

## References

- **Microsoft Documentation**: [IDXGISwapChain3 Interface](https://docs.microsoft.com/en-us/windows/win32/api/dxgi1_4/nn-dxgi1_4-idxgiswapchain3)
- **Windows SDK**: `dxgi1_4.h` header file
- **Interface IID**: `IID_IDXGISwapChain3 = {0x94d99bdb,0xf1f8,0x4ab0,0xb2,0x36,0x7d,0xa0,0x17,0x0e,0xda,0xb1}`

---

**Last Updated**: December 2024
**Verified Against**: Windows SDK 10.0.26100.0
**Status**: ✅ Verified and Correct
