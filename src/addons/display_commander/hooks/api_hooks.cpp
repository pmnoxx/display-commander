#include "api_hooks.hpp"
#include <d3d11.h>
#include <d3d12.h>
#include <MinHook.h>
#include <wrl/client.h>
#include "../settings/developer_tab_settings.hpp"
#include "../utils/detour_call_tracker.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"
#include "../utils/timing.hpp"
#include "debug_output_hooks.hpp"
#include "dinput_hooks.hpp"
#include "display_settings_hooks.hpp"
#include "dxgi/dxgi_present_hooks.hpp"
#include "dxgi_factory_wrapper.hpp"
#include "globals.hpp"
#include "hook_suppression_manager.hpp"
#include "loadlibrary_hooks.hpp"
#include "opengl_hooks.hpp"
#include "process_exit_hooks.hpp"
#include "rand_hooks.hpp"
#include "sleep_hooks.hpp"
#include "timeslowdown_hooks.hpp"
#include "windows_gaming_input_hooks.hpp"
#include "windows_hooks/windows_message_hooks.hpp"

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
SetWindowLongA_pfn SetWindowLongA_Original = nullptr;
SetWindowLongW_pfn SetWindowLongW_Original = nullptr;
SetWindowLongPtrA_pfn SetWindowLongPtrA_Original = nullptr;
SetWindowPos_pfn SetWindowPos_Original = nullptr;
SetCursor_pfn SetCursor_Original = nullptr;
ShowCursor_pfn ShowCursor_Original = nullptr;
AddVectoredExceptionHandler_pfn AddVectoredExceptionHandler_Original = nullptr;
CreateDXGIFactory_pfn CreateDXGIFactory_Original = nullptr;
CreateDXGIFactory1_pfn CreateDXGIFactory1_Original = nullptr;
D3D11CreateDeviceAndSwapChain_pfn D3D11CreateDeviceAndSwapChain_Original = nullptr;
D3D11CreateDevice_pfn D3D11CreateDevice_Original = nullptr;
D3D12CreateDevice_pfn D3D12CreateDevice_Original = nullptr;

// Hook state
static std::atomic<bool> g_api_hooks_installed{false};

// Get the game window handle (we'll need to track this)
static std::atomic<HWND> g_game_window = nullptr;

HWND GetGameWindow() { return g_game_window; }

bool HWNDBelongsToCurrentProcess(HWND hwnd) {
    DWORD dwPid = 0;
    DWORD dwTid = GetWindowThreadProcessId(hwnd, &dwPid);
    return GetCurrentProcessId() == dwPid;
}

// Hooked GetFocus function
HWND WINAPI GetFocus_Detour() {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    auto hwnd = GetFocus_Original ? GetFocus_Original() : GetFocus();

    if (HWNDBelongsToCurrentProcess(hwnd)) {
        return hwnd;
    }

    if (s_continue_rendering.load() && g_game_window.load() != nullptr && IsWindow(g_game_window.load())) {
        // Return the game window even when it doesn't have focus
        //   LogInfo("GetFocus_Detour: Returning game window due to continue rendering - HWND: 0x%p", g_game_window);
        return g_game_window;
    }

    // Call original function
    return hwnd;
}

HWND WINAPI GetForegroundWindow_Direct() {
    return GetForegroundWindow_Original ? GetForegroundWindow_Original() : GetForegroundWindow();
}

// Hooked GetForegroundWindow function
HWND WINAPI GetForegroundWindow_Detour() {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    auto hwnd = GetForegroundWindow_Direct();

    if (HWNDBelongsToCurrentProcess(hwnd)) {
        return hwnd;
    }

    if (s_continue_rendering.load() && g_game_window.load() != nullptr && IsWindow(g_game_window.load())) {
        // Return the game window even when it's not in foreground
        //    LogInfo("GetForegroundWindow_Detour: Returning game window due to continue rendering - HWND: 0x%p",
        //    g_game_window);
        return g_game_window;
    }

    // Call original function
    return hwnd;
}

// Hooked GetActiveWindow function
HWND WINAPI GetActiveWindow_Detour() {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    auto hwnd = GetActiveWindow_Original ? GetActiveWindow_Original() : GetActiveWindow();

    if (HWNDBelongsToCurrentProcess(hwnd)) {
        return hwnd;
    }

    if (s_continue_rendering.load() && g_game_window.load() != nullptr && IsWindow(g_game_window.load())) {
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
    return hwnd;
}

// Hooked GetGUIThreadInfo function
BOOL WINAPI GetGUIThreadInfo_Detour(DWORD idThread, PGUITHREADINFO pgui) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    if (s_continue_rendering.load() && g_game_window.load() != nullptr && IsWindow(g_game_window.load())) {
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
                pgui->hwndCapture = nullptr;      // Clear capture to prevent issues
                pgui->hwndCaret = g_game_window;  // Set caret to game window

                // Set appropriate flags (using standard Windows constants)
                pgui->flags = 0x00000001 | 0x00000002;  // GTI_CARETBLINKING | GTI_CARETSHOWN

                LogInfo(
                    "GetGUIThreadInfo_Detour: Modified thread info to show game window as active - HWND: 0x%p, "
                    "Thread: %lu",
                    g_game_window.load(), idThread);
            }
        }

        return result;
    }

    // Call original function
    return GetGUIThreadInfo_Original ? GetGUIThreadInfo_Original(idThread, pgui) : GetGUIThreadInfo(idThread, pgui);
}

