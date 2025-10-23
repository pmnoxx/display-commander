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

// DXGI Factory creation function pointer types
using CreateDXGIFactory_pfn = HRESULT(WINAPI *)(REFIID, void **);
using CreateDXGIFactory1_pfn = HRESULT(WINAPI *)(REFIID, void **);

// D3D11 Device creation function pointer types
using D3D11CreateDeviceAndSwapChain_pfn = HRESULT(WINAPI *)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT, const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

// D3D12 Device creation function pointer types
using D3D12CreateDevice_pfn = HRESULT(WINAPI *)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);

// API hook function pointers
extern GetFocus_pfn GetFocus_Original;
extern GetForegroundWindow_pfn GetForegroundWindow_Original;
extern GetActiveWindow_pfn GetActiveWindow_Original;
extern GetGUIThreadInfo_pfn GetGUIThreadInfo_Original;
extern SetThreadExecutionState_pfn SetThreadExecutionState_Original;
extern CreateDXGIFactory_pfn CreateDXGIFactory_Original;
extern CreateDXGIFactory1_pfn CreateDXGIFactory1_Original;
extern D3D11CreateDeviceAndSwapChain_pfn D3D11CreateDeviceAndSwapChain_Original;
extern D3D12CreateDevice_pfn D3D12CreateDevice_Original;

// Hooked API functions
HWND WINAPI GetFocus_Detour();
HWND WINAPI GetForegroundWindow_Detour();
HWND WINAPI GetActiveWindow_Detour();
BOOL WINAPI GetGUIThreadInfo_Detour(DWORD idThread, PGUITHREADINFO pgui);
EXECUTION_STATE WINAPI SetThreadExecutionState_Detour(EXECUTION_STATE esFlags);
HRESULT WINAPI CreateDXGIFactory_Detour(REFIID riid, void **ppFactory);
HRESULT WINAPI CreateDXGIFactory1_Detour(REFIID riid, void **ppFactory);
HRESULT WINAPI D3D11CreateDeviceAndSwapChain_Detour(IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags, const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels, UINT SDKVersion, const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc, IDXGISwapChain** ppSwapChain, ID3D11Device** ppDevice, D3D_FEATURE_LEVEL* pFeatureLevel, ID3D11DeviceContext** ppImmediateContext);
HRESULT WINAPI D3D12CreateDevice_Detour(IUnknown* pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel, REFIID riid, void** ppDevice);

// Hook management
bool InstallApiHooks();
bool InstallDxgiHooks();
bool InstallD3DDeviceHooks();
void UninstallApiHooks();

// Helper functions
HWND GetGameWindow();
bool IsGameWindow(HWND hwnd);
void SetGameWindow(HWND hwnd);

} // namespace display_commanderhooks
