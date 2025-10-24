#include "api_hooks.hpp"
#include "hook_suppression_manager.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"
#include "dxgi/dxgi_present_hooks.hpp"
#include "globals.hpp"
#include "loadlibrary_hooks.hpp"
#include "opengl_hooks.hpp"
#include "process_exit_hooks.hpp"
#include "sleep_hooks.hpp"
#include "timeslowdown_hooks.hpp"
#include "windows_gaming_input_hooks.hpp"
#include "windows_hooks/windows_message_hooks.hpp"
#include "dinput_hooks.hpp"
#include "display_settings_hooks.hpp"
#include "debug_output_hooks.hpp"
#include "window_proc_hooks.hpp"
#include "../settings/developer_tab_settings.hpp"
#include "../display_cache.hpp"
#include <MinHook.h>

// External reference to screensaver mode setting
extern std::atomic<ScreensaverMode> s_screensaver_mode;

namespace display_commanderhooks {

// Original function pointers
GetFocus_pfn GetFocus_Original = nullptr;
GetForegroundWindow_pfn GetForegroundWindow_Original = nullptr;
GetActiveWindow_pfn GetActiveWindow_Original = nullptr;
GetGUIThreadInfo_pfn GetGUIThreadInfo_Original = nullptr;
SetThreadExecutionState_pfn SetThreadExecutionState_Original = nullptr;
SetWindowLongPtrW_pfn SetWindowLongPtrW_Original = nullptr;
SetWindowPos_pfn SetWindowPos_Original = nullptr;
SetCursor_pfn SetCursor_Original = nullptr;
ShowCursor_pfn ShowCursor_Original = nullptr;
CreateDXGIFactory_pfn CreateDXGIFactory_Original = nullptr;
CreateDXGIFactory1_pfn CreateDXGIFactory1_Original = nullptr;
D3D11CreateDeviceAndSwapChain_pfn D3D11CreateDeviceAndSwapChain_Original = nullptr;
D3D12CreateDevice_pfn D3D12CreateDevice_Original = nullptr;

// Hook state
static std::atomic<bool> g_api_hooks_installed{false};

// Get the game window handle (we'll need to track this)
static HWND g_game_window = nullptr;

HWND GetGameWindow() { return g_game_window; }


// Hooked GetFocus function
HWND WINAPI GetFocus_Detour() {
    if (s_continue_rendering.load() && g_game_window != nullptr && IsWindow(g_game_window)) {
        // Return the game window even when it doesn't have focus
        //   LogInfo("GetFocus_Detour: Returning game window due to continue rendering - HWND: 0x%p", g_game_window);
        return g_game_window;
    }

    // Call original function
    return GetFocus_Original ? GetFocus_Original() : GetFocus();
}

// Hooked GetForegroundWindow function
HWND WINAPI GetForegroundWindow_Detour() {
    if (s_continue_rendering.load() && g_game_window != nullptr && IsWindow(g_game_window)) {
        // Return the game window even when it's not in foreground
        //    LogInfo("GetForegroundWindow_Detour: Returning game window due to continue rendering - HWND: 0x%p",
        //    g_game_window);
        return g_game_window;
    }

    // Call original function
    return GetForegroundWindow_Original ? GetForegroundWindow_Original() : GetForegroundWindow();
}

// Hooked GetActiveWindow function
HWND WINAPI GetActiveWindow_Detour() {
    if (s_continue_rendering.load() && g_game_window != nullptr && IsWindow(g_game_window)) {
        // Return the game window even when it's not the active window
        // Check if we're in the same thread as the game window
        DWORD dwPid = 0;
        DWORD dwTid = GetWindowThreadProcessId(g_game_window, &dwPid);

        if (GetCurrentThreadId() == dwTid) {
            // We're in the same thread as the game window, return it
            return g_game_window;
        }

        // For other threads, check if the current process owns the game window
        if (GetCurrentProcessId() == dwPid) {
            return g_game_window;
        }
        return g_game_window;
    }

    // Call original function
    return GetActiveWindow_Original ? GetActiveWindow_Original() : GetActiveWindow();
}

// Hooked GetGUIThreadInfo function
BOOL WINAPI GetGUIThreadInfo_Detour(DWORD idThread, PGUITHREADINFO pgui) {
    if (s_continue_rendering.load() && g_game_window != nullptr && IsWindow(g_game_window)) {
        // Call original function first
        BOOL result =
            GetGUIThreadInfo_Original ? GetGUIThreadInfo_Original(idThread, pgui) : GetGUIThreadInfo(idThread, pgui);

        if (result && pgui) {
            // Modify the thread info to show game window as active
            DWORD dwPid = 0;
            DWORD dwTid = GetWindowThreadProcessId(g_game_window, &dwPid);

            if (idThread == dwTid || idThread == 0) {
                // Set the game window as active and focused
                pgui->hwndActive = g_game_window;
                pgui->hwndFocus = g_game_window;
                pgui->hwndCapture = nullptr;     // Clear capture to prevent issues
                pgui->hwndCaret = g_game_window; // Set caret to game window

                // Set appropriate flags (using standard Windows constants)
                pgui->flags = 0x00000001 | 0x00000002; // GTI_CARETBLINKING | GTI_CARETSHOWN

                LogInfo("GetGUIThreadInfo_Detour: Modified thread info to show game window as active - HWND: 0x%p, "
                        "Thread: %lu",
                        g_game_window, idThread);
            }
        }

        return result;
    }

    // Call original function
    return GetGUIThreadInfo_Original ? GetGUIThreadInfo_Original(idThread, pgui) : GetGUIThreadInfo(idThread, pgui);
}

// Hooked SetThreadExecutionState function
EXECUTION_STATE WINAPI SetThreadExecutionState_Detour(EXECUTION_STATE esFlags) {
    // Track total calls
    g_hook_stats[HOOK_SetThreadExecutionState].increment_total();

    // Check screensaver mode setting
    ScreensaverMode screensaver_mode = s_screensaver_mode.load();

    // If screensaver mode is DisableWhenFocused or Disable, ignore all calls
    if (screensaver_mode == ScreensaverMode::kDisableWhenFocused || screensaver_mode == ScreensaverMode::kDisable) {
        return 0x0; // Block game's attempt to control execution state
    }

    // Track unsuppressed calls (when we call the original function)
    g_hook_stats[HOOK_SetThreadExecutionState].increment_unsuppressed();

    // Call original function for kDefault mode
    return SetThreadExecutionState_Original ? SetThreadExecutionState_Original(esFlags)
                                            : SetThreadExecutionState(esFlags);
}

// Hooked SetWindowLongPtrW function
LONG_PTR WINAPI SetWindowLongPtrW_Detour(HWND hWnd, int nIndex, LONG_PTR dwNewLong) {
    // Only process if prevent_always_on_top is enabled
    if (settings::g_developerTabSettings.prevent_always_on_top.GetValue()) {
        if (nIndex == GWL_STYLE) {
            // WS_POPUP added to fix godstrike
            dwNewLong &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_POPUP);
        }
        if (nIndex == GWL_EXSTYLE) {
            dwNewLong &= ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
            // Remove WS_EX_TOPMOST and WS_EX_TOOLWINDOW styles
            LONG_PTR modifiedLong = dwNewLong;
            if ((dwNewLong & (WS_EX_TOPMOST | WS_EX_TOOLWINDOW)) != 0) {
                modifiedLong = dwNewLong & ~(WS_EX_TOPMOST | WS_EX_TOOLWINDOW);

                // Log the modification
                LogInfo("SetWindowLongPtrW: Stripped always-on-top styles from window 0x%p - Original: 0x%llx, Modified: 0x%llx",
                        hWnd, (unsigned long long)dwNewLong, (unsigned long long)modifiedLong);
            }

            // Call original function with modified value
            return SetWindowLongPtrW_Original ? SetWindowLongPtrW_Original(hWnd, nIndex, modifiedLong)
                                             : SetWindowLongPtrW(hWnd, nIndex, modifiedLong);
        }
    }