// Hooked SetThreadExecutionState function
EXECUTION_STATE WINAPI SetThreadExecutionState_Detour(EXECUTION_STATE esFlags) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_SetThreadExecutionState].increment_total();

    // Check screensaver mode setting
    ScreensaverMode screensaver_mode = s_screensaver_mode.load();

    // If screensaver mode is DisableWhenFocused or Disable, ignore all calls
    if (screensaver_mode == ScreensaverMode::kDisableWhenFocused || screensaver_mode == ScreensaverMode::kDisable) {
        return 0x0;  // Block game's attempt to control execution state
    }

    // Track unsuppressed calls (when we call the original function)
    g_hook_stats[HOOK_SetThreadExecutionState].increment_unsuppressed();

    // Call original function for kDefault mode
    return SetThreadExecutionState_Original ? SetThreadExecutionState_Original(esFlags)
                                            : SetThreadExecutionState(esFlags);
}

// Hooked SetWindowLongPtrW function
LONG_PTR WINAPI SetWindowLongPtrW_Detour(HWND hWnd, int nIndex, LONG_PTR dwNewLong) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Only process if prevent_always_on_top is enabled
    if (hWnd == g_game_window) {
        ModifyWindowStyle(nIndex, dwNewLong, settings::g_developerTabSettings.prevent_always_on_top.GetValue());
    }

    // Call original function with unmodified value
    return SetWindowLongPtrW_Original(hWnd, nIndex, dwNewLong);
}

// Hooked SetWindowLongA function
LONG WINAPI SetWindowLongA_Detour(HWND hWnd, int nIndex, LONG dwNewLong) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    g_display_settings_hook_counters[DISPLAY_SETTINGS_HOOK_SETWINDOWLONGA].fetch_add(1);
    g_display_settings_hook_total_count.fetch_add(1);

    // Check if fullscreen prevention is enabled
    if (hWnd == g_game_window) {
        ModifyWindowStyle(nIndex, dwNewLong, settings::g_developerTabSettings.prevent_always_on_top.GetValue());
    }

    // Call original function with unmodified value
    return SetWindowLongA_Original(hWnd, nIndex, dwNewLong);
}

// Hooked SetWindowLongW function
LONG WINAPI SetWindowLongW_Detour(HWND hWnd, int nIndex, LONG dwNewLong) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    g_display_settings_hook_counters[DISPLAY_SETTINGS_HOOK_SETWINDOWLONGW].fetch_add(1);
    g_display_settings_hook_total_count.fetch_add(1);
    // Check if fullscreen prevention is enabled
    if (hWnd == g_game_window) {
        ModifyWindowStyle(nIndex, dwNewLong, settings::g_developerTabSettings.prevent_always_on_top.GetValue());
    }

    return SetWindowLongW_Original(hWnd, nIndex, dwNewLong);
}

// Hooked SetWindowLongPtrA function
LONG_PTR WINAPI SetWindowLongPtrA_Detour(HWND hWnd, int nIndex, LONG_PTR dwNewLong) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    g_display_settings_hook_counters[DISPLAY_SETTINGS_HOOK_SETWINDOWLONGPTRA].fetch_add(1);
    g_display_settings_hook_total_count.fetch_add(1);

    // Check if fullscreen prevention is enabled
    // if (settings::g_developerTabSettings.prevent_fullscreen.GetValue()) {
    // Prevent window style changes that enable fullscreen
    if (hWnd == g_game_window) {
        ModifyWindowStyle(nIndex, dwNewLong, settings::g_developerTabSettings.prevent_always_on_top.GetValue());
    }
    // }

    return SetWindowLongPtrA_Original(hWnd, nIndex, dwNewLong);
}

// Hooked SetWindowPos function
BOOL WINAPI SetWindowPos_Detour(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Only process if prevent_always_on_top is enabled
    if (hWnd == g_game_window && settings::g_developerTabSettings.prevent_always_on_top.GetValue()
        && hWndInsertAfter != HWND_NOTOPMOST) {
        hWndInsertAfter = HWND_NOTOPMOST;
        // uFlags |= SWP_FRAMECHANGED; perhaphs not needed
        /*

        // Check if we're trying to set the window to be always on top
        if (hWndInsertAfter != HWND_TOPMOST) {
            // Replace HWND_TOPMOST with HWND_NOTOPMOST to prevent always-on-top behavior
            LogInfo("SetWindowPos: Preventing always-on-top for window 0x%p - Replacing HWND_TOPMOST with
        HWND_NOTOPMOST", hWnd);

            // Call original function with HWND_NOTOPMOST instead of HWND_TOPMOST
            return SetWindowPos_Original ? SetWindowPos_Original(hWnd, HWND_NOTOPMOST, X, Y, cx, cy, uFlags)
                                         : SetWindowPos(hWnd, HWND_NOTOPMOST, X, Y, cx, cy, uFlags);
        }*/
    }

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
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Store the cursor value atomically
    s_last_cursor_value.store(hCursor);
    // if (ShouldBlockMouseInput()) {
    //     hCursor = LoadCursor(nullptr, IDC_ARROW);
    //  }
    // hCursor = LoadCursor(nullptr, IDC_ARROW);

    // Call original function
    return SetCursor_Direct(hCursor);
}

int WINAPI ShowCursor_Direct(BOOL bShow) {
    // Call original function
    return ShowCursor_Original ? ShowCursor_Original(bShow) : ShowCursor(bShow);
}

// Hooked ShowCursor function
int WINAPI ShowCursor_Detour(BOOL bShow) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
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

