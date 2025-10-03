#pragma once

#include <windows.h>

#include <dxgi.h>

// Forward declarations for DXGI hooks
namespace display_commanderhooks::dxgi {
bool InstallDxgiPresentHooks();
void UninstallDxgiPresentHooks();
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

// API hook function pointers
extern GetFocus_pfn GetFocus_Original;
extern GetForegroundWindow_pfn GetForegroundWindow_Original;
extern GetActiveWindow_pfn GetActiveWindow_Original;
extern GetGUIThreadInfo_pfn GetGUIThreadInfo_Original;
extern SetThreadExecutionState_pfn SetThreadExecutionState_Original;
extern CreateDXGIFactory_pfn CreateDXGIFactory_Original;
extern CreateDXGIFactory1_pfn CreateDXGIFactory1_Original;

// Hooked API functions
HWND WINAPI GetFocus_Detour();
HWND WINAPI GetForegroundWindow_Detour();
HWND WINAPI GetActiveWindow_Detour();
BOOL WINAPI GetGUIThreadInfo_Detour(DWORD idThread, PGUITHREADINFO pgui);
EXECUTION_STATE WINAPI SetThreadExecutionState_Detour(EXECUTION_STATE esFlags);
HRESULT WINAPI CreateDXGIFactory_Detour(REFIID riid, void **ppFactory);
HRESULT WINAPI CreateDXGIFactory1_Detour(REFIID riid, void **ppFactory);

// Hook management
bool InstallApiHooks();
void UninstallApiHooks();
bool AreApiHooksInstalled();

// Helper functions
HWND GetGameWindow();
bool IsGameWindow(HWND hwnd);
void SetGameWindow(HWND hwnd);

} // namespace display_commanderhooks
