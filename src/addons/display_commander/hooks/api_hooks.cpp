#include "api_hooks.hpp"
#include <d3d11.h>
#include <d3d12.h>
#include <MinHook.h>
#include <wrl/client.h>
#include <cstdio>
#include "../settings/advanced_tab_settings.hpp"
#include "../settings/main_tab_settings.hpp"
#include "../utils/detour_call_tracker.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"
#include "../utils/timing.hpp"
#include "debug_output_hooks.hpp"
#include "dinput_hooks.hpp"
#include "display_settings_hooks.hpp"
#include "dpi_hooks.hpp"
#include "dxgi/dxgi_present_hooks.hpp"
#include "dxgi_factory_wrapper.hpp"
#include "globals.hpp"
#include "hook_suppression_manager.hpp"
#include "loadlibrary_hooks.hpp"
#include "opengl_hooks.hpp"
#include "pclstats_etw_hooks.hpp"
#include "process_exit_hooks.hpp"
#include "rand_hooks.hpp"
#include "sleep_hooks.hpp"
#include "timeslowdown_hooks.hpp"
#include "windows_gaming_input_hooks.hpp"
#include "windows_hooks/windows_message_hooks.hpp"

namespace display_commanderhooks {

// Original function pointers
GetFocus_pfn GetFocus_Original = nullptr;
GetForegroundWindow_pfn GetForegroundWindow_Original = nullptr;
GetActiveWindow_pfn GetActiveWindow_Original = nullptr;
GetGUIThreadInfo_pfn GetGUIThreadInfo_Original = nullptr;
IsIconic_pfn IsIconic_Original = nullptr;
IsWindowVisible_pfn IsWindowVisible_Original = nullptr;
GetWindowPlacement_pfn GetWindowPlacement_Original = nullptr;
SetThreadExecutionState_pfn SetThreadExecutionState_Original = nullptr;
SetWindowLongPtrW_pfn SetWindowLongPtrW_Original = nullptr;
SetWindowLongA_pfn SetWindowLongA_Original = nullptr;
SetWindowLongW_pfn SetWindowLongW_Original = nullptr;
SetWindowLongPtrA_pfn SetWindowLongPtrA_Original = nullptr;
SetWindowPos_pfn SetWindowPos_Original = nullptr;
CreateWindowExW_pfn CreateWindowExW_Original = nullptr;
SetCursor_pfn SetCursor_Original = nullptr;
ShowCursor_pfn ShowCursor_Original = nullptr;
AddVectoredExceptionHandler_pfn AddVectoredExceptionHandler_Original = nullptr;
CreateDXGIFactory_pfn CreateDXGIFactory_Original = nullptr;
CreateDXGIFactory1_pfn CreateDXGIFactory1_Original = nullptr;
CreateDXGIFactory2_pfn CreateDXGIFactory2_Original = nullptr;
D3D11CreateDeviceAndSwapChain_pfn D3D11CreateDeviceAndSwapChain_Original = nullptr;
D3D11CreateDevice_pfn D3D11CreateDevice_Original = nullptr;
D3D12CreateDevice_pfn D3D12CreateDevice_Original = nullptr;

// Hook state
static std::atomic<bool> g_api_hooks_installed{false};

// Continue Rendering API debug: last return value and override per API (written in detours, read by UI).
namespace {
enum CRDebugIndex {
    CR_GetFocus = 0,
    CR_GetForegroundWindow,
    CR_GetActiveWindow,
    CR_GetGUIThreadInfo,
    CR_IsIconic,
    CR_IsWindowVisible,
};
struct CRDebugEntry {
    std::atomic<uintptr_t> last_value{0};
    std::atomic<uint64_t> last_call_time_ns{0};
    std::atomic<bool> did_override{false};
};
static CRDebugEntry g_cr_debug[CR_DEBUG_API_COUNT];
static const char* const g_cr_names[CR_DEBUG_API_COUNT] = {
    "GetFocus", "GetForegroundWindow", "GetActiveWindow", "GetGUIThreadInfo", "IsIconic", "IsWindowVisible",
};
// Format REFIID/GUID for logging (avoids wrong/undefined output from %s with GUID).
static std::string FormatRefIid(REFIID riid) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", riid.Data1, riid.Data2,
                  riid.Data3, riid.Data4[0], riid.Data4[1], riid.Data4[2], riid.Data4[3], riid.Data4[4], riid.Data4[5],
                  riid.Data4[6], riid.Data4[7]);
    return std::string(buf);
}
static void RecordCRDebug(int idx, uintptr_t value, bool did_override) {
    g_cr_debug[idx].last_value.store(value, std::memory_order_relaxed);
    g_cr_debug[idx].last_call_time_ns.store(utils::get_now_ns(), std::memory_order_relaxed);
    g_cr_debug[idx].did_override.store(did_override, std::memory_order_relaxed);
}
}  // namespace