// Hooked AddVectoredExceptionHandler function
PVOID WINAPI AddVectoredExceptionHandler_Detour(ULONG First, PVECTORED_EXCEPTION_HANDLER Handler) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Log the call for debugging
    LogDebug("AddVectoredExceptionHandler_Detour: First=%lu, Handler=0x%p", First, Handler);

    // Call original function
    return AddVectoredExceptionHandler_Original ? AddVectoredExceptionHandler_Original(First, Handler)
                                                : AddVectoredExceptionHandler(First, Handler);
}

// Hooked CreateDXGIFactory function
HRESULT WINAPI CreateDXGIFactory_Detour(REFIID riid, void** ppFactory) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Increment counter
    g_dxgi_factory_event_counters[DXGI_FACTORY_EVENT_CREATEFACTORY].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Call original function
    HRESULT hr =
        CreateDXGIFactory_Original ? CreateDXGIFactory_Original(riid, ppFactory) : CreateDXGIFactory(riid, ppFactory);

    // If successful and we got a factory, wrap it with our proxy
    if (SUCCEEDED(hr) && ppFactory != nullptr && *ppFactory != nullptr) {
        // Try to query for IDXGIFactory7 (highest version)
        Microsoft::WRL::ComPtr<IDXGIFactory7> factory7;
        if (SUCCEEDED(static_cast<IUnknown*>(*ppFactory)->QueryInterface(IID_PPV_ARGS(&factory7)))) {
            // AddRef the factory so wrapper can take ownership (wrapper doesn't AddRef in constructor)
            // This ensures the factory stays alive when ComPtr releases and when we release the original
            factory7->AddRef();

            // Create wrapper for the factory (wrapper takes ownership, doesn't AddRef)
            display_commanderhooks::DXGIFactoryWrapper* wrapper = new display_commanderhooks::DXGIFactoryWrapper(
                factory7.Get(), display_commanderhooks::SwapChainHook::NativeRaw);

            // Release the original factory reference from the original function
            static_cast<IUnknown*>(*ppFactory)->Release();

            // Replace with wrapper (wrapper already has refcount of 1 from constructor)
            *ppFactory = wrapper;
            // No need to AddRef - wrapper constructor sets refcount to 1
            // When ComPtr goes out of scope, it will release its ref, but wrapper's AddRef keeps it alive

            LogInfo("CreateDXGIFactory: Created factory wrapper for IDXGIFactory7: 0x%p", wrapper);
        } else {
            LogWarn("CreateDXGIFactory: Failed to query IDXGIFactory7, skipping wrapper creation");
        }
    }

    return hr;
}

// Hooked CreateDXGIFactory1 function
HRESULT WINAPI CreateDXGIFactory1_Detour(REFIID riid, void** ppFactory) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Increment counter
    g_dxgi_factory_event_counters[DXGI_FACTORY_EVENT_CREATEFACTORY1].fetch_add(1);
    g_swapchain_event_total_count.fetch_add(1);

    // Call original function
    HRESULT hr = CreateDXGIFactory1_Original ? CreateDXGIFactory1_Original(riid, ppFactory)
                                             : CreateDXGIFactory1(riid, ppFactory);

    // If successful and we got a factory, wrap it with our proxy
    if (SUCCEEDED(hr) && ppFactory != nullptr && *ppFactory != nullptr) {
        // Try to query for IDXGIFactory7 (highest version)
        Microsoft::WRL::ComPtr<IDXGIFactory7> factory7;
        if (SUCCEEDED(static_cast<IUnknown*>(*ppFactory)->QueryInterface(IID_PPV_ARGS(&factory7)))) {
            // AddRef the factory so wrapper can take ownership (wrapper doesn't AddRef in constructor)
            // This ensures the factory stays alive when ComPtr releases and when we release the original
            factory7->AddRef();

            // Create wrapper for the factory (wrapper takes ownership, doesn't AddRef)
            display_commanderhooks::DXGIFactoryWrapper* wrapper = new display_commanderhooks::DXGIFactoryWrapper(
                factory7.Get(), display_commanderhooks::SwapChainHook::NativeRaw);

            // Release the original factory reference from the original function
            static_cast<IUnknown*>(*ppFactory)->Release();

            // Replace with wrapper (wrapper already has refcount of 1 from constructor)
            *ppFactory = wrapper;
            // No need to AddRef - wrapper constructor sets refcount to 1
            // When ComPtr goes out of scope, it will release its ref, but wrapper's AddRef keeps it alive

            LogInfo("CreateDXGIFactory1: Created factory wrapper for IDXGIFactory7: 0x%p", wrapper);
        } else {
            LogWarn("CreateDXGIFactory1: Failed to query IDXGIFactory7, skipping wrapper creation");
        }
    }

    return hr;
}