    // Call original function with unmodified value
    return SetWindowLongPtrW_Original ? SetWindowLongPtrW_Original(hWnd, nIndex, dwNewLong)
                                     : SetWindowLongPtrW(hWnd, nIndex, dwNewLong);
}

// Hooked SetWindowPos function
BOOL WINAPI SetWindowPos_Detour(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags) {
    // Only process if prevent_always_on_top is enabled
    if (settings::g_developerTabSettings.prevent_always_on_top.GetValue()) {
        // Check if we're trying to set the window to be always on top
        if (hWndInsertAfter == HWND_TOPMOST) {
            // Replace HWND_TOPMOST with HWND_NOTOPMOST to prevent always-on-top behavior
            LogInfo("SetWindowPos: Preventing always-on-top for window 0x%p - Replacing HWND_TOPMOST with HWND_NOTOPMOST", hWnd);

            // Call original function with HWND_NOTOPMOST instead of HWND_TOPMOST
            return SetWindowPos_Original ? SetWindowPos_Original(hWnd, HWND_NOTOPMOST, X, Y, cx, cy, uFlags)
                                         : SetWindowPos(hWnd, HWND_NOTOPMOST, X, Y, cx, cy, uFlags);
        }
    }
/*
    // Apply spoofing logic if and only if hwnd matches g_last_swapchain_hwnd
    HWND swapchain_hwnd = g_last_swapchain_hwnd.load();
    if (hWnd == swapchain_hwnd && swapchain_hwnd != nullptr) {
        // Get the current window state to apply spoofing
        auto window_state = g_window_state.load();
        if (window_state) {
            auto s = *window_state;

            // Apply the same scaling calculations as in ApplyWindowChange
            float scaling_percentage_width = 1.0f;
            float scaling_percentage_height = 1.0f;

            // Get DPI scaling from the display cache
            const auto* disp = display_cache::g_displayCache.GetDisplay(s.current_monitor_index);
            if (disp != nullptr) {
                float dpi = disp->GetDpiScaling();
                LogInfo("SetWindowPos_Detour: Applying spoofing for swapchain window 0x%p, target_x: %d, target_y: %d, target_w: %d, target_h: %d, dpi: %f",
                       hWnd, s.target_x, s.target_y, s.target_w, s.target_h, dpi);
            }

            // Get DPI using GetDpiForWindow with g_last_swapchain_hwnd (same as ApplyWindowChange)
            if (swapchain_hwnd != nullptr) {
                UINT dpi = GetDpiForWindow(swapchain_hwnd);
                LogInfo("SetWindowPos_Detour: Window DPI from GetDpiForWindow: %u", dpi);
            }

            // Query Windows display scaling settings (same as ApplyWindowChange)
            HDC hdc = GetDC(swapchain_hwnd);
            if (hdc != nullptr) {
                int system_dpi_x = GetDeviceCaps(hdc, LOGPIXELSX);
                int system_dpi_y = GetDeviceCaps(hdc, LOGPIXELSY);

                // Get virtual resolution (logical resolution before DPI scaling)
                int virtual_width = GetDeviceCaps(hdc, HORZRES);
                int virtual_height = GetDeviceCaps(hdc, VERTRES);

                // Get physical resolution (actual pixel resolution)
                int physical_width = GetDeviceCaps(hdc, DESKTOPHORZRES);
                int physical_height = GetDeviceCaps(hdc, DESKTOPVERTRES);

                ReleaseDC(nullptr, hdc);

                // Prevent division by zero
                if (physical_width > 0 && physical_height > 0) {
                    scaling_percentage_width = static_cast<float>(virtual_width) / static_cast<float>(physical_width);
                    scaling_percentage_height = static_cast<float>(virtual_height) / static_cast<float>(physical_height);
                } else {
                    LogWarn("SetWindowPos_Detour: Invalid physical resolution %dx%d, using default scaling", physical_width, physical_height);
                    scaling_percentage_width = 1.0f;
                    scaling_percentage_height = 1.0f;
                }
                LogInfo("SetWindowPos_Detour: Windows Display Scaling - Width: %.0f%%, Height: %.0f%%",
                       scaling_percentage_width, scaling_percentage_height);
            }

            // Calculate final dimensions with scaling (same as ApplyWindowChange)
            int final_width = static_cast<int>(round(s.target_w * scaling_percentage_width));
            int final_height = static_cast<int>(round(s.target_h * scaling_percentage_height));

            // Validate parameters before SetWindowPos call
            if (final_width <= 0 || final_height <= 0) {
                LogWarn("SetWindowPos_Detour: Invalid calculated dimensions %dx%d, using original parameters", final_width, final_height);
            } else if (s.target_x < -32768 || s.target_x > 32767 || s.target_y < -32768 || s.target_y > 32767) {
                LogWarn("SetWindowPos_Detour: Invalid coordinates (%d, %d), using original parameters", s.target_x, s.target_y);
            } else {
                // Use spoofed values
                LogDebug("SetWindowPos_Detour: Using spoofed values - x=%d, y=%d, w=%d, h=%d, flags=0x%x",
                        s.target_x, s.target_y, final_width, final_height, uFlags);

                auto result = SetWindowPos_Original ? SetWindowPos_Original(hWnd, hWndInsertAfter, s.target_x, s.target_y, final_width, final_height, uFlags)
                                             : SetWindowPos(hWnd, hWndInsertAfter, s.target_x, s.target_y, final_width, final_height, uFlags);

                SendFakeActivationMessages(hWnd);
                return result;
            }
        }
    }*/

    // Call original function with unmodified parameters
    return SetWindowPos_Original ? SetWindowPos_Original(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags)
                                 : SetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

HCURSOR WINAPI SetCursor_Direct(HCURSOR hCursor) {

    // Call original function
    return SetCursor_Original ? SetCursor_Original(hCursor) : SetCursor(hCursor);
}

void RestoreSetCursor() {
    // Get the last stored cursor value atomically
    HCURSOR last_cursor = s_last_cursor_value.load();

    if (last_cursor != nullptr) {
        // Restore the cursor using the direct function
        SetCursor_Direct(last_cursor);
        LogInfo("RestoreSetCursor: Restored cursor to 0x%p", last_cursor);
    } else {
        // If no cursor was stored, set to default arrow
        SetCursor_Direct(LoadCursor(nullptr, IDC_ARROW));
        LogInfo("RestoreSetCursor: No previous cursor found, set to default arrow");
    }
}

void RestoreShowCursor() {
    // Get the last stored show cursor count atomically
    BOOL bShow = s_last_show_cursor_arg.load();
    ShowCursor_Direct(bShow);
}


// Hooked SetCursor function
HCURSOR WINAPI SetCursor_Detour(HCURSOR hCursor) {
    // Store the cursor value atomically
    s_last_cursor_value.store(hCursor);
    if (ShouldBlockMouseInput()) {
        hCursor = LoadCursor(nullptr, IDC_ARROW);
    }
    hCursor = LoadCursor(nullptr, IDC_ARROW);

    // Call original function
    return SetCursor_Direct(hCursor);
}

int WINAPI ShowCursor_Direct(BOOL bShow) {
    // Call original function
    return ShowCursor_Original ? ShowCursor_Original(bShow) : ShowCursor(bShow);
}

// Hooked ShowCursor function
int WINAPI ShowCursor_Detour(BOOL bShow) {
    s_last_show_cursor_arg.store(bShow);


    if (ShouldBlockMouseInput()) {
        bShow = FALSE;
    }

    // Call original function
    int result = ShowCursor_Direct(bShow);
    // Store the cursor count atomically

    LogDebug("ShowCursor_Detour: bShow=%d, result=%d", bShow, result);

    return result;
}

// Hooked CreateDXGIFactory function
HRESULT WINAPI CreateDXGIFactory_Detour(REFIID riid, void **ppFactory) {
    // Increment counter
    g_dxgi_factory_event_counters[DXGI_FACTORY_EVENT_CREATEFACTORY].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Call original function
    HRESULT hr = CreateDXGIFactory_Original ? CreateDXGIFactory_Original(riid, ppFactory)
                                            : CreateDXGIFactory(riid, ppFactory);

    // If successful and we got a factory, hook it
    if (SUCCEEDED(hr) && ppFactory != nullptr && *ppFactory != nullptr) {
        IDXGIFactory *factory = static_cast<IDXGIFactory *>(*ppFactory);
        LogInfo("CreateDXGIFactory succeeded, hooking factory: 0x%p", factory);
        display_commanderhooks::dxgi::HookFactory(factory);// crashes Returnal
    }

    return hr;
}

// Hooked CreateDXGIFactory1 function
HRESULT WINAPI CreateDXGIFactory1_Detour(REFIID riid, void **ppFactory) {
    // Increment counter
    g_dxgi_factory_event_counters[DXGI_FACTORY_EVENT_CREATEFACTORY1].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Call original function
    HRESULT hr = CreateDXGIFactory1_Original ? CreateDXGIFactory1_Original(riid, ppFactory)
                                             : CreateDXGIFactory1(riid, ppFactory);

    // If successful and we got a factory, hook it
    if (SUCCEEDED(hr) && ppFactory != nullptr && *ppFactory != nullptr) {
        IDXGIFactory *factory = static_cast<IDXGIFactory *>(*ppFactory);
        LogInfo("CreateDXGIFactory1 succeeded, hooking factory: 0x%p", factory);
        display_commanderhooks::dxgi::HookFactory(factory); // crashes Returnal
    }

    return hr;
}

// Hooked D3D11CreateDeviceAndSwapChain function
HRESULT WINAPI D3D11CreateDeviceAndSwapChain_Detour(
    IDXGIAdapter* pAdapter,
    D3D_DRIVER_TYPE DriverType,
    HMODULE Software,
    UINT Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels,
    UINT FeatureLevels,
    UINT SDKVersion,
    const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
    IDXGISwapChain** ppSwapChain,
    ID3D11Device** ppDevice,
    D3D_FEATURE_LEVEL* pFeatureLevel,
    ID3D11DeviceContext** ppImmediateContext)
{
    LogInfo("=== D3D11CreateDeviceAndSwapChain Called ===");
    LogInfo("  pAdapter: 0x%p", pAdapter);
    LogInfo("  DriverType: %d", DriverType);
    LogInfo("  Software: 0x%p", Software);
    LogInfo("  Flags: 0x%08X", Flags);
    LogInfo("  pFeatureLevels: 0x%p", pFeatureLevels);
    LogInfo("  FeatureLevels: %u", FeatureLevels);
    LogInfo("  SDKVersion: %u", SDKVersion);
    LogInfo("  pSwapChainDesc: 0x%p", pSwapChainDesc);
    LogInfo("  ppSwapChain: 0x%p", ppSwapChain);
    LogInfo("  ppDevice: 0x%p", ppDevice);
    LogInfo("  pFeatureLevel: 0x%p", pFeatureLevel);
    LogInfo("  ppImmediateContext: 0x%p", ppImmediateContext);

    // Log feature levels if provided
    if (pFeatureLevels && FeatureLevels > 0) {
        LogInfo("  Feature Levels:");
        for (UINT i = 0; i < FeatureLevels; i++) {
            LogInfo("    [%u]: 0x%04X", i, pFeatureLevels[i]);
        }
    }

    // Log swap chain description if provided
    if (pSwapChainDesc) {
        LogInfo("  Swap Chain Description:");
        LogInfo("    BufferDesc.Width: %u", pSwapChainDesc->BufferDesc.Width);
        LogInfo("    BufferDesc.Height: %u", pSwapChainDesc->BufferDesc.Height);
        LogInfo("    BufferDesc.RefreshRate: %u/%u", pSwapChainDesc->BufferDesc.RefreshRate.Numerator, pSwapChainDesc->BufferDesc.RefreshRate.Denominator);
        LogInfo("    BufferDesc.Format: %d", pSwapChainDesc->BufferDesc.Format);
        LogInfo("    BufferDesc.ScanlineOrdering: %d", pSwapChainDesc->BufferDesc.ScanlineOrdering);
        LogInfo("    BufferDesc.Scaling: %d", pSwapChainDesc->BufferDesc.Scaling);
        LogInfo("    SampleDesc.Count: %u", pSwapChainDesc->SampleDesc.Count);
        LogInfo("    SampleDesc.Quality: %u", pSwapChainDesc->SampleDesc.Quality);
        LogInfo("    BufferUsage: 0x%08X", pSwapChainDesc->BufferUsage);
        LogInfo("    BufferCount: %u", pSwapChainDesc->BufferCount);
        LogInfo("    OutputWindow: 0x%p", pSwapChainDesc->OutputWindow);
        LogInfo("    Windowed: %s", pSwapChainDesc->Windowed ? "TRUE" : "FALSE");
        LogInfo("    SwapEffect: %d", pSwapChainDesc->SwapEffect);
        LogInfo("    Flags: 0x%08X", pSwapChainDesc->Flags);
    }

    // Call original function
    HRESULT hr = D3D11CreateDeviceAndSwapChain_Original ?
        D3D11CreateDeviceAndSwapChain_Original(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels, SDKVersion, pSwapChainDesc, ppSwapChain, ppDevice, pFeatureLevel, ppImmediateContext) :
        E_FAIL; // D3D11CreateDeviceAndSwapChain not available

    LogInfo("  Result: 0x%08X (%s)", hr, SUCCEEDED(hr) ? "SUCCESS" : "FAILED");

    // Log output parameters if successful
    if (SUCCEEDED(hr)) {
        if (ppDevice && *ppDevice) {
            LogInfo("  Created Device: 0x%p", *ppDevice);
        }
        if (ppImmediateContext && *ppImmediateContext) {
            LogInfo("  Created Context: 0x%p", *ppImmediateContext);
        }
        if (ppSwapChain && *ppSwapChain) {
            LogInfo("  Created SwapChain: 0x%p", *ppSwapChain);
        }
        if (pFeatureLevel && *pFeatureLevel) {
            LogInfo("  Feature Level: 0x%04X", *pFeatureLevel);
        }
    }

    LogInfo("=== D3D11CreateDeviceAndSwapChain Complete ===");
    return hr;
}

// Hooked D3D12CreateDevice function
HRESULT WINAPI D3D12CreateDevice_Detour(
    IUnknown* pAdapter,
    D3D_FEATURE_LEVEL MinimumFeatureLevel,
    REFIID riid,
    void** ppDevice)
{
    LogInfo("=== D3D12CreateDevice Called ===");
    LogInfo("  pAdapter: 0x%p", pAdapter);
    LogInfo("  MinimumFeatureLevel: 0x%04X", MinimumFeatureLevel);
    LogInfo("  riid: {%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
            riid.Data1, riid.Data2, riid.Data3,
            riid.Data4[0], riid.Data4[1], riid.Data4[2], riid.Data4[3],
            riid.Data4[4], riid.Data4[5], riid.Data4[6], riid.Data4[7]);
    LogInfo("  ppDevice: 0x%p", ppDevice);

    // Call original function
    HRESULT hr = D3D12CreateDevice_Original ?
        D3D12CreateDevice_Original(pAdapter, MinimumFeatureLevel, riid, ppDevice) :
        E_FAIL; // D3D12CreateDevice not available

    LogInfo("  Result: 0x%08X (%s)", hr, SUCCEEDED(hr) ? "SUCCESS" : "FAILED");

    // Log output parameters if successful
    if (SUCCEEDED(hr) && ppDevice && *ppDevice) {
        LogInfo("  Created Device: 0x%p", *ppDevice);
    }

    LogInfo("=== D3D12CreateDevice Complete ===");
    return hr;
}

bool InstallDxgiHooks() {
    static bool dxgi_hooks_installed = false;
    if (dxgi_hooks_installed) {
        LogInfo("DXGI hooks already installed");
        return true;
    }

    // Check if DXGI hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(display_commanderhooks::HookType::DXGI)) {
        LogInfo("DXGI hooks installation suppressed by user setting");
        return false;
    }

    dxgi_hooks_installed = true;


    // Get dxgi.dll module handle
    HMODULE dxgi_module = GetModuleHandleW(L"dxgi.dll");
    if (dxgi_module == nullptr) {
        LogError("Failed to get dxgi.dll module handle");
        return false;
    }

    // Hook CreateDXGIFactory - try both system and ReShade versions
    auto CreateDXGIFactory_sys = reinterpret_cast<decltype(&CreateDXGIFactory)>(GetProcAddress(dxgi_module, "CreateDXGIFactory"));
    if (CreateDXGIFactory_sys != nullptr) {
        if (!CreateAndEnableHook(CreateDXGIFactory_sys, CreateDXGIFactory_Detour, reinterpret_cast<LPVOID *>(&CreateDXGIFactory_Original), "CreateDXGIFactory")) {
            LogError("Failed to create and enable CreateDXGIFactory system hook");
            return false;
        }
        LogInfo("CreateDXGIFactory system hook created successfully");
    } else {
        LogWarn("Failed to get CreateDXGIFactory system address, trying ReShade version");
        if (!CreateAndEnableHook(CreateDXGIFactory, CreateDXGIFactory_Detour, reinterpret_cast<LPVOID *>(&CreateDXGIFactory_Original), "CreateDXGIFactory")) {
            LogError("Failed to create and enable CreateDXGIFactory ReShade hook");
            return false;
        }
        LogInfo("CreateDXGIFactory ReShade hook created successfully");
    }

    // Hook CreateDXGIFactory1 - try both system and ReShade versions
    auto CreateDXGIFactory1_sys = reinterpret_cast<decltype(&CreateDXGIFactory1)>(GetProcAddress(dxgi_module, "CreateDXGIFactory1"));
    if (CreateDXGIFactory1_sys != nullptr) {
        if (!CreateAndEnableHook(CreateDXGIFactory1_sys, CreateDXGIFactory1_Detour, reinterpret_cast<LPVOID *>(&CreateDXGIFactory1_Original), "CreateDXGIFactory1")) {
            LogError("Failed to create and enable CreateDXGIFactory1 system hook");
            return false;
        }
        LogInfo("CreateDXGIFactory1 system hook created successfully");
    } else {
        LogWarn("Failed to get CreateDXGIFactory1 system address, trying ReShade version");
        if (!CreateAndEnableHook(CreateDXGIFactory1, CreateDXGIFactory1_Detour, reinterpret_cast<LPVOID *>(&CreateDXGIFactory1_Original), "CreateDXGIFactory1")) {
            LogError("Failed to create and enable CreateDXGIFactory1 ReShade hook");
            return false;
        }
        LogInfo("CreateDXGIFactory1 ReShade hook created successfully");
    }

    LogInfo("DXGI hooks installed successfully");

    // Mark DXGI hooks as installed
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(display_commanderhooks::HookType::DXGI);

    return true;
}