void GetContinueRenderingApiDebugSnapshots(ContinueRenderingApiDebugSnapshot* out) {
    for (int i = 0; i < CR_DEBUG_API_COUNT && out != nullptr; ++i) {
        out[i].api_name = g_cr_names[i];
        out[i].last_value = g_cr_debug[i].last_value.load(std::memory_order_relaxed);
        out[i].last_call_time_ns = g_cr_debug[i].last_call_time_ns.load(std::memory_order_relaxed);
        out[i].did_override = g_cr_debug[i].did_override.load(std::memory_order_relaxed);
        out[i].value_is_bool = (i == CR_IsIconic || i == CR_IsWindowVisible);
    }
}

HWND GetGameWindow() { return g_last_swapchain_hwnd.load(std::memory_order_acquire); }

bool HWNDBelongsToCurrentProcess(HWND hwnd) {
    DWORD dwPid = 0;
    DWORD dwTid = GetWindowThreadProcessId(hwnd, &dwPid);
    return GetCurrentProcessId() == dwPid;
}

// Hooked GetFocus function
HWND WINAPI GetFocus_Detour() {
    CALL_GUARD(utils::get_now_ns());
    auto hwnd = GetFocus_Original ? GetFocus_Original() : GetFocus();

    if (HWNDBelongsToCurrentProcess(hwnd)) {
        RecordCRDebug(CR_GetFocus, reinterpret_cast<uintptr_t>(hwnd), false);
        return hwnd;
    }

    if (settings::g_advancedTabSettings.continue_rendering.GetValue()) {
        HWND game_hwnd = g_last_swapchain_hwnd.load();
        if (game_hwnd != nullptr) {
            RecordCRDebug(CR_GetFocus, reinterpret_cast<uintptr_t>(game_hwnd), true);
            return game_hwnd;
        }
    }

    RecordCRDebug(CR_GetFocus, reinterpret_cast<uintptr_t>(hwnd), false);
    return hwnd;
}

HWND WINAPI GetForegroundWindow_Direct() {
    return GetForegroundWindow_Original ? GetForegroundWindow_Original() : GetForegroundWindow();
}

// Hooked GetForegroundWindow function
HWND WINAPI GetForegroundWindow_Detour() {
    CALL_GUARD(utils::get_now_ns());
    auto hwnd = GetForegroundWindow_Direct();

    if (HWNDBelongsToCurrentProcess(hwnd)) {
        RecordCRDebug(CR_GetForegroundWindow, reinterpret_cast<uintptr_t>(hwnd), false);
        return hwnd;
    }

    if (settings::g_advancedTabSettings.continue_rendering.GetValue()) {
        HWND game_hwnd = g_last_swapchain_hwnd.load();
        if (game_hwnd != nullptr) {
            RecordCRDebug(CR_GetForegroundWindow, reinterpret_cast<uintptr_t>(game_hwnd), true);
            return game_hwnd;
        }
    }

    RecordCRDebug(CR_GetForegroundWindow, reinterpret_cast<uintptr_t>(hwnd), false);
    return hwnd;
}

// Hooked GetActiveWindow function
HWND WINAPI GetActiveWindow_Detour() {
    CALL_GUARD(utils::get_now_ns());
    auto hwnd = GetActiveWindow_Original ? GetActiveWindow_Original() : GetActiveWindow();

    if (HWNDBelongsToCurrentProcess(hwnd)) {
        RecordCRDebug(CR_GetActiveWindow, reinterpret_cast<uintptr_t>(hwnd), false);
        return hwnd;
    }

    if (settings::g_advancedTabSettings.continue_rendering.GetValue()) {
        HWND game_hwnd = g_last_swapchain_hwnd.load();
        if (game_hwnd == nullptr) {
            RecordCRDebug(CR_GetActiveWindow, 0, false);
            return nullptr;
        }
        DWORD dwPid = 0;
        DWORD dwTid = GetWindowThreadProcessId(game_hwnd, &dwPid);

        if (GetCurrentThreadId() == dwTid) {
            RecordCRDebug(CR_GetActiveWindow, reinterpret_cast<uintptr_t>(game_hwnd), true);
            return game_hwnd;
        }
        if (GetCurrentProcessId() == dwPid) {
            RecordCRDebug(CR_GetActiveWindow, reinterpret_cast<uintptr_t>(game_hwnd), true);
            return game_hwnd;
        }
        RecordCRDebug(CR_GetActiveWindow, reinterpret_cast<uintptr_t>(game_hwnd), true);
        return game_hwnd;
    }

    RecordCRDebug(CR_GetActiveWindow, reinterpret_cast<uintptr_t>(hwnd), false);
    return hwnd;
}