// Hooked D3D11CreateDeviceAndSwapChain function
HRESULT WINAPI D3D11CreateDeviceAndSwapChain_Detour(IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType,
                                                    HMODULE Software, UINT Flags,
                                                    const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels,
                                                    UINT SDKVersion, const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
                                                    IDXGISwapChain** ppSwapChain, ID3D11Device** ppDevice,
                                                    D3D_FEATURE_LEVEL* pFeatureLevel,
                                                    ID3D11DeviceContext** ppImmediateContext) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
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

    // Apply debug layer flag if enabled
    UINT modifiedFlags = Flags;
    if (settings::g_developerTabSettings.debug_layer_enabled.GetValue()) {
        modifiedFlags |= D3D11_CREATE_DEVICE_DEBUG;
        LogInfo("  Debug layer enabled - Modified Flags: 0x%08X", modifiedFlags);
    }

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
        LogInfo("    BufferDesc.RefreshRate: %u/%u", pSwapChainDesc->BufferDesc.RefreshRate.Numerator,
                pSwapChainDesc->BufferDesc.RefreshRate.Denominator);
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

    // Call original function with modified flags
    HRESULT hr = D3D11CreateDeviceAndSwapChain_Original
                     ? D3D11CreateDeviceAndSwapChain_Original(pAdapter, DriverType, Software, modifiedFlags,
                                                              pFeatureLevels, FeatureLevels, SDKVersion, pSwapChainDesc,
                                                              ppSwapChain, ppDevice, pFeatureLevel, ppImmediateContext)
                     : E_FAIL;  // D3D11CreateDeviceAndSwapChain not available

    LogInfo("  Result: 0x%08X (%s)", hr, SUCCEEDED(hr) ? "SUCCESS" : "FAILED");

    // Setup D3D11 debug info queue if debug layer is enabled and device creation was successful
    if (SUCCEEDED(hr) && ppDevice && *ppDevice && settings::g_developerTabSettings.debug_layer_enabled.GetValue()) {
        ID3D11Device* device = static_cast<ID3D11Device*>(*ppDevice);
        Microsoft::WRL::ComPtr<ID3D11Debug> debug_device;
        HRESULT debug_hr = device->QueryInterface(IID_PPV_ARGS(&debug_device));
        if (SUCCEEDED(debug_hr)) {
            Microsoft::WRL::ComPtr<ID3D11InfoQueue> info_queue;
            HRESULT info_hr = debug_device->QueryInterface(IID_PPV_ARGS(&info_queue));
            if (SUCCEEDED(info_hr)) {
                // Only set break on severity if the setting is enabled
                if (settings::g_developerTabSettings.debug_break_on_severity.GetValue()) {
                    info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, true);
                    info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
                    info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_WARNING, true);
                    info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_INFO, true);
                    info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_MESSAGE, true);
                    LogInfo("  D3D11 debug info queue configured for all severity levels");
                } else {
                    LogInfo("  D3D11 debug info queue configured (SetBreakOnSeverity disabled)");
                }
            } else {
                LogWarn("  Failed to get D3D11 info queue: 0x%08X", info_hr);
            }
        } else {
            LogWarn("  Failed to get D3D11 debug device: 0x%08X", debug_hr);
        }
    }

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

// Hooked D3D11CreateDevice function
HRESULT WINAPI D3D11CreateDevice_Detour(IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software,
                                        UINT Flags, const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels,
                                        UINT SDKVersion, ID3D11Device** ppDevice, D3D_FEATURE_LEVEL* pFeatureLevel,
                                        ID3D11DeviceContext** ppImmediateContext) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    LogInfo("=== D3D11CreateDevice Called ===");
    LogInfo("  pAdapter: 0x%p", pAdapter);
    LogInfo("  DriverType: %d", DriverType);
    LogInfo("  Software: 0x%p", Software);
    LogInfo("  Flags: 0x%08X", Flags);
    LogInfo("  pFeatureLevels: 0x%p", pFeatureLevels);
    LogInfo("  FeatureLevels: %u", FeatureLevels);
    LogInfo("  SDKVersion: %u", SDKVersion);
    LogInfo("  ppDevice: 0x%p", ppDevice);
    LogInfo("  pFeatureLevel: 0x%p", pFeatureLevel);
    LogInfo("  ppImmediateContext: 0x%p", ppImmediateContext);

    // Apply debug layer flag if enabled
    UINT modifiedFlags = Flags;
    if (settings::g_developerTabSettings.debug_layer_enabled.GetValue()) {
        modifiedFlags |= D3D11_CREATE_DEVICE_DEBUG;
        LogInfo("  Debug layer enabled - Modified Flags: 0x%08X", modifiedFlags);
    }

    // Log feature levels if provided
    if (pFeatureLevels && FeatureLevels > 0) {
        LogInfo("  Feature Levels:");
        for (UINT i = 0; i < FeatureLevels; i++) {
            LogInfo("    [%u]: 0x%04X", i, pFeatureLevels[i]);
        }
    }

    // Call original function with modified flags
    HRESULT hr = D3D11CreateDevice_Original ? D3D11CreateDevice_Original(pAdapter, DriverType, Software, modifiedFlags,
                                                                         pFeatureLevels, FeatureLevels, SDKVersion,
                                                                         ppDevice, pFeatureLevel, ppImmediateContext)
                                            : E_FAIL;  // D3D11CreateDevice not available

    LogInfo("  Result: 0x%08X (%s)", hr, SUCCEEDED(hr) ? "SUCCESS" : "FAILED");

    // Setup D3D11 debug info queue if debug layer is enabled and device creation was successful
    if (SUCCEEDED(hr) && ppDevice && *ppDevice && settings::g_developerTabSettings.debug_layer_enabled.GetValue()) {
        ID3D11Device* device = static_cast<ID3D11Device*>(*ppDevice);
        Microsoft::WRL::ComPtr<ID3D11Debug> debug_device;
        HRESULT debug_hr = device->QueryInterface(IID_PPV_ARGS(&debug_device));
        if (SUCCEEDED(debug_hr)) {
            Microsoft::WRL::ComPtr<ID3D11InfoQueue> info_queue;
            HRESULT info_hr = debug_device->QueryInterface(IID_PPV_ARGS(&info_queue));
            if (SUCCEEDED(info_hr)) {
                // Only set break on severity if the setting is enabled
                if (settings::g_developerTabSettings.debug_break_on_severity.GetValue()) {
                    info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, true);
                    info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
                    info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_WARNING, true);
                    info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_INFO, true);
                    info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_MESSAGE, true);
                    LogInfo("  D3D11 debug info queue configured for all severity levels");
                } else {
                    LogInfo("  D3D11 debug info queue configured (SetBreakOnSeverity disabled)");
                }
            } else {
                LogWarn("  Failed to get D3D11 info queue: 0x%08X", info_hr);
            }
        } else {
            LogWarn("  Failed to get D3D11 debug device: 0x%08X", debug_hr);
        }
    }

    // Log output parameters if successful
    if (SUCCEEDED(hr)) {
        if (ppDevice && *ppDevice) {
            LogInfo("  Created Device: 0x%p", *ppDevice);
        }
        if (ppImmediateContext && *ppImmediateContext) {
            LogInfo("  Created Context: 0x%p", *ppImmediateContext);
        }
        if (pFeatureLevel && *pFeatureLevel) {
            LogInfo("  Feature Level: 0x%04X", *pFeatureLevel);
        }
    }

    LogInfo("=== D3D11CreateDevice Complete ===");
    return hr;
}