bool InstallD3DDeviceHooks() {
    static bool d3d_device_hooks_installed = false;
    if (d3d_device_hooks_installed) {
        LogInfo("D3D device hooks already installed");
        return true;
    }

    // Check if D3D device hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(display_commanderhooks::HookType::D3D_DEVICE)) {
        LogInfo("D3D device hooks installation suppressed by user setting");
        return false;
    }

    d3d_device_hooks_installed = true;

    LogInfo("Installing D3D device creation hooks...");

    // Hook D3D11CreateDeviceAndSwapChain
    HMODULE d3d11_module = GetModuleHandleW(L"d3d11.dll");
    if (d3d11_module != nullptr) {
        auto D3D11CreateDeviceAndSwapChain_sys = reinterpret_cast<decltype(&D3D11CreateDeviceAndSwapChain)>(GetProcAddress(d3d11_module, "D3D11CreateDeviceAndSwapChain"));
        if (D3D11CreateDeviceAndSwapChain_sys != nullptr) {
            if (!CreateAndEnableHook(D3D11CreateDeviceAndSwapChain_sys, D3D11CreateDeviceAndSwapChain_Detour, reinterpret_cast<LPVOID *>(&D3D11CreateDeviceAndSwapChain_Original), "D3D11CreateDeviceAndSwapChain")) {
                LogError("Failed to create and enable D3D11CreateDeviceAndSwapChain hook");
                return false;
            }
            LogInfo("D3D11CreateDeviceAndSwapChain hook created successfully");
        } else {
            LogWarn("Failed to get D3D11CreateDeviceAndSwapChain address from d3d11.dll");
        }
    } else {
        LogWarn("Failed to get d3d11.dll module handle");
    }

    // Hook D3D12CreateDevice
    HMODULE d3d12_module = GetModuleHandleW(L"d3d12.dll");
    if (d3d12_module != nullptr) {
        auto D3D12CreateDevice_sys = reinterpret_cast<decltype(&D3D12CreateDevice)>(GetProcAddress(d3d12_module, "D3D12CreateDevice"));
        if (D3D12CreateDevice_sys != nullptr) {
            if (!CreateAndEnableHook(D3D12CreateDevice_sys, D3D12CreateDevice_Detour, reinterpret_cast<LPVOID *>(&D3D12CreateDevice_Original), "D3D12CreateDevice")) {
                LogError("Failed to create and enable D3D12CreateDevice hook");
                return false;
            }
            LogInfo("D3D12CreateDevice hook created successfully");
        } else {
            LogWarn("Failed to get D3D12CreateDevice address from d3d12.dll");
        }
    } else {
        LogWarn("Failed to get d3d12.dll module handle");
    }

    LogInfo("D3D device hooks installed successfully");

    // Mark D3D device hooks as installed
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(display_commanderhooks::HookType::D3D_DEVICE);

    return true;
}