// Hooked GetGUIThreadInfo function
BOOL WINAPI GetGUIThreadInfo_Detour(DWORD idThread, PGUITHREADINFO pgui) {
    CALL_GUARD(utils::get_now_ns());
    HWND game_hwnd = g_last_swapchain_hwnd.load();
    if (settings::g_advancedTabSettings.continue_rendering.GetValue() && game_hwnd != nullptr && IsWindow(game_hwnd)) {
        // Call original function first
        BOOL result =
            GetGUIThreadInfo_Original ? GetGUIThreadInfo_Original(idThread, pgui) : GetGUIThreadInfo(idThread, pgui);

        if (result && pgui != nullptr) {
            // Modify the thread info to show game window as active (pgui validated to avoid crash on API misuse)
            DWORD dwPid = 0;
            DWORD dwTid = GetWindowThreadProcessId(game_hwnd, &dwPid);

            if (idThread == dwTid || idThread == 0) {
                // Set the game window as active and focused
                pgui->hwndActive = game_hwnd;
                pgui->hwndFocus = game_hwnd;
                pgui->hwndCapture = nullptr;  // Clear capture to prevent issues
                pgui->hwndCaret = game_hwnd;  // Set caret to game window

                // Set appropriate flags (using standard Windows constants)
                pgui->flags = 0x00000001 | 0x00000002;  // GTI_CARETBLINKING | GTI_CARETSHOWN

                RecordCRDebug(CR_GetGUIThreadInfo, reinterpret_cast<uintptr_t>(game_hwnd), true);
                LogInfo(
                    "GetGUIThreadInfo_Detour: Modified thread info to show game window as active - HWND: 0x%p, "
                    "Thread: %lu",
                    game_hwnd, idThread);
            } else {
                RecordCRDebug(CR_GetGUIThreadInfo, reinterpret_cast<uintptr_t>(pgui->hwndActive), false);
            }
        } else {
            RecordCRDebug(CR_GetGUIThreadInfo, 0, false);
        }

        return result;
    }

    BOOL result =
        GetGUIThreadInfo_Original ? GetGUIThreadInfo_Original(idThread, pgui) : GetGUIThreadInfo(idThread, pgui);
    RecordCRDebug(CR_GetGUIThreadInfo, (pgui && result) ? reinterpret_cast<uintptr_t>(pgui->hwndActive) : 0, false);
    return result;
}

// True minimized state, bypassing our IsIconic detour (used when we need real state, e.g. skip ApplyWindowChange).
bool IsIconic_direct(HWND hwnd) { return (IsIconic_Original ? IsIconic_Original(hwnd) : IsIconic(hwnd)) != FALSE; }

// True visibility state, bypassing our IsWindowVisible detour (used when code needs real visibility).
bool IsWindowVisible_direct(HWND hwnd) {
    return (IsWindowVisible_Original ? IsWindowVisible_Original(hwnd) : IsWindowVisible(hwnd)) != FALSE;
}

// Hooked IsIconic: when Continue Rendering is on, game window must not appear minimized (games treat minimized as
// background).
BOOL WINAPI IsIconic_Detour(HWND hWnd) {
    if (settings::g_advancedTabSettings.continue_rendering.GetValue() && hWnd == g_last_swapchain_hwnd.load()) {
        RecordCRDebug(CR_IsIconic, 0, true);  // we return FALSE (0)
        return FALSE;                         // Spoof "not minimized"
    }
    BOOL ret = IsIconic_Original ? IsIconic_Original(hWnd) : IsIconic(hWnd);
    RecordCRDebug(CR_IsIconic, ret ? 1u : 0u, false);
    return ret;
}

// Hooked IsWindowVisible: when Continue Rendering is on, game window must appear visible (some games check this for
// foreground).
BOOL WINAPI IsWindowVisible_Detour(HWND hWnd) {
    if (settings::g_advancedTabSettings.continue_rendering.GetValue() && hWnd == g_last_swapchain_hwnd.load()) {
        RecordCRDebug(CR_IsWindowVisible, 1, true);  // we return TRUE (1)
        return TRUE;                                 // Spoof "visible"
    }
    BOOL ret = IsWindowVisible_direct(hWnd);
    RecordCRDebug(CR_IsWindowVisible, ret ? 1u : 0u, false);
    return ret;
}

// Hooked GetWindowPlacement: when Continue Rendering is on, game window must not report SW_SHOWMINIMIZED (games treat
// minimized as background).
BOOL WINAPI GetWindowPlacement_Detour(HWND hWnd, WINDOWPLACEMENT* lpwndpl) {
    BOOL result =
        GetWindowPlacement_Original ? GetWindowPlacement_Original(hWnd, lpwndpl) : GetWindowPlacement(hWnd, lpwndpl);
    if (result && lpwndpl != nullptr && settings::g_advancedTabSettings.continue_rendering.GetValue()
        && hWnd == g_last_swapchain_hwnd.load()) {
        if (lpwndpl->showCmd == SW_SHOWMINIMIZED) {
            lpwndpl->showCmd = SW_SHOWNORMAL;  // Spoof "not minimized"
        }
    }
    return result;
}