// Hooked D3D12CreateDevice function
HRESULT WINAPI D3D12CreateDevice_Detour(IUnknown* pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel, REFIID riid,
                                        void** ppDevice) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    LogInfo("=== D3D12CreateDevice Called ===");
    LogInfo("  pAdapter: 0x%p", pAdapter);
    LogInfo("  MinimumFeatureLevel: 0x%04X", MinimumFeatureLevel);
    LogInfo("  riid: {%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", riid.Data1, riid.Data2, riid.Data3,
            riid.Data4[0], riid.Data4[1], riid.Data4[2], riid.Data4[3], riid.Data4[4], riid.Data4[5], riid.Data4[6],
            riid.Data4[7]);
    LogInfo("  ppDevice: 0x%p", ppDevice);

    // Call original function
    HRESULT hr = D3D12CreateDevice_Original ? D3D12CreateDevice_Original(pAdapter, MinimumFeatureLevel, riid, ppDevice)
                                            : E_FAIL;  // D3D12CreateDevice not available

    LogInfo("  Result: 0x%08X (%s)", hr, SUCCEEDED(hr) ? "SUCCESS" : "FAILED");

    // Enable debug layer if setting is enabled and device creation was successful
    if (SUCCEEDED(hr) && ppDevice && *ppDevice && settings::g_developerTabSettings.debug_layer_enabled.GetValue()) {
        LogInfo("  Enabling D3D12 debug layer...");

        // Get D3D12 debug interface
        HMODULE d3d12_module = GetModuleHandleW(L"d3d12.dll");
        if (d3d12_module != nullptr) {
            auto D3D12GetDebugInterface = reinterpret_cast<decltype(&::D3D12GetDebugInterface)>(
                GetProcAddress(d3d12_module, "D3D12GetDebugInterface"));
            if (D3D12GetDebugInterface != nullptr) {
                Microsoft::WRL::ComPtr<ID3D12Debug> debug_controller;
                HRESULT debug_hr = D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller));
                if (SUCCEEDED(debug_hr) && debug_controller != nullptr) {
                    debug_controller->EnableDebugLayer();
                    LogInfo("  D3D12 debug layer enabled successfully");
                } else {
                    LogWarn("  Failed to enable D3D12 debug layer: 0x%08X", debug_hr);
                }
            } else {
                LogWarn("  D3D12GetDebugInterface not available");
            }
        } else {
            LogWarn("  d3d12.dll module not found");
        }

        // Setup debug interface to break on any warnings/errors (similar to DX12_ENABLE_DEBUG_LAYER)
        if (SUCCEEDED(hr) && ppDevice && *ppDevice) {
            ID3D12Device* device = static_cast<ID3D12Device*>(*ppDevice);
            Microsoft::WRL::ComPtr<ID3D12InfoQueue> info_queue;
            HRESULT info_hr = device->QueryInterface(IID_PPV_ARGS(&info_queue));
            if (SUCCEEDED(info_hr)) {
                // Only set break on severity if the setting is enabled
                if (settings::g_developerTabSettings.debug_break_on_severity.GetValue()) {
                    info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
                    info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
                    info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
                    info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_INFO, true);
                    info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_MESSAGE, true);
                    LogInfo("  D3D12 debug info queue configured for all severity levels");
                } else {
                    LogInfo("  D3D12 debug info queue configured (SetBreakOnSeverity disabled)");
                }
            } else {
                LogWarn("  Failed to get D3D12 info queue: 0x%08X", info_hr);
            }
        }
    }

    // Log output parameters if successful
    if (SUCCEEDED(hr) && ppDevice && *ppDevice) {
        LogInfo("  Created Device: 0x%p", *ppDevice);
    }

    LogInfo("=== D3D12CreateDevice Complete ===");
    return hr;
}

