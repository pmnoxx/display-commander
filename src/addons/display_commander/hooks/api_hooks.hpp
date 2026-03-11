#pragma once

#include "dxgi/dxgi_hooks.hpp"
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi.h>
#include <windows.h>
#include <cstdint>


// Forward declarations for DXGI hooks
namespace display_commanderhooks::dxgi {
bool HookSwapchain(IDXGISwapChain* swapchain);
bool HookFactory(IDXGIFactory* factory);
}  // namespace display_commanderhooks::dxgi

namespace display_commanderhooks {

// Function pointer types
using GetFocus_pfn = HWND(WINAPI*)();
using GetForegroundWindow_pfn = HWND(WINAPI*)();
using GetActiveWindow_pfn = HWND(WINAPI*)();
using GetGUIThreadInfo_pfn = BOOL(WINAPI*)(DWORD, PGUITHREADINFO);
using IsIconic_pfn = BOOL(WINAPI*)(HWND);
using IsWindowVisible_pfn = BOOL(WINAPI*)(HWND);
using GetWindowPlacement_pfn = BOOL(WINAPI*)(HWND, WINDOWPLACEMENT*);
using SetThreadExecutionState_pfn = EXECUTION_STATE(WINAPI*)(EXECUTION_STATE);
using SetWindowLongPtrW_pfn = LONG_PTR(WINAPI*)(HWND, int, LONG_PTR);
using SetWindowLongA_pfn = LONG(WINAPI*)(HWND, int, LONG);
using SetWindowLongW_pfn = LONG(WINAPI*)(HWND, int, LONG);
using SetWindowLongPtrA_pfn = LONG_PTR(WINAPI*)(HWND, int, LONG_PTR);
using SetWindowPos_pfn = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
using CreateWindowExW_pfn = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE,
                                          LPVOID);
using SetCursor_pfn = HCURSOR(WINAPI*)(HCURSOR);
using ShowCursor_pfn = int(WINAPI*)(BOOL);
using AddVectoredExceptionHandler_pfn = PVOID(WINAPI*)(ULONG, PVECTORED_EXCEPTION_HANDLER);

// DXGI Factory creation function pointer types
using CreateDXGIFactory_pfn = HRESULT(WINAPI*)(REFIID, void**);
using CreateDXGIFactory1_pfn = HRESULT(WINAPI*)(REFIID, void**);
using CreateDXGIFactory2_pfn = HRESULT(WINAPI*)(UINT Flags, REFIID, void**);

// D3D11 Device creation function pointer types
using D3D11CreateDeviceAndSwapChain_pfn = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
                                                           const D3D_FEATURE_LEVEL*, UINT, UINT,
                                                           const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**,
                                                           ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
using D3D11CreateDevice_pfn = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*,
                                               UINT, UINT, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
using D3D11On12CreateDevice_pfn = HRESULT(WINAPI*)(IUnknown*, UINT, const D3D_FEATURE_LEVEL*, UINT, IUnknown* const*,
                                                  UINT, UINT, ID3D11Device**, ID3D11DeviceContext**,
                                                  D3D_FEATURE_LEVEL*);

// D3D12 Device creation function pointer types
using D3D12CreateDevice_pfn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);

// API hook function pointers
extern GetFocus_pfn GetFocus_Original;
extern GetForegroundWindow_pfn GetForegroundWindow_Original;
extern GetActiveWindow_pfn GetActiveWindow_Original;
extern GetGUIThreadInfo_pfn GetGUIThreadInfo_Original;
extern IsIconic_pfn IsIconic_Original;
extern IsWindowVisible_pfn IsWindowVisible_Original;
extern GetWindowPlacement_pfn GetWindowPlacement_Original;
extern SetThreadExecutionState_pfn SetThreadExecutionState_Original;
extern SetWindowLongPtrW_pfn SetWindowLongPtrW_Original;
extern SetWindowLongA_pfn SetWindowLongA_Original;
extern SetWindowLongW_pfn SetWindowLongW_Original;
extern SetWindowLongPtrA_pfn SetWindowLongPtrA_Original;
extern SetWindowPos_pfn SetWindowPos_Original;
extern CreateWindowExW_pfn CreateWindowExW_Original;
extern SetCursor_pfn SetCursor_Original;
extern ShowCursor_pfn ShowCursor_Original;
extern AddVectoredExceptionHandler_pfn AddVectoredExceptionHandler_Original;
extern CreateDXGIFactory_pfn CreateDXGIFactory_Original;
extern CreateDXGIFactory1_pfn CreateDXGIFactory1_Original;
extern CreateDXGIFactory2_pfn CreateDXGIFactory2_Original;
extern D3D11CreateDeviceAndSwapChain_pfn D3D11CreateDeviceAndSwapChain_Original;
extern D3D11CreateDevice_pfn D3D11CreateDevice_Original;
extern D3D11On12CreateDevice_pfn D3D11On12CreateDevice_Original;
extern D3D12CreateDevice_pfn D3D12CreateDevice_Original;

// True minimized state, bypassing IsIconic detour (e.g. for ApplyWindowChange - do not move/resize minimized windows).
bool IsIconic_direct(HWND hwnd);
// True visibility state, bypassing IsWindowVisible detour (e.g. when code needs real visibility, not Continue Rendering
// spoof).
bool IsWindowVisible_direct(HWND hwnd);