// Hooked SetThreadExecutionState function
EXECUTION_STATE WINAPI SetThreadExecutionState_Detour(EXECUTION_STATE esFlags) {
    CALL_GUARD(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_SetThreadExecutionState].increment_total();

    // Check prevent display sleep & screensaver mode setting
    ScreensaverMode screensaver_mode =
        static_cast<ScreensaverMode>(settings::g_mainTabSettings.screensaver_mode.GetValue());

    // If mode is DisableWhenFocused or Disable, ignore all calls
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
    CALL_GUARD(utils::get_now_ns());
    g_hook_stats[HOOK_SetWindowLongPtrW].increment_total();
    // Only process if prevent_always_on_top is enabled
    if (hWnd == g_last_swapchain_hwnd.load()) {
        ModifyWindowStyle(nIndex, dwNewLong, settings::g_advancedTabSettings.prevent_always_on_top.GetValue());
    }

    g_hook_stats[HOOK_SetWindowLongPtrW].increment_unsuppressed();
    return SetWindowLongPtrW_Original ? SetWindowLongPtrW_Original(hWnd, nIndex, dwNewLong)
                                      : SetWindowLongPtrW(hWnd, nIndex, dwNewLong);
}

// Hooked SetWindowLongA function
LONG WINAPI SetWindowLongA_Detour(HWND hWnd, int nIndex, LONG dwNewLong) {
    CALL_GUARD(utils::get_now_ns());
    g_hook_stats[HOOK_SetWindowLongA].increment_total();

    // Check if fullscreen prevention is enabled
    if (hWnd == g_last_swapchain_hwnd.load()) {
        ModifyWindowStyle(nIndex, dwNewLong, settings::g_advancedTabSettings.prevent_always_on_top.GetValue());
    }

    g_hook_stats[HOOK_SetWindowLongA].increment_unsuppressed();
    return SetWindowLongA_Original ? SetWindowLongA_Original(hWnd, nIndex, dwNewLong)
                                   : SetWindowLongA(hWnd, nIndex, dwNewLong);
}

// Hooked SetWindowLongW function
LONG WINAPI SetWindowLongW_Detour(HWND hWnd, int nIndex, LONG dwNewLong) {
    CALL_GUARD(utils::get_now_ns());
    g_hook_stats[HOOK_SetWindowLongW].increment_total();
    // Check if fullscreen prevention is enabled
    if (hWnd == g_last_swapchain_hwnd.load()) {
        ModifyWindowStyle(nIndex, dwNewLong, settings::g_advancedTabSettings.prevent_always_on_top.GetValue());
    }

    g_hook_stats[HOOK_SetWindowLongW].increment_unsuppressed();
    return SetWindowLongW_Original ? SetWindowLongW_Original(hWnd, nIndex, dwNewLong)
                                   : SetWindowLongW(hWnd, nIndex, dwNewLong);
}

// Hooked SetWindowLongPtrA function
LONG_PTR WINAPI SetWindowLongPtrA_Detour(HWND hWnd, int nIndex, LONG_PTR dwNewLong) {
    CALL_GUARD(utils::get_now_ns());
    g_hook_stats[HOOK_SetWindowLongPtrA].increment_total();

    // Check if fullscreen prevention is enabled
    // if (settings::g_advancedTabSettings.prevent_fullscreen.GetValue()) {
    // Prevent window style changes that enable fullscreen
    if (hWnd == g_last_swapchain_hwnd.load()) {
        ModifyWindowStyle(nIndex, dwNewLong, settings::g_advancedTabSettings.prevent_always_on_top.GetValue());
    }
    // }

    g_hook_stats[HOOK_SetWindowLongPtrA].increment_unsuppressed();
    return SetWindowLongPtrA_Original ? SetWindowLongPtrA_Original(hWnd, nIndex, dwNewLong)
                                      : SetWindowLongPtrA(hWnd, nIndex, dwNewLong);
}

