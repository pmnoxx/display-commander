# DX11 Proxy - Game Content Copying Limitations

## Crash Fix Summary

The "Display Game Content (Start Copying)" button was causing crashes due to:

### Issues Fixed:
1. **❌ Wrong Pointer Type**: Was directly casting `g_last_swapchain_ptr` to `IDXGISwapChain*`
   - **✅ Fixed**: Now properly casts to `reshade::api::swapchain*` first, then gets native handle

2. **❌ Wrong Cast Method**: ReShade's `get_native()` returns `uint64_t` handle, not pointer
   - **✅ Fixed**: Now uses `reinterpret_cast<IDXGISwapChain*>(native_handle)`

3. **❌ No API Validation**: Didn't check if game uses compatible API
   - **✅ Fixed**: Now validates game is DX11 or DX12 before attempting copy

4. **❌ No Device Validation**: Didn't check if textures are from same device
   - **✅ Fixed**: Added device compatibility check in `CopyFrame()`

## Current Implementation

### Proper Swapchain Access:
```cpp
// WRONG (causes crash):
IDXGISwapChain* swapchain = static_cast<IDXGISwapChain*>(g_last_swapchain_ptr.load());

// CORRECT:
void* ptr = g_last_swapchain_ptr.load();
auto* reshade_swapchain = static_cast<reshade::api::swapchain*>(ptr);
uint64_t native_handle = reshade_swapchain->get_native();
IDXGISwapChain* swapchain = reinterpret_cast<IDXGISwapChain*>(native_handle);
```

### API Validation:
```cpp
int game_api = g_last_swapchain_api.load();
bool is_dx11 = (game_api == 4);  // reshade::api::device_api::d3d11
bool is_dx12 = (game_api == 5);  // reshade::api::device_api::d3d12
bool compatible = is_dx11 || is_dx12;
```

### Device Compatibility Check:
```cpp
// In CopyFrame():
ComPtr<ID3D11Device> source_device;
source_backbuffer->GetDevice(&source_device);

if (source_device.Get() != device_.Get()) {
    LogError("Cannot copy between different D3D11 devices!");
    return false;
}
```

## ⚠️ CRITICAL LIMITATION: Cross-Device Copy

### The Problem:
**You CANNOT directly copy textures between different D3D11 devices!**

Even with the crash fixes, copying will fail if:
- ✅ Game swapchain is valid
- ✅ Test window swapchain is valid
- ❌ **Game has its own D3D11 device**
- ❌ **Test window has its own separate D3D11 device**

### Why It Fails:
```
Game Device A (0x12345678)           Test Device B (0x87654321)
├─ Game Swapchain                    ├─ Test Swapchain
├─ Game Backbuffer Texture           ├─ Test Backbuffer Texture
└─ Game ID3D11DeviceContext          └─ Test ID3D11DeviceContext

❌ context_->CopyResource(test_tex, game_tex)  // FAILS!
   Error: Cannot copy between resources from different devices
```

D3D11 rule: **CopyResource only works within the same device!**

## Error Messages You'll See:

### 1. Different Device Error:
```
DX11ProxyManager::CopyFrame: Cannot copy between different D3D11 devices!
Source device: 0x12345678, Our device: 0x87654321
Cross-device copy requires shared resources (not yet implemented)
```
**Meaning**: Game and test window use separate D3D11 devices

### 2. API Incompatible:
```
DX11ProxyUI: Game is not DX11/DX12 (API: 1) - cannot copy
```
**Meaning**: Game uses DX9, OpenGL, or Vulkan (not supported)

### 3. Button Disabled:
Tooltip shows: "Incompatible API! Game is using: D3D9 (API 1)"
**Meaning**: Game doesn't use a compatible graphics API

## Workarounds

### Option 1: Shared Resources (Not Implemented)
To copy between devices, you need **shared resources**:

```cpp
// Create shared texture on source device
D3D11_TEXTURE2D_DESC desc = {...};
desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

// Get shared handle
IDXGIResource* dxgi_resource;
source_texture->QueryInterface(__uuidof(IDXGIResource), (void**)&dxgi_resource);
HANDLE shared_handle;
dxgi_resource->GetSharedHandle(&shared_handle);

// Open on destination device
dest_device->OpenSharedResource(shared_handle, __uuidof(ID3D11Texture2D), (void**)&shared_texture);

// Now you can copy
dest_context->CopyResource(dest_texture, shared_texture);
```

**Complexity**: HIGH - requires recreation of textures with shared flags

