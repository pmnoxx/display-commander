# DXGI Hooks

This directory contains hooks for DXGI Present calls to log present flags.

## Files

- `dxgi_hooks.hpp` - Header file with function declarations
- `dxgi_hooks.cpp` - Implementation with Present hook and flag logging

## Current Implementation

This is a **simplified implementation** that provides the foundation for logging DXGI Present flags. The current implementation:

1. **Logs Present Flags**: Decodes and logs all DXGI present flags including:
   - `DXGI_PRESENT_TEST`
   - `DXGI_PRESENT_DO_NOT_SEQUENCE`
   - `DXGI_PRESENT_RESTART`
   - `DXGI_PRESENT_DO_NOT_WAIT`
   - `DXGI_PRESENT_STEREO_PREFER_RIGHT`
   - `DXGI_PRESENT_STEREO_TEMPORARY_MONO`
   - `DXGI_PRESENT_RESTRICT_TO_OUTPUT`
   - `DXGI_PRESENT_USE_DURATION`
   - `DXGI_PRESENT_ALLOW_TEARING`

2. **Supports Both Present Functions**:
   - `IDXGISwapChain::Present(UINT SyncInterval, UINT Flags)`
   - `IDXGISwapChain1::Present1(UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS* pPresentParameters)`

## Limitations

**Important**: This is currently a **foundation implementation**. For actual hooking of DXGI Present calls, vtable hooking would be required since these are COM interface methods. The current implementation provides the logging infrastructure and can be extended with proper vtable hooking.

## Integration

The DXGI hooks are automatically installed when `InstallApiHooks()` is called and uninstalled when `UninstallApiHooks()` is called.

## Usage

When present calls are made, you'll see logs like:
```
DXGI Present called - SyncInterval: 1, Flags: 0x00000000 (NONE)
DXGI Present1 called - SyncInterval: 0, PresentFlags: 0x00000008 (ALLOW_TEARING)
```
