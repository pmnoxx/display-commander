# DXGI VTable Quick Reference

## IDXGISwapChain3 Key Methods

| Method | VTable Index | Description |
|--------|--------------|-------------|
| `GetCurrentBackBufferIndex` | 36 | Get current back buffer index |
| **`CheckColorSpaceSupport`** | **37** | **Check color space support** ⭐ |
| `SetColorSpace1` | 38 | Set color space |
| `ResizeBuffers1` | 39 | Resize buffers with parameters |

## Common VTable Indices

| Method | VTable Index | Interface |
|--------|--------------|-----------|
| `Present` | 8 | IDXGISwapChain |
| `GetDesc` | 12 | IDXGISwapChain |
| `Present1` | 22 | IDXGISwapChain1 |
| `GetDesc1` | 19 | IDXGISwapChain1 |

## Verification Commands

```bash
# Check Windows SDK header file
Get-Content "C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared\dxgi1_4.h" | Select-String -Pattern "CheckColorSpaceSupport" -Context 5
```

## Documentation Files

- **Complete Reference**: `docs/DXGI_VTABLE_INDICES.md`
- **This Quick Reference**: `docs/VTABLE_QUICK_REFERENCE.md`

---

**Last Updated**: December 2024
**Status**: ✅ Ready for Use