### Option 2: Same Device (Recommended)
Use the **SAME D3D11 device** for both game and test window:

```cpp
// Instead of creating new device:
auto* game_device = reshade_swapchain->get_device();
ID3D11Device* native_device = reinterpret_cast<ID3D11Device*>(game_device->get_native());

// Use game's device to create test window swapchain
// Now CopyResource will work!
```

**Complexity**: MEDIUM - requires refactoring initialization

### Option 3: Manual Copy via CPU (Slow)
```cpp
// Map source texture (read from GPU to CPU)
D3D11_MAPPED_SUBRESOURCE src_map;
context->Map(source_staging, 0, D3D11_MAP_READ, 0, &src_map);

// Map dest texture (write from CPU to GPU)
D3D11_MAPPED_SUBRESOURCE dest_map;
context->Map(dest_staging, 0, D3D11_MAP_WRITE, 0, &dest_map);

// Copy via CPU memory
memcpy(dest_map.pData, src_map.pData, size);

// Unmap both
context->Unmap(source_staging, 0);
context->Unmap(dest_staging, 0);
```

**Complexity**: LOW - but **VERY SLOW** (GPU→CPU→GPU)

## Current Behavior

### ✅ Works:
- Button no longer crashes
- Proper error messages displayed
- API compatibility checked
- Device mismatch detected

### ❌ Doesn't Work:
- Actual copying from game to test window (device mismatch)
- Will silently fail with device error in log

### Test Color Buttons:
- ✅ These WORK perfectly!
- They render directly to test window (same device)
- Use these to verify test window is working

## Testing Workflow

1. **Start a DX11 game**
2. **Create test window**: Click "Quick Test: Enable + Create 4K Window"
   - Window appears showing RED ✅

3. **Test rendering**: Click color buttons (Green, Blue, etc.)
   - Window changes colors ✅

4. **Try game copy**: Click "Display Game Content (Start Copying)"
   - Check ReShade log for error messages
   - If you see "Cannot copy between different D3D11 devices!", this is expected

5. **Keep using colors**: Color buttons will continue to work ✅

## Future Implementation

To properly support cross-device copying, we need to implement:

### Phase 1: Shared Resources
- ✅ Detect device mismatch
- ⏳ Create shared texture on game device
- ⏳ Open shared texture on test device
- ⏳ Use shared texture as intermediate buffer

### Phase 2: ReShade Integration
- ⏳ Use ReShade's resource manager
- ⏳ Hook into game's rendering pipeline
- ⏳ Copy through ReShade's effect system

### Phase 3: API Translation
- ⏳ Support DX9 → DX11 (actual proxy translation)
- ⏳ Use shared surfaces for DX9
- ⏳ Implement format conversion

## API Support Status

| API | Detection | Native Access | Copy Support | Notes |
|-----|-----------|---------------|--------------|-------|
| **DX11** | ✅ | ✅ | ⚠️ | Same device only |
| **DX12** | ✅ | ✅ | ⚠️ | Same device only |
| **DX9** | ✅ | ❌ | ❌ | Requires shared surfaces |
| **DX10** | ✅ | ⚠️ | ❌ | Not tested |
| **OpenGL** | ✅ | ❌ | ❌ | Not supported |
| **Vulkan** | ✅ | ❌ | ❌ | Not supported |

## Summary

**What was fixed**: Crash on button click ✅

**What still doesn't work**: Actual game content copying (device limitation) ⚠️

**What works perfectly**: Test window rendering with colors ✅

**Next steps**: Implement shared resource support for cross-device copying

## Developer Notes

If you're implementing cross-device copy support:

1. Check device compatibility BEFORE starting copy thread
2. If devices differ, create shared resources
3. Use intermediate staging textures if needed
4. Handle format mismatches gracefully
5. Log detailed error messages for debugging

### Recommended Approach:
```cpp
bool DX11ProxyManager::StartCopyThread(IDXGISwapChain* source) {
    // Get source device
    ComPtr<ID3D11Device> source_device;
    GetSourceDevice(source, &source_device);

    // Check if same device
    if (source_device.Get() == device_.Get()) {
        // Direct copy - fast path
        StartDirectCopy(source);
    } else {
        // Different devices - need shared resources
        if (!InitializeSharedResources(source_device)) {
            LogError("Failed to initialize shared resources");
            return false;
        }
        StartSharedCopy(source);
    }
}
```

This approach gracefully handles both same-device (fast) and cross-device (shared resources) scenarios.