bool InstallDxgiFactoryHooks(HMODULE dxgi_module) {
    if (!enabled_experimental_features) {
        return true;
    }
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Check if this module is ReShade's proxy by checking for ReShade exports
    FARPROC reshade_register = GetProcAddress(dxgi_module, "ReShadeRegisterAddon");
    FARPROC reshade_unregister = GetProcAddress(dxgi_module, "ReShadeUnregisterAddon");
    if (reshade_register != nullptr && reshade_unregister != nullptr) {
        LogInfo("Skipping DXGI hooks installation - detected ReShade proxy module (0x%p)", dxgi_module);
        return true;
    }

    static bool dxgi_hooks_installed = false;
    if (dxgi_hooks_installed) {
        LogInfo("DXGI hooks already installed");
        return true;
    }

    // Check if DXGI hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::DXGI_FACTORY)) {
        LogInfo("DXGI hooks installation suppressed by user setting");
        return false;
    }

    dxgi_hooks_installed = true;

    // Initialize MinHook (only if not already initialized)
    MH_STATUS init_status = SafeInitializeMinHook(display_commanderhooks::HookType::DXGI_FACTORY);
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for DXGI hooks - Status: %d", init_status);
        return false;
    }

    if (init_status == MH_ERROR_ALREADY_INITIALIZED) {
        LogInfo("MinHook already initialized, proceeding with DXGI hooks");
    } else {
        LogInfo("MinHook initialized successfully for DXGI hooks");
    }

    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
        display_commanderhooks::HookType::DXGI_FACTORY);

    // Hook CreateDXGIFactory - try both system and ReShade versions
    auto CreateDXGIFactory_sys =
        reinterpret_cast<decltype(&CreateDXGIFactory)>(GetProcAddress(dxgi_module, "CreateDXGIFactory"));
    if (CreateDXGIFactory_sys != nullptr) {
        if (!CreateAndEnableHook(CreateDXGIFactory_sys, CreateDXGIFactory_Detour,
                                 reinterpret_cast<LPVOID*>(&CreateDXGIFactory_Original), "CreateDXGIFactory")) {
            LogError("Failed to create and enable CreateDXGIFactory system hook");
            return false;
        }
        LogInfo("CreateDXGIFactory system hook created successfully");
    } else {
        LogWarn("Failed to get CreateDXGIFactory system address, trying ReShade version");
        if (!CreateAndEnableHook(CreateDXGIFactory, CreateDXGIFactory_Detour,
                                 reinterpret_cast<LPVOID*>(&CreateDXGIFactory_Original), "CreateDXGIFactory")) {
            LogError("Failed to create and enable CreateDXGIFactory ReShade hook");
            return false;
        }
        LogInfo("CreateDXGIFactory ReShade hook created successfully");
    }

    // Hook CreateDXGIFactory1 - try both system and ReShade versions
    auto CreateDXGIFactory1_sys =
        reinterpret_cast<decltype(&CreateDXGIFactory1)>(GetProcAddress(dxgi_module, "CreateDXGIFactory1"));
    if (CreateDXGIFactory1_sys != nullptr) {
        if (!CreateAndEnableHook(CreateDXGIFactory1_sys, CreateDXGIFactory1_Detour,
                                 reinterpret_cast<LPVOID*>(&CreateDXGIFactory1_Original), "CreateDXGIFactory1")) {
            LogError("Failed to create and enable CreateDXGIFactory1 system hook");
            return false;
        }
        LogInfo("CreateDXGIFactory1 system hook created successfully");
    } else {
        LogWarn("Failed to get CreateDXGIFactory1 system address, trying ReShade version");
        if (!CreateAndEnableHook(CreateDXGIFactory1, CreateDXGIFactory1_Detour,
                                 reinterpret_cast<LPVOID*>(&CreateDXGIFactory1_Original), "CreateDXGIFactory1")) {
            LogError("Failed to create and enable CreateDXGIFactory1 ReShade hook");
            return false;
        }
        LogInfo("CreateDXGIFactory1 ReShade hook created successfully");
    }

    LogInfo("DXGI hooks installed successfully");

    // Mark DXGI hooks as installed

    return true;
}

bool InstallD3D11DeviceHooks(HMODULE d3d11_module) {
    // Check if this module is ReShade's proxy by checking for ReShade exports
    FARPROC reshade_register = GetProcAddress(d3d11_module, "ReShadeRegisterAddon");
    FARPROC reshade_unregister = GetProcAddress(d3d11_module, "ReShadeUnregisterAddon");
    if (reshade_register != nullptr && reshade_unregister != nullptr) {
        LogInfo("Skipping D3D11 hooks installation - detected ReShade proxy module (0x%p)", d3d11_module);
        return true;
    }

    static bool d3d11_device_hooks_installed = false;
    if (d3d11_device_hooks_installed) {
        LogInfo("D3D11 device hooks already installed");
        return true;
    }

    // Check if D3D device hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::D3D_DEVICE)) {
        LogInfo("D3D11 device hooks installation suppressed by user setting");
        return false;
    }

    d3d11_device_hooks_installed = true;

    LogInfo("Installing D3D11 device creation hooks...");

    // Hook D3D11CreateDeviceAndSwapChain
    auto D3D11CreateDeviceAndSwapChain_sys = reinterpret_cast<decltype(&D3D11CreateDeviceAndSwapChain)>(
        GetProcAddress(d3d11_module, "D3D11CreateDeviceAndSwapChain"));
    if (D3D11CreateDeviceAndSwapChain_sys != nullptr) {
        if (!CreateAndEnableHook(D3D11CreateDeviceAndSwapChain_sys, D3D11CreateDeviceAndSwapChain_Detour,
                                 reinterpret_cast<LPVOID*>(&D3D11CreateDeviceAndSwapChain_Original),
                                 "D3D11CreateDeviceAndSwapChain")) {
            LogError("Failed to create and enable D3D11CreateDeviceAndSwapChain hook");
            return false;
        }
        LogInfo("D3D11CreateDeviceAndSwapChain hook created successfully");
    } else {
        LogWarn("Failed to get D3D11CreateDeviceAndSwapChain address from d3d11.dll");
    }

    // Hook D3D11CreateDevice
    auto D3D11CreateDevice_sys =
        reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(d3d11_module, "D3D11CreateDevice"));
    if (D3D11CreateDevice_sys != nullptr) {
        if (!CreateAndEnableHook(D3D11CreateDevice_sys, D3D11CreateDevice_Detour,
                                 reinterpret_cast<LPVOID*>(&D3D11CreateDevice_Original), "D3D11CreateDevice")) {
            LogError("Failed to create and enable D3D11CreateDevice hook");
            return false;
        }
        LogInfo("D3D11CreateDevice hook created successfully");
    } else {
        LogWarn("Failed to get D3D11CreateDevice address from d3d11.dll");
    }

    LogInfo("D3D11 device hooks installed successfully");

    // Mark D3D device hooks as installed
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
        display_commanderhooks::HookType::D3D_DEVICE);

    return true;
}

