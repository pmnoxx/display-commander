#pragma once

#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi.h>

// Forward declarations for DXGI hooks
namespace display_commanderhooks::dxgi {
bool HookSwapchain(IDXGISwapChain *swapchain);
bool HookFactory(IDXGIFactory *factory);
} // namespace display_commanderhooks::dxgi

namespace display_commanderhooks {

// Function pointer types
using GetFocus_pfn = HWND(WINAPI *)();
using GetForegroundWindow_pfn = HWND(WINAPI *)();
using GetActiveWindow_pfn = HWND(WINAPI *)();
using GetGUIThreadInfo_pfn = BOOL(WINAPI *)(DWORD, PGUITHREADINFO);
using SetThreadExecutionState_pfn = EXECUTION_STATE(WINAPI *)(EXECUTION_STATE);
using SetWindowLongPtrW_pfn = LONG_PTR(WINAPI *)(HWND, int, LONG_PTR);
using SetWindowLongA_pfn = LONG(WINAPI *)(HWND, int, LONG);
using SetWindowLongW_pfn = LONG(WINAPI *)(HWND, int, LONG);
using SetWindowLongPtrA_pfn = LONG_PTR(WINAPI *)(HWND, int, LONG_PTR);
using SetWindowPos_pfn = BOOL(WINAPI *)(HWND, HWND, int, int, int, int, UINT);
using SetCursor_pfn = HCURSOR(WINAPI *)(HCURSOR);
using ShowCursor_pfn = int(WINAPI *)(BOOL);

// DXGI Factory creation function pointer types
using CreateDXGIFactory_pfn = HRESULT(WINAPI *)(REFIID, void **);
using CreateDXGIFactory1_pfn = HRESULT(WINAPI *)(REFIID, void **);

// D3D11 Device creation function pointer types
using D3D11CreateDeviceAndSwapChain_pfn = HRESULT(WINAPI *)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT, const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
using D3D11CreateDevice_pfn = HRESULT(WINAPI *)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

// D3D12 Device creation function pointer types
using D3D12CreateDevice_pfn = HRESULT(WINAPI *)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);

// API hook function pointers
extern GetFocus_pfn GetFocus_Original;
extern GetForegroundWindow_pfn GetForegroundWindow_Original;
extern GetActiveWindow_pfn GetActiveWindow_Original;
extern GetGUIThreadInfo_pfn GetGUIThreadInfo_Original;
extern SetThreadExecutionState_pfn SetThreadExecutionState_Original;
extern SetWindowLongPtrW_pfn SetWindowLongPtrW_Original;
extern SetWindowLongA_pfn SetWindowLongA_Original;
extern SetWindowLongW_pfn SetWindowLongW_Original;
extern SetWindowLongPtrA_pfn SetWindowLongPtrA_Original;
extern SetWindowPos_pfn SetWindowPos_Original;
extern SetCursor_pfn SetCursor_Original;
extern ShowCursor_pfn ShowCursor_Original;
extern CreateDXGIFactory_pfn CreateDXGIFactory_Original;
extern CreateDXGIFactory1_pfn CreateDXGIFactory1_Original;
extern D3D11CreateDeviceAndSwapChain_pfn D3D11CreateDeviceAndSwapChain_Original;
extern D3D11CreateDevice_pfn D3D11CreateDevice_Original;
extern D3D12CreateDevice_pfn D3D12CreateDevice_Original;

// Hooked API functions
HWND WINAPI GetFocus_Detour();
HWND WINAPI GetForegroundWindow_Detour();
HWND WINAPI GetActiveWindow_Detour();
BOOL WINAPI GetGUIThreadInfo_Detour(DWORD idThread, PGUITHREADINFO pgui);
EXECUTION_STATE WINAPI SetThreadExecutionState_Detour(EXECUTION_STATE esFlags);
LONG_PTR WINAPI SetWindowLongPtrW_Detour(HWND hWnd, int nIndex, LONG_PTR dwNewLong);
LONG WINAPI SetWindowLongA_Detour(HWND hWnd, int nIndex, LONG dwNewLong);
LONG WINAPI SetWindowLongW_Detour(HWND hWnd, int nIndex, LONG dwNewLong);
LONG_PTR WINAPI SetWindowLongPtrA_Detour(HWND hWnd, int nIndex, LONG_PTR dwNewLong);
BOOL WINAPI SetWindowPos_Detour(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags);
HCURSOR WINAPI SetCursor_Detour(HCURSOR hCursor);
int WINAPI ShowCursor_Detour(BOOL bShow);
HRESULT WINAPI CreateDXGIFactory_Detour(REFIID riid, void **ppFactory);
HRESULT WINAPI CreateDXGIFactory1_Detour(REFIID riid, void **ppFactory);
HRESULT WINAPI D3D11CreateDeviceAndSwapChain_Detour(IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags, const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels, UINT SDKVersion, const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc, IDXGISwapChain** ppSwapChain, ID3D11Device** ppDevice, D3D_FEATURE_LEVEL* pFeatureLevel, ID3D11DeviceContext** ppImmediateContext);
HRESULT WINAPI D3D11CreateDevice_Detour(IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags, const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels, UINT SDKVersion, ID3D11Device** ppDevice, D3D_FEATURE_LEVEL* pFeatureLevel, ID3D11DeviceContext** ppImmediateContext);
HRESULT WINAPI D3D12CreateDevice_Detour(IUnknown* pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel, REFIID riid, void** ppDevice);

// Hook management
bool InstallApiHooks();
bool InstallWindowsApiHooks();
bool InstallDxgiHooks(HMODULE dxgi_module = nullptr);
bool InstallD3DDeviceHooks();
void UninstallApiHooks();

// Helper functions
HWND GetGameWindow();
bool IsGameWindow(HWND hwnd);
void SetGameWindow(HWND hwnd);

// SetCursor direct access function
HCURSOR WINAPI SetCursor_Direct(HCURSOR hCursor);

// ShowCursor direct access function
int WINAPI ShowCursor_Direct(BOOL bShow);

// GetForegroundWindow direct access function
HWND WINAPI GetForegroundWindow_Direct();

// Restore cursor function
void RestoreSetCursor();
void RestoreShowCursor();

// Input blocking functions (forward declarations)
bool ShouldBlockMouseInput();
bool ShouldBlockKeyboardInput();

} // namespace display_commanderhooks