// Create DXGI factory via original API (bypasses CreateDXGIFactory1 detour). Use for addon-internal factory (e.g.
// shared factory, VRAM, display enumeration). Returns S_OK on success.
HRESULT CreateDXGIFactory1_Direct(REFIID riid, void** ppFactory);

// Hooked API functions
HWND WINAPI GetFocus_Detour();
HWND WINAPI GetForegroundWindow_Detour();
HWND WINAPI GetActiveWindow_Detour();
BOOL WINAPI GetGUIThreadInfo_Detour(DWORD idThread, PGUITHREADINFO pgui);
BOOL WINAPI IsIconic_Detour(HWND hWnd);
BOOL WINAPI IsWindowVisible_Detour(HWND hWnd);
BOOL WINAPI GetWindowPlacement_Detour(HWND hWnd, WINDOWPLACEMENT* lpwndpl);
EXECUTION_STATE WINAPI SetThreadExecutionState_Detour(EXECUTION_STATE esFlags);
LONG_PTR WINAPI SetWindowLongPtrW_Detour(HWND hWnd, int nIndex, LONG_PTR dwNewLong);
LONG WINAPI SetWindowLongA_Detour(HWND hWnd, int nIndex, LONG dwNewLong);
LONG WINAPI SetWindowLongW_Detour(HWND hWnd, int nIndex, LONG dwNewLong);
LONG_PTR WINAPI SetWindowLongPtrA_Detour(HWND hWnd, int nIndex, LONG_PTR dwNewLong);
BOOL WINAPI SetWindowPos_Detour(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags);
HWND WINAPI CreateWindowExW_Detour(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int X,
                                   int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance,
                                   LPVOID lpParam);
HCURSOR WINAPI SetCursor_Detour(HCURSOR hCursor);
int WINAPI ShowCursor_Detour(BOOL bShow);
PVOID WINAPI AddVectoredExceptionHandler_Detour(ULONG First, PVECTORED_EXCEPTION_HANDLER Handler);
// Bypass detour: register vectored exception handler with real API (e.g. for process_exit_hooks).
PVOID AddVectoredExceptionHandler_Direct(ULONG First, PVECTORED_EXCEPTION_HANDLER Handler);
HRESULT WINAPI CreateDXGIFactory_Detour(REFIID riid, void** ppFactory);
HRESULT WINAPI CreateDXGIFactory1_Detour(REFIID riid, void** ppFactory);
HRESULT WINAPI CreateDXGIFactory2_Detour(UINT Flags, REFIID riid, void** ppFactory);
HRESULT WINAPI D3D11CreateDeviceAndSwapChain_Detour(IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType,
                                                    HMODULE Software, UINT Flags,
                                                    const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels,
                                                    UINT SDKVersion, const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
                                                    IDXGISwapChain** ppSwapChain, ID3D11Device** ppDevice,
                                                    D3D_FEATURE_LEVEL* pFeatureLevel,
                                                    ID3D11DeviceContext** ppImmediateContext);
HRESULT WINAPI D3D11CreateDevice_Detour(IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software,
                                        UINT Flags, const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels,
                                        UINT SDKVersion, ID3D11Device** ppDevice, D3D_FEATURE_LEVEL* pFeatureLevel,
                                        ID3D11DeviceContext** ppImmediateContext);
HRESULT WINAPI D3D11On12CreateDevice_Detour(IUnknown* pDevice, UINT Flags,
                                            const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels,
                                            IUnknown* const* ppCommandQueues, UINT NumQueues, UINT NodeMask,
                                            ID3D11Device** ppDevice, ID3D11DeviceContext** ppImmediateContext,
                                            D3D_FEATURE_LEVEL* pChosenFeatureLevel);
HRESULT WINAPI D3D12CreateDevice_Detour(IUnknown* pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel, REFIID riid,
                                        void** ppDevice);

// Hook management
bool InstallApiHooks();
bool InstallWindowsApiHooks();
void UninstallApiHooks();

// Helper functions
HWND GetGameWindow();
void SetGameWindow(HWND hwnd);

// SetCursor direct access function
HCURSOR WINAPI SetCursor_Direct(HCURSOR hCursor);

// ShowCursor direct access function
int WINAPI ShowCursor_Direct(BOOL bShow);

// GetForegroundWindow direct access function
HWND WINAPI GetForegroundWindow_Direct();

// SetWindowPos direct access (bypasses hook)
BOOL WINAPI SetWindowPos_Direct(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags);

// CreateWindowW direct access (bypasses hook; use from standalone UI or when real window creation is needed)
HWND WINAPI CreateWindowW_Direct(LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth,
                                 int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam);

// Input blocking functions (forward declarations)
bool ShouldBlockMouseInput(bool assume_foreground = false);
bool ShouldBlockKeyboardInput(bool assume_foreground = false);

// Continue Rendering API debug: snapshot of what each focus/visibility API last returned (for Window Info tab).
struct ContinueRenderingApiDebugSnapshot {
    const char* api_name;
    uintptr_t last_value;        // HWND as uintptr_t, or 0/1 for BOOL (when value_is_bool)
    uint64_t last_call_time_ns;  // from utils::get_now_ns()
    bool did_override;           // true if we spoofed the return value
    bool value_is_bool;          // true for IsIconic/IsWindowVisible (last_value 0 or 1)
};
constexpr int CR_DEBUG_API_COUNT = 6;
void GetContinueRenderingApiDebugSnapshots(ContinueRenderingApiDebugSnapshot* out);

}  // namespace display_commanderhooks