bool InstallApiHooks() {
    if (g_api_hooks_installed.load()) {
        LogInfo("API hooks already installed");
        return true;
    }

    // Check if API hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(display_commanderhooks::HookType::API)) {
        LogInfo("API hooks installation suppressed by user setting");
        return false;
    }

    // Initialize MinHook (only if not already initialized)
    MH_STATUS init_status = SafeInitializeMinHook();
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for API hooks - Status: %d", init_status);
        return false;
    }

    if (init_status == MH_ERROR_ALREADY_INITIALIZED) {
        LogInfo("MinHook already initialized, proceeding with API hooks");
    } else {
        LogInfo("MinHook initialized successfully for API hooks");
    }
    // Hook GetFocus
    if (!CreateAndEnableHook(GetFocus, GetFocus_Detour, reinterpret_cast<LPVOID *>(&GetFocus_Original), "GetFocus")) {
        LogError("Failed to create and enable GetFocus hook");
    }

    // Hook GetForegroundWindow
    if (!CreateAndEnableHook(GetForegroundWindow, GetForegroundWindow_Detour, reinterpret_cast<LPVOID *>(&GetForegroundWindow_Original), "GetForegroundWindow")) {
        LogError("Failed to create and enable GetForegroundWindow hook");
    }

    // Hook GetActiveWindow
    if (!CreateAndEnableHook(GetActiveWindow, GetActiveWindow_Detour, reinterpret_cast<LPVOID *>(&GetActiveWindow_Original), "GetActiveWindow")) {
        LogError("Failed to create and enable GetActiveWindow hook");
    }

    // Hook GetGUIThreadInfo
    if (!CreateAndEnableHook(GetGUIThreadInfo, GetGUIThreadInfo_Detour, reinterpret_cast<LPVOID *>(&GetGUIThreadInfo_Original), "GetGUIThreadInfo")) {
        LogError("Failed to create and enable GetGUIThreadInfo hook");
    }

    // Hook SetThreadExecutionState
    if (!CreateAndEnableHook(SetThreadExecutionState, SetThreadExecutionState_Detour, reinterpret_cast<LPVOID *>(&SetThreadExecutionState_Original), "SetThreadExecutionState")) {
        LogError("Failed to create and enable SetThreadExecutionState hook");
    }

    // Hook SetWindowLongPtrW
    if (!CreateAndEnableHook(SetWindowLongPtrW, SetWindowLongPtrW_Detour, reinterpret_cast<LPVOID *>(&SetWindowLongPtrW_Original), "SetWindowLongPtrW")) {
        LogError("Failed to create and enable SetWindowLongPtrW hook");
    }

    // Hook SetWindowPos
    if (!CreateAndEnableHook(SetWindowPos, SetWindowPos_Detour, reinterpret_cast<LPVOID *>(&SetWindowPos_Original), "SetWindowPos")) {
        LogError("Failed to create and enable SetWindowPos hook");
    }

    #if 0
    // Hook SetCursor
    if (!CreateAndEnableHook(SetCursor, SetCursor_Detour, reinterpret_cast<LPVOID *>(&SetCursor_Original), "SetCursor")) {
        LogError("Failed to create and enable SetCursor hook");
    }

    // Hook ShowCursor
    if (!CreateAndEnableHook(ShowCursor, ShowCursor_Detour, reinterpret_cast<LPVOID *>(&ShowCursor_Original), "ShowCursor")) {
        LogError("Failed to create and enable ShowCursor hook");
    }
    #endif


    // todo: move to loadlibrary hooks
    // Install Windows message hooks


    // ### SAME LIBRARY ###
    if (!InstallWindowsMessageHooks()) {
        LogError("Failed to install Windows message hooks");
    }

    // Install timeslowdown hooks
    if (!InstallTimeslowdownHooks()) {
        LogError("Failed to install timeslowdown hooks");
    }

    // Install process exit hooks
    if (!InstallProcessExitHooks()) {
        LogError("Failed to install process exit hooks");
    }

    if (!InstallSleepHooks()) {
        LogError("Failed to install sleep hooks");
    }

    // Install LoadLibrary hooks
    if (!InstallLoadLibraryHooks()) {
        LogError("Failed to install LoadLibrary hooks");
    }

    // Install DirectInput hooks
    if (!InstallDirectInputHooks()) {
        LogError("Failed to install DirectInput hooks");
    }

    // Install OpenGL hooks
    if (!InstallOpenGLHooks()) {
        LogError("Failed to install OpenGL hooks");
    }

    // Install display settings hooks
    if (!InstallDisplaySettingsHooks()) {
        LogError("Failed to install display settings hooks");
    }

    // Install debug output hooks
    if (!debug_output::InstallDebugOutputHooks()) {
        LogError("Failed to install debug output hooks");
    }

    // Install D3D device creation hooks
    if (!InstallD3DDeviceHooks()) {
        LogError("Failed to install D3D device hooks");
    }

    g_api_hooks_installed.store(true);
    LogInfo("API hooks installed successfully");

    // Mark API hooks as installed
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(display_commanderhooks::HookType::API);

    // Debug: Show current continue rendering state
    bool current_state = s_continue_rendering.load();
    LogInfo("API hooks installed - continue_rendering state: %s", current_state ? "enabled" : "disabled");

    return true;
}