bool InstallD3D12DeviceHooks(HMODULE d3d12_module) {
    // Check if this module is ReShade's proxy by checking for ReShade exports
    FARPROC reshade_register = GetProcAddress(d3d12_module, "ReShadeRegisterAddon");
    FARPROC reshade_unregister = GetProcAddress(d3d12_module, "ReShadeUnregisterAddon");
    if (reshade_register != nullptr && reshade_unregister != nullptr) {
        LogInfo("Skipping D3D12 hooks installation - detected ReShade proxy module (0x%p)", d3d12_module);
        return true;
    }

    static bool d3d12_device_hooks_installed = false;
    if (d3d12_device_hooks_installed) {
        LogInfo("D3D12 device hooks already installed");
        return true;
    }

    // Check if D3D device hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::D3D_DEVICE)) {
        LogInfo("D3D12 device hooks installation suppressed by user setting");
        return false;
    }

    d3d12_device_hooks_installed = true;

    LogInfo("Installing D3D12 device creation hooks...");

    // Hook D3D12CreateDevice
    auto D3D12CreateDevice_sys =
        reinterpret_cast<decltype(&D3D12CreateDevice)>(GetProcAddress(d3d12_module, "D3D12CreateDevice"));
    if (D3D12CreateDevice_sys != nullptr) {
        if (!CreateAndEnableHook(D3D12CreateDevice_sys, D3D12CreateDevice_Detour,
                                 reinterpret_cast<LPVOID*>(&D3D12CreateDevice_Original), "D3D12CreateDevice")) {
            LogError("Failed to create and enable D3D12CreateDevice hook");
            return false;
        }
        LogInfo("D3D12CreateDevice hook created successfully");
    } else {
        LogWarn("Failed to get D3D12CreateDevice address from d3d12.dll");
    }

    LogInfo("D3D12 device hooks installed successfully");

    // Mark D3D device hooks as installed
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
        display_commanderhooks::HookType::D3D_DEVICE);

    return true;
}

bool InstallWindowsApiHooks() {
    // Check if Windows API hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::WINDOW_API)) {
        LogInfo("Windows API hooks installation suppressed by user setting");
        return false;
    }

    // MH initialize
    MH_STATUS init_status = SafeInitializeMinHook(display_commanderhooks::HookType::WINDOW_API);
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for Windows API hooks - Status: %d", init_status);
        return false;
    }

    if (init_status == MH_ERROR_ALREADY_INITIALIZED) {
        LogInfo("MinHook already initialized, proceeding with Windows API hooks");
    } else {
        LogInfo("MinHook initialized successfully for Windows API hooks");
    }

    LogInfo("Installing Windows API hooks...");

    // Hook GetFocus
    if (!CreateAndEnableHook(GetFocus, GetFocus_Detour, reinterpret_cast<LPVOID*>(&GetFocus_Original), "GetFocus")) {
        LogError("Failed to create and enable GetFocus hook");
    }

    // Hook GetForegroundWindow
    if (!CreateAndEnableHook(GetForegroundWindow, GetForegroundWindow_Detour,
                             reinterpret_cast<LPVOID*>(&GetForegroundWindow_Original), "GetForegroundWindow")) {
        LogError("Failed to create and enable GetForegroundWindow hook");
    }

    // Hook GetActiveWindow
    if (!CreateAndEnableHook(GetActiveWindow, GetActiveWindow_Detour,
                             reinterpret_cast<LPVOID*>(&GetActiveWindow_Original), "GetActiveWindow")) {
        LogError("Failed to create and enable GetActiveWindow hook");
    }

    // Hook GetGUIThreadInfo
    if (!CreateAndEnableHook(GetGUIThreadInfo, GetGUIThreadInfo_Detour,
                             reinterpret_cast<LPVOID*>(&GetGUIThreadInfo_Original), "GetGUIThreadInfo")) {
        LogError("Failed to create and enable GetGUIThreadInfo hook");
    }

    // Hook SetThreadExecutionState
    if (!CreateAndEnableHook(SetThreadExecutionState, SetThreadExecutionState_Detour,
                             reinterpret_cast<LPVOID*>(&SetThreadExecutionState_Original), "SetThreadExecutionState")) {
        LogError("Failed to create and enable SetThreadExecutionState hook");
    }

    // Hook SetWindowLongPtrW
    if (!CreateAndEnableHook(SetWindowLongPtrW, SetWindowLongPtrW_Detour,
                             reinterpret_cast<LPVOID*>(&SetWindowLongPtrW_Original), "SetWindowLongPtrW")) {
        LogError("Failed to create and enable SetWindowLongPtrW hook");
    }

    // Hook SetWindowLongA
    if (!CreateAndEnableHook(SetWindowLongA, SetWindowLongA_Detour, reinterpret_cast<LPVOID*>(&SetWindowLongA_Original),
                             "SetWindowLongA")) {
        LogError("Failed to create and enable SetWindowLongA hook");
    }

    // Hook SetWindowLongW
    if (!CreateAndEnableHook(SetWindowLongW, SetWindowLongW_Detour, reinterpret_cast<LPVOID*>(&SetWindowLongW_Original),
                             "SetWindowLongW")) {
        LogError("Failed to create and enable SetWindowLongW hook");
    }

    // Hook SetWindowLongPtrA
    if (!CreateAndEnableHook(SetWindowLongPtrA, SetWindowLongPtrA_Detour,
                             reinterpret_cast<LPVOID*>(&SetWindowLongPtrA_Original), "SetWindowLongPtrA")) {
        LogError("Failed to create and enable SetWindowLongPtrA hook");
    }

    // Hook SetWindowPos
    if (!CreateAndEnableHook(SetWindowPos, SetWindowPos_Detour, reinterpret_cast<LPVOID*>(&SetWindowPos_Original),
                             "SetWindowPos")) {
        LogError("Failed to create and enable SetWindowPos hook");
    }

    // Hook AddVectoredExceptionHandler
    if (!CreateAndEnableHook(AddVectoredExceptionHandler, AddVectoredExceptionHandler_Detour,
                             reinterpret_cast<LPVOID*>(&AddVectoredExceptionHandler_Original),
                             "AddVectoredExceptionHandler")) {
        LogError("Failed to create and enable AddVectoredExceptionHandler hook");
    }

    LogInfo("Windows API hooks installed successfully");

    // Mark Windows API hooks as installed
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
        display_commanderhooks::HookType::WINDOW_API);

    return true;
}