// Hooked SetWindowPos function
BOOL WINAPI SetWindowPos_Detour(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags) {
    CALL_GUARD(utils::get_now_ns());
    g_hook_stats[HOOK_SetWindowPos].increment_total();
    // Only process if prevent_always_on_top is enabled
    if (hWnd == g_last_swapchain_hwnd.load() && settings::g_advancedTabSettings.prevent_always_on_top.GetValue()
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

    g_hook_stats[HOOK_SetWindowPos].increment_unsuppressed();
    // Call original function with unmodified parameters
    return SetWindowPos_Original ? SetWindowPos_Original(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags)
                                 : SetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

BOOL WINAPI SetWindowPos_Direct(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags) {
    return SetWindowPos_Original ? SetWindowPos_Original(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags)
                                 : SetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

HWND WINAPI CreateWindowW_Direct(LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth,
                                 int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam) {
    return CreateWindowExW_Original ? CreateWindowExW_Original(0, lpClassName, lpWindowName, dwStyle, X, Y, nWidth,
                                                               nHeight, hWndParent, hMenu, hInstance, lpParam)
                                    : CreateWindowExW(0, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight,
                                                      hWndParent, hMenu, hInstance, lpParam);
}

HWND WINAPI CreateWindowExW_Detour(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int X,
                                   int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance,
                                   LPVOID lpParam) {
    return CreateWindowExW_Original ? CreateWindowExW_Original(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y,
                                                               nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam)
                                    : CreateWindowExW(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth,
                                                      nHeight, hWndParent, hMenu, hInstance, lpParam);
}

HCURSOR WINAPI SetCursor_Direct(HCURSOR hCursor) {
    // Call original function
    return SetCursor_Original ? SetCursor_Original(hCursor) : SetCursor(hCursor);
}

// Hooked SetCursor function
HCURSOR WINAPI SetCursor_Detour(HCURSOR hCursor) {
    CALL_GUARD(utils::get_now_ns());
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
    CALL_GUARD(utils::get_now_ns());

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
    CALL_GUARD(utils::get_now_ns());
    // Log the call for debugging
    LogDebug("AddVectoredExceptionHandler_Detour: First=%lu, Handler=0x%p", First, Handler);

    // Call original function
    return AddVectoredExceptionHandler_Original ? AddVectoredExceptionHandler_Original(First, Handler)
                                                : AddVectoredExceptionHandler(First, Handler);
}

// Hooked CreateDXGIFactory2 function
HRESULT WINAPI CreateDXGIFactory2_Detour(UINT Flags, REFIID riid, void** ppFactory) {
    CALL_GUARD(utils::get_now_ns());
    if (ppFactory == nullptr) return E_POINTER;
    // Increment counter
    g_dxgi_factory_event_counters[DXGI_FACTORY_EVENT_CREATEFACTORY2].fetch_add(1);

    std::array<const GUID, 8> rrids = {__uuidof(IDXGIFactory),  __uuidof(IDXGIFactory1), __uuidof(IDXGIFactory2),
                                       __uuidof(IDXGIFactory3), __uuidof(IDXGIFactory4), __uuidof(IDXGIFactory5),
                                       __uuidof(IDXGIFactory6), __uuidof(IDXGIFactory7)};

    if (std::find(rrids.begin(), rrids.end(), riid) == rrids.end()) {
        LogWarn("CreateDXGIFactory2: Unknown interface %s", FormatRefIid(riid).c_str());
        return E_NOINTERFACE;
    }
    LogInfo("CreateDXGIFactory2: Found interface %s -> IDXGIFactory7", FormatRefIid(riid).c_str());
    // Upgrading interface
    const GUID rrid_override = __uuidof(IDXGIFactory7);

    // Call original function
    HRESULT hr = CreateDXGIFactory2_Original ? CreateDXGIFactory2_Original(Flags, rrid_override, ppFactory)
                                             : CreateDXGIFactory2(Flags, rrid_override, ppFactory);

    if (SUCCEEDED(hr) && ppFactory != nullptr && *ppFactory != nullptr) {
        display_commanderhooks::dxgi::HookFactory(static_cast<IDXGIFactory*>(*ppFactory));
    }
    return hr;
}

// Hooked CreateDXGIFactory function
HRESULT WINAPI CreateDXGIFactory_Detour(REFIID riid, void** ppFactory) {
    CALL_GUARD(utils::get_now_ns());
    // Increment counter
    g_dxgi_factory_event_counters[DXGI_FACTORY_EVENT_CREATEFACTORY].fetch_add(1);

    LogInfo("Redirecting CreateDXGIFactory to CreateDXGIFactory2");
    return CreateDXGIFactory2(0, riid, ppFactory);
}

// Hooked CreateDXGIFactory1 function
HRESULT WINAPI CreateDXGIFactory1_Detour(REFIID riid, void** ppFactory) {
    CALL_GUARD(utils::get_now_ns());
    // Increment counter
    g_dxgi_factory_event_counters[DXGI_FACTORY_EVENT_CREATEFACTORY1].fetch_add(1);

    LogInfo("Redirecting CreateDXGIFactory1 to CreateDXGIFactory2");

    return CreateDXGIFactory2(0, riid, ppFactory);
}

HRESULT CreateDXGIFactory1_Direct(REFIID riid, void** ppFactory) {
    if (ppFactory == nullptr) {
        return E_POINTER;
    }
    if (CreateDXGIFactory1_Original != nullptr) {
        return CreateDXGIFactory1_Original(riid, ppFactory);
    }
    return CreateDXGIFactory1(riid, ppFactory);
}

// Hooked D3D11CreateDeviceAndSwapChain function
HRESULT WINAPI D3D11CreateDeviceAndSwapChain_Detour(IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType,
                                                    HMODULE Software, UINT Flags,
                                                    const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels,
                                                    UINT SDKVersion, const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
                                                    IDXGISwapChain** ppSwapChain, ID3D11Device** ppDevice,
                                                    D3D_FEATURE_LEVEL* pFeatureLevel,
                                                    ID3D11DeviceContext** ppImmediateContext) {
    CALL_GUARD(utils::get_now_ns());
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
    if (settings::g_advancedTabSettings.debug_layer_enabled.GetValue()) {
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
    if (FAILED(hr)) {
        LogError("[D3D11 error] D3D11CreateDeviceAndSwapChain returned 0x%08X", static_cast<unsigned>(hr));
    }

    // Setup D3D11 debug info queue if debug layer is enabled and device creation was successful
    if (SUCCEEDED(hr) && ppDevice && *ppDevice && settings::g_advancedTabSettings.debug_layer_enabled.GetValue()) {
        ID3D11Device* device = static_cast<ID3D11Device*>(*ppDevice);
        Microsoft::WRL::ComPtr<ID3D11Debug> debug_device;
        HRESULT debug_hr = device->QueryInterface(IID_PPV_ARGS(&debug_device));
        if (SUCCEEDED(debug_hr)) {
            Microsoft::WRL::ComPtr<ID3D11InfoQueue> info_queue;
            HRESULT info_hr = debug_device->QueryInterface(IID_PPV_ARGS(&info_queue));
            if (SUCCEEDED(info_hr)) {
                // Only set break on severity if the setting is enabled
                if (settings::g_advancedTabSettings.debug_break_on_severity.GetValue()) {
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
    // Log output parameters if successful
    if (SUCCEEDED(hr)) {
        if (ppDevice && *ppDevice) {
            LogInfo("  Created Device: 0x%p", *ppDevice);
            // Get DXGI factory from device (same path as ReShade d3d11.cpp: device -> IDXGIDevice -> GetAdapter ->
            // GetParent) so that IDXGIFactory::CreateSwapChain (and ForHwnd/ForCoreWindow) vtable hooks are installed
            // when the app never calls CreateDXGIFactory1/2 and only uses D3D11CreateDevice.
            IDXGIDevice* dxgi_device = nullptr;
            HRESULT qhr = (*ppDevice)->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi_device));
            if (SUCCEEDED(qhr) && dxgi_device != nullptr) {
                IDXGIAdapter* adapter = nullptr;
                qhr = dxgi_device->GetAdapter(&adapter);
                dxgi_device->Release();
                if (SUCCEEDED(qhr) && adapter != nullptr) {
                    IDXGIFactory* factory = nullptr;
                    qhr = adapter->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory));
                    adapter->Release();
                    if (SUCCEEDED(qhr) && factory != nullptr) {
                        display_commanderhooks::dxgi::HookFactory(factory);
                        factory->Release();
                    }
                }
            }
        }
        if (ppImmediateContext && *ppImmediateContext) {
            LogInfo("  Created Context: 0x%p", *ppImmediateContext);
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
    CALL_GUARD(utils::get_now_ns());
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
    if (settings::g_advancedTabSettings.debug_layer_enabled.GetValue()) {
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
    if (FAILED(hr)) {
        LogError("[D3D11 error] D3D11CreateDevice returned 0x%08X", static_cast<unsigned>(hr));
    }

    // Setup D3D11 debug info queue if debug layer is enabled and device creation was successful
    if (SUCCEEDED(hr) && ppDevice && *ppDevice && settings::g_advancedTabSettings.debug_layer_enabled.GetValue()) {
        ID3D11Device* device = static_cast<ID3D11Device*>(*ppDevice);
        Microsoft::WRL::ComPtr<ID3D11Debug> debug_device;
        HRESULT debug_hr = device->QueryInterface(IID_PPV_ARGS(&debug_device));
        if (SUCCEEDED(debug_hr)) {
            Microsoft::WRL::ComPtr<ID3D11InfoQueue> info_queue;
            HRESULT info_hr = debug_device->QueryInterface(IID_PPV_ARGS(&info_queue));
            if (SUCCEEDED(info_hr)) {
                // Only set break on severity if the setting is enabled
                if (settings::g_advancedTabSettings.debug_break_on_severity.GetValue()) {
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
            // Get DXGI factory from device (same path as ReShade d3d11.cpp: device -> IDXGIDevice -> GetAdapter ->
            // GetParent) so that IDXGIFactory::CreateSwapChain (and ForHwnd/ForCoreWindow) vtable hooks are installed
            // when the app never calls CreateDXGIFactory1/2 and only uses D3D11CreateDevice.
            IDXGIDevice* dxgi_device = nullptr;
            HRESULT qhr = (*ppDevice)->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi_device));
            if (SUCCEEDED(qhr) && dxgi_device != nullptr) {
                IDXGIAdapter* adapter = nullptr;
                qhr = dxgi_device->GetAdapter(&adapter);
                dxgi_device->Release();
                if (SUCCEEDED(qhr) && adapter != nullptr) {
                    IDXGIFactory* factory = nullptr;
                    qhr = adapter->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory));
                    adapter->Release();
                    if (SUCCEEDED(qhr) && factory != nullptr) {
                        display_commanderhooks::dxgi::HookFactory(factory);
                        factory->Release();
                    }
                }
            }
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
    CALL_GUARD(utils::get_now_ns());
    LogInfo("=== D3D12CreateDevice Called ===");
    LogInfo("  pAdapter: 0x%p", pAdapter);
    LogInfo("  MinimumFeatureLevel: 0x%04X", MinimumFeatureLevel);
    LogInfo("  riid: %s", FormatRefIid(riid).c_str());
    LogInfo("  ppDevice: 0x%p", ppDevice);

    // Call original function
    HRESULT hr = D3D12CreateDevice_Original ? D3D12CreateDevice_Original(pAdapter, MinimumFeatureLevel, riid, ppDevice)
                                            : E_FAIL;  // D3D12CreateDevice not available

    LogInfo("  Result: 0x%08X (%s)", hr, SUCCEEDED(hr) ? "SUCCESS" : "FAILED");

    // Enable debug layer if setting is enabled and device creation was successful
    if (SUCCEEDED(hr) && ppDevice && *ppDevice && settings::g_advancedTabSettings.debug_layer_enabled.GetValue()) {
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
                if (settings::g_advancedTabSettings.debug_break_on_severity.GetValue()) {
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
    if (!display_commanderhooks::g_hooked_before_reshade.load()) {
        return true;
    }
    CALL_GUARD(utils::get_now_ns());
    // Check if this module is ReShade's proxy by checking for ReShade exports
    FARPROC reshade_register = GetProcAddress(dxgi_module, "ReShadeRegisterAddon");
    FARPROC reshade_unregister = GetProcAddress(dxgi_module, "ReShadeUnregisterAddon");
    if (reshade_register != nullptr || reshade_unregister != nullptr) {
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

    // Hook CreateDXGIFactory2 - try both system and ReShade versions
    auto CreateDXGIFactory2_sys =
        reinterpret_cast<CreateDXGIFactory2_pfn>(GetProcAddress(dxgi_module, "CreateDXGIFactory2"));
    if (CreateDXGIFactory2_sys != nullptr) {
        if (!CreateAndEnableHook(CreateDXGIFactory2_sys, CreateDXGIFactory2_Detour,
                                 reinterpret_cast<LPVOID*>(&CreateDXGIFactory2_Original), "CreateDXGIFactory2")) {
            LogError("Failed to create and enable CreateDXGIFactory2 system hook");
            return false;
        }
        LogInfo("CreateDXGIFactory2 system hook created successfully");
    } else {
        LogWarn("Failed to get CreateDXGIFactory2 system address, trying ReShade version");
        if (!CreateAndEnableHook(CreateDXGIFactory2, CreateDXGIFactory2_Detour,
                                 reinterpret_cast<LPVOID*>(&CreateDXGIFactory2_Original), "CreateDXGIFactory2")) {
            LogError("Failed to create and enable CreateDXGIFactory2 ReShade hook");
            return false;
        }
        LogInfo("CreateDXGIFactory2 ReShade hook created successfully");
    }

    LogInfo("DXGI hooks installed successfully");

    // Mark DXGI hooks as installed

    return true;
}

bool InstallD3D11DeviceHooks(HMODULE d3d11_module) {
    if (g_reshade_module != nullptr && !display_commanderhooks::g_hooked_before_reshade.load()) {
        return true;
    }
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

    // Check if D3D11 device hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::D3D11_DEVICE)) {
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

    // Mark D3D11 device hooks as installed
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
        display_commanderhooks::HookType::D3D11_DEVICE);

    return true;
}

bool InstallD3D12DeviceHooks(HMODULE d3d12_module) {
    if (!display_commanderhooks::g_hooked_before_reshade.load()) {
        return true;
    }
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

    // Check if D3D12 device hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::D3D12_DEVICE)) {
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

    // Mark D3D12 device hooks as installed
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
        display_commanderhooks::HookType::D3D12_DEVICE);

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

    // Hook IsIconic / IsWindowVisible so the game does not see "minimized" or "not visible" when Continue Rendering is
    // on
    if (!CreateAndEnableHook(IsIconic, IsIconic_Detour, reinterpret_cast<LPVOID*>(&IsIconic_Original), "IsIconic")) {
        LogError("Failed to create and enable IsIconic hook");
    }
    if (!CreateAndEnableHook(IsWindowVisible, IsWindowVisible_Detour,
                             reinterpret_cast<LPVOID*>(&IsWindowVisible_Original), "IsWindowVisible")) {
        LogError("Failed to create and enable IsWindowVisible hook");
    }
    if (!CreateAndEnableHook(GetWindowPlacement, GetWindowPlacement_Detour,
                             reinterpret_cast<LPVOID*>(&GetWindowPlacement_Original), "GetWindowPlacement")) {
        LogError("Failed to create and enable GetWindowPlacement hook");
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

    // Hook CreateWindowExW (CreateWindowW is a macro that calls CreateWindowExW(0, ...)); bypass via
    // CreateWindowW_Direct
    if (!CreateAndEnableHook(CreateWindowExW, CreateWindowExW_Detour,
                             reinterpret_cast<LPVOID*>(&CreateWindowExW_Original), "CreateWindowExW")) {
        LogError("Failed to create and enable CreateWindowExW hook");
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
    CALL_GUARD(utils::get_now_ns());
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

    // DirectInput and OpenGL hooks are installed via OnModuleLoaded when dinput8.dll, dinput.dll, or opengl32.dll is
    // loaded

    // Install display settings hooks
    InstallDisplaySettingsHooks();

    // Install DPI hooks
    display_commanderhooks::dpi::InstallDpiHooks();

    // Install debug output hooks
    debug_output::InstallDebugOutputHooks();

    // PCLStats ETW hooks are installed via OnModuleLoaded when advapi32.dll is loaded

    // D3D device creation hooks are now installed via OnModuleLoaded when d3d11.dll or d3d12.dll is loaded

    // Install rand hooks (experimental feature)
    if (enabled_experimental_features) {
        InstallRandHooks();
    }

    g_api_hooks_installed.store(true);
    LogInfo("API hooks installed successfully");

    // Debug: Show current continue rendering state
    bool current_state = settings::g_advancedTabSettings.continue_rendering.GetValue();
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

    // Uninstall PCLStats ETW hooks (installed via OnModuleLoaded when advapi32.dll loaded)
    UninstallPCLStatsEtwHooks();

    // Uninstall DPI hooks
    display_commanderhooks::dpi::UninstallDpiHooks();

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
    MH_RemoveHook(IsIconic);
    MH_RemoveHook(IsWindowVisible);
    MH_RemoveHook(GetWindowPlacement);
    MH_RemoveHook(SetThreadExecutionState);
    MH_RemoveHook(SetWindowLongPtrW);
    MH_RemoveHook(SetWindowLongA);
    MH_RemoveHook(SetWindowLongW);
    MH_RemoveHook(SetWindowLongPtrA);
    MH_RemoveHook(SetWindowPos);
    MH_RemoveHook(CreateWindowExW);
    MH_RemoveHook(SetCursor);
    MH_RemoveHook(ShowCursor);
    MH_RemoveHook(AddVectoredExceptionHandler);
    MH_RemoveHook(CreateDXGIFactory);
    MH_RemoveHook(CreateDXGIFactory1);
    MH_RemoveHook(CreateDXGIFactory2);

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
    IsIconic_Original = nullptr;
    IsWindowVisible_Original = nullptr;
    GetWindowPlacement_Original = nullptr;
    SetThreadExecutionState_Original = nullptr;
    SetWindowLongPtrW_Original = nullptr;
    SetWindowLongA_Original = nullptr;
    SetWindowLongW_Original = nullptr;
    SetWindowLongPtrA_Original = nullptr;
    SetWindowPos_Original = nullptr;
    CreateWindowExW_Original = nullptr;
    SetCursor_Original = nullptr;
    ShowCursor_Original = nullptr;
    AddVectoredExceptionHandler_Original = nullptr;
    CreateDXGIFactory_Original = nullptr;
    CreateDXGIFactory1_Original = nullptr;
    CreateDXGIFactory2_Original = nullptr;
    D3D11CreateDeviceAndSwapChain_Original = nullptr;
    D3D11CreateDevice_Original = nullptr;
    D3D12CreateDevice_Original = nullptr;

    g_api_hooks_installed.store(false);
    LogInfo("API hooks uninstalled successfully");
}
// Game window (atomic): same as SK's game_window.hWnd. Set when we install the WNDPROC hook and when
// we get the swapchain output window. GetGameWindow() returns this. Single source of truth for the
// hooked/game window.
void SetGameWindow(HWND hwnd) { g_last_swapchain_hwnd.store(hwnd, std::memory_order_release); }

}  // namespace display_commanderhooks