void UninstallApiHooks() {
    if (!g_api_hooks_installed.load()) {
        LogInfo("API hooks not installed");
        return;
    }

    // Uninstall Windows.Gaming.Input hooks
    UninstallWindowsGamingInputHooks();

    // Uninstall LoadLibrary hooks
    UninstallLoadLibraryHooks();

    // Uninstall DirectInput hooks
    UninstallDirectInputHooks();

    // Uninstall OpenGL hooks
    UninstallOpenGLHooks();

    // Uninstall Windows message hooks
    UninstallWindowsMessageHooks();


    // Uninstall sleep hooks
    UninstallSleepHooks();

    // Uninstall timeslowdown hooks
    UninstallTimeslowdownHooks();

    // Uninstall process exit hooks
    UninstallProcessExitHooks();

    // Uninstall debug output hooks
    debug_output::UninstallDebugOutputHooks();

    // NVAPI hooks are uninstalled via LoadLibrary hooks cleanup

    // Disable all hooks
    MH_DisableHook(MH_ALL_HOOKS);

    // Remove hooks
    MH_RemoveHook(GetFocus);
    MH_RemoveHook(GetForegroundWindow);
    MH_RemoveHook(GetActiveWindow);
    MH_RemoveHook(GetGUIThreadInfo);
    MH_RemoveHook(SetThreadExecutionState);
    MH_RemoveHook(SetWindowLongPtrW);
    MH_RemoveHook(SetWindowPos);
    MH_RemoveHook(SetCursor);
    MH_RemoveHook(ShowCursor);
    MH_RemoveHook(CreateDXGIFactory);
    MH_RemoveHook(CreateDXGIFactory1);

    // Remove D3D device hooks
    HMODULE d3d11_module = GetModuleHandleW(L"d3d11.dll");
    if (d3d11_module != nullptr) {
        auto D3D11CreateDeviceAndSwapChain_sys = reinterpret_cast<decltype(&D3D11CreateDeviceAndSwapChain)>(GetProcAddress(d3d11_module, "D3D11CreateDeviceAndSwapChain"));
        if (D3D11CreateDeviceAndSwapChain_sys != nullptr) {
            MH_RemoveHook(D3D11CreateDeviceAndSwapChain_sys);
        }
    }

    HMODULE d3d12_module = GetModuleHandleW(L"d3d12.dll");
    if (d3d12_module != nullptr) {
        auto D3D12CreateDevice_sys = reinterpret_cast<decltype(&D3D12CreateDevice)>(GetProcAddress(d3d12_module, "D3D12CreateDevice"));
        if (D3D12CreateDevice_sys != nullptr) {
            MH_RemoveHook(D3D12CreateDevice_sys);
        }
    }

    // Clean up
    GetFocus_Original = nullptr;
    GetForegroundWindow_Original = nullptr;
    GetActiveWindow_Original = nullptr;
    GetGUIThreadInfo_Original = nullptr;
    SetThreadExecutionState_Original = nullptr;
    SetWindowLongPtrW_Original = nullptr;
    SetWindowPos_Original = nullptr;
    SetCursor_Original = nullptr;
    ShowCursor_Original = nullptr;
    CreateDXGIFactory_Original = nullptr;
    CreateDXGIFactory1_Original = nullptr;
    D3D11CreateDeviceAndSwapChain_Original = nullptr;
    D3D12CreateDevice_Original = nullptr;

    g_api_hooks_installed.store(false);
    LogInfo("API hooks uninstalled successfully");
}
// Function to set the game window (should be called when we detect the game window)
void SetGameWindow(HWND hwnd) {
    g_game_window = hwnd;
    LogInfo("Game window set for API hooks - HWND: 0x%p", hwnd);
}

} // namespace display_commanderhooks