bool InstallApiHooks() {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    if (g_api_hooks_installed.load()) {
        // LogInfo("API hooks already installed");
        return true;
    }
#if 1
    // Initialize MinHook (only if not already initialized)
    MH_STATUS init_status = SafeInitializeMinHook(display_commanderhooks::HookType::API);
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for API hooks - Status: %d", init_status);
        return false;
    }

    if (init_status == MH_ERROR_ALREADY_INITIALIZED) {
        LogInfo("MinHook already initialized, proceeding with API hooks");
    } else {
        LogInfo("MinHook initialized successfully for API hooks");
    }
#endif

    // Install Windows API hooks
    InstallWindowsApiHooks();
    // todo: move to loadlibrary hooks
    // Install Windows message hooks

    // ### SAME LIBRARY ###
    InstallWindowsMessageHooks();

    if (enabled_experimental_features) {
        InstallTimeslowdownHooks();
    }

    InstallProcessExitHooks();

    InstallSleepHooks();

    // Install LoadLibrary hooks
    InstallLoadLibraryHooks();

    // Install DirectInput hooks
    InstallDirectInputHooks();

    // Install OpenGL hooks
    InstallOpenGLHooks();

    // Install display settings hooks
    InstallDisplaySettingsHooks();

    // Install debug output hooks
    debug_output::InstallDebugOutputHooks();

    // D3D device creation hooks are now installed via OnModuleLoaded when d3d11.dll or d3d12.dll is loaded

    // Install rand hooks (experimental feature)
    if (enabled_experimental_features) {
        InstallRandHooks();
    }

    g_api_hooks_installed.store(true);
    LogInfo("API hooks installed successfully");

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

    // Uninstall rand hooks
    UninstallRandHooks();

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
    MH_RemoveHook(SetWindowLongA);
    MH_RemoveHook(SetWindowLongW);
    MH_RemoveHook(SetWindowLongPtrA);
    MH_RemoveHook(SetWindowPos);
    MH_RemoveHook(SetCursor);
    MH_RemoveHook(ShowCursor);
    MH_RemoveHook(AddVectoredExceptionHandler);
    MH_RemoveHook(CreateDXGIFactory);
    MH_RemoveHook(CreateDXGIFactory1);

    // Remove D3D device hooks
    HMODULE d3d11_module = GetModuleHandleW(L"d3d11.dll");
    if (d3d11_module != nullptr) {
        auto D3D11CreateDeviceAndSwapChain_sys = reinterpret_cast<decltype(&D3D11CreateDeviceAndSwapChain)>(
            GetProcAddress(d3d11_module, "D3D11CreateDeviceAndSwapChain"));
        if (D3D11CreateDeviceAndSwapChain_sys != nullptr) {
            MH_RemoveHook(D3D11CreateDeviceAndSwapChain_sys);
        }

        auto D3D11CreateDevice_sys =
            reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(d3d11_module, "D3D11CreateDevice"));
        if (D3D11CreateDevice_sys != nullptr) {
            MH_RemoveHook(D3D11CreateDevice_sys);
        }
    }

    HMODULE d3d12_module = GetModuleHandleW(L"d3d12.dll");
    if (d3d12_module != nullptr) {
        auto D3D12CreateDevice_sys =
            reinterpret_cast<decltype(&D3D12CreateDevice)>(GetProcAddress(d3d12_module, "D3D12CreateDevice"));
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
    SetWindowLongA_Original = nullptr;
    SetWindowLongW_Original = nullptr;
    SetWindowLongPtrA_Original = nullptr;
    SetWindowPos_Original = nullptr;
    SetCursor_Original = nullptr;
    ShowCursor_Original = nullptr;
    AddVectoredExceptionHandler_Original = nullptr;
    CreateDXGIFactory_Original = nullptr;
    CreateDXGIFactory1_Original = nullptr;
    D3D11CreateDeviceAndSwapChain_Original = nullptr;
    D3D11CreateDevice_Original = nullptr;
    D3D12CreateDevice_Original = nullptr;

    g_api_hooks_installed.store(false);
    LogInfo("API hooks uninstalled successfully");
}
// Function to set the game window (should be called when we detect the game window)
void SetGameWindow(HWND hwnd) {
    g_game_window = hwnd;
    LogInfo("Game window set for API hooks - HWND: 0x%p", hwnd);
}

}  // namespace display_commanderhooks
