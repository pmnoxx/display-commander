#include "api_hooks.hpp"
#include <MinHook.h>
#include <cwchar>
#include "../../process_exit_hooks.hpp"
#include "../../settings/advanced_tab_settings.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../utils/detour_call_tracker.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/timing.hpp"
#include "../system/debug_output_hooks.hpp"
#include "../input/dinput_hooks.hpp"
#include "../system/display_settings_hooks.hpp"
#include "dpi_hooks.hpp"
#include "../../globals.hpp"
#include "../hook_suppression_manager.hpp"
#include "../loadlibrary_hooks.hpp"
#include "../opengl/opengl_hooks.hpp"
#include "../nvidia/pclstats_etw_hooks.hpp"
#include "../system/timeslowdown_hooks.hpp"
#include "../input/windows_gaming_input_hooks.hpp"
#include "windows_message_hooks.hpp"

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
    CALL_GUARD_NO_TS();
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
bool IsIconic_direct(HWND hwnd) {
    CALL_GUARD_NO_TS();
    return (IsIconic_Original ? IsIconic_Original(hwnd) : IsIconic(hwnd)) != FALSE;
}

// True visibility state, bypassing our IsWindowVisible detour (used when code needs real visibility).
bool IsWindowVisible_direct(HWND hwnd) {
    CALL_GUARD_NO_TS();
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
    CALL_GUARD_NO_TS();
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
    if (settings::g_mainTabSettings.window_mode.GetValue() != static_cast<int>(WindowMode::kNoChanges)
        && hWnd == g_last_swapchain_hwnd.load()) {
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

    if (settings::g_mainTabSettings.window_mode.GetValue() != static_cast<int>(WindowMode::kNoChanges)
        && hWnd == g_last_swapchain_hwnd.load()) {
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
    if (settings::g_mainTabSettings.window_mode.GetValue() != static_cast<int>(WindowMode::kNoChanges)
        && hWnd == g_last_swapchain_hwnd.load()) {
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

    if (settings::g_mainTabSettings.window_mode.GetValue() != static_cast<int>(WindowMode::kNoChanges)
        && hWnd == g_last_swapchain_hwnd.load()) {
        ModifyWindowStyle(nIndex, dwNewLong, settings::g_advancedTabSettings.prevent_always_on_top.GetValue());
    }

    g_hook_stats[HOOK_SetWindowLongPtrA].increment_unsuppressed();
    return SetWindowLongPtrA_Original ? SetWindowLongPtrA_Original(hWnd, nIndex, dwNewLong)
                                      : SetWindowLongPtrA(hWnd, nIndex, dwNewLong);
}

// Hooked SetWindowPos function
BOOL WINAPI SetWindowPos_Detour(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags) {
    CALL_GUARD(utils::get_now_ns());
    g_hook_stats[HOOK_SetWindowPos].increment_total();
    // Only process if prevent_always_on_top is enabled
    if (settings::g_mainTabSettings.window_mode.GetValue() != static_cast<int>(WindowMode::kNoChanges)
        && hWnd == g_last_swapchain_hwnd.load() && settings::g_advancedTabSettings.prevent_always_on_top.GetValue()
        && hWndInsertAfter != HWND_NOTOPMOST) {
        hWndInsertAfter = HWND_NOTOPMOST;
    }

    g_hook_stats[HOOK_SetWindowPos].increment_unsuppressed();
    // Call original function with unmodified parameters
    return SetWindowPos_Original ? SetWindowPos_Original(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags)
                                 : SetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

BOOL WINAPI SetWindowPos_Direct(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags) {
    CALL_GUARD_NO_TS();
    return SetWindowPos_Original ? SetWindowPos_Original(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags)
                                 : SetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

HWND WINAPI CreateWindowW_Direct(LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth,
                                 int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam) {
    CALL_GUARD_NO_TS();
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
    CALL_GUARD_NO_TS();
    // Call original function
    return SetCursor_Original ? SetCursor_Original(hCursor) : SetCursor(hCursor);
}

// Hooked SetCursor function
HCURSOR WINAPI SetCursor_Detour(HCURSOR hCursor) {
    CALL_GUARD(utils::get_now_ns());
    return SetCursor_Direct(hCursor);
}

int WINAPI ShowCursor_Direct(BOOL bShow) {
    CALL_GUARD_NO_TS();
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

static std::atomic<bool> installed_dc_addvectoredexceptionhandler_hook{false};

// Hooked AddVectoredExceptionHandler function
PVOID WINAPI AddVectoredExceptionHandler_Detour(ULONG First, PVECTORED_EXCEPTION_HANDLER Handler) {
    CALL_GUARD(utils::get_now_ns());
    // Log the call for debugging
    LogDebug("AddVectoredExceptionHandler_Detour: First=%lu, Handler=0x%p", First, Handler);
    if (installed_dc_addvectoredexceptionhandler_hook) {
        // return error
        //    return nullptr;
    }

    // replace with our own handler

    // Call original function
    auto result = AddVectoredExceptionHandler_Original ? AddVectoredExceptionHandler_Original(First, Handler)
                                                       : AddVectoredExceptionHandler(First, Handler);

    // Note: if this causes issues with anti-check disable
    // For example add detection, and then don't call.
    AddVectoredExceptionHandler_Original
        ? AddVectoredExceptionHandler_Original(First, &process_exit_hooks::VectoredExceptionHandler)
        : AddVectoredExceptionHandler(First, &process_exit_hooks::VectoredExceptionHandler);

    return result;
}

PVOID AddVectoredExceptionHandler_Direct(ULONG First, PVECTORED_EXCEPTION_HANDLER Handler) {
    CALL_GUARD_NO_TS();
    installed_dc_addvectoredexceptionhandler_hook.store(true, std::memory_order_release);

    return AddVectoredExceptionHandler_Original ? AddVectoredExceptionHandler_Original(First, Handler)
                                                : AddVectoredExceptionHandler(First, Handler);
}

HRESULT CreateDXGIFactory1_Direct(REFIID riid, void** ppFactory) {
    CALL_GUARD_NO_TS();
    if (ppFactory == nullptr) {
        return E_POINTER;
    }
    HMODULE dxgi_module = GetModuleHandleW(L"dxgi.dll");
    if (dxgi_module == nullptr) {
        WCHAR path[MAX_PATH];
        if (GetSystemDirectoryW(path, MAX_PATH) == 0) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        size_t len = std::wcslen(path);
        if (len + 11 >= MAX_PATH) {
            return E_FAIL;
        }
        wcscat_s(path, MAX_PATH, L"\\dxgi.dll");
        dxgi_module = LoadLibraryW(path);
    }
    if (dxgi_module == nullptr) {
        return E_FAIL;
    }
    using PFN_CreateDXGIFactory1 = HRESULT(WINAPI*)(REFIID, void**);
    auto pfn = reinterpret_cast<PFN_CreateDXGIFactory1>(GetProcAddress(dxgi_module, "CreateDXGIFactory1"));
    if (pfn == nullptr) {
        return E_FAIL;
    }
    return pfn(riid, ppFactory);
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

    process_exit_hooks::Initialize();

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

    // Uninstall timeslowdown hooks
    UninstallTimeslowdownHooks();

    // Uninstall process exit hooks
    process_exit_hooks::Shutdown();

    // Uninstall debug output hooks
    debug_output::UninstallDebugOutputHooks();

    // Uninstall PCLStats ETW hooks (installed via OnModuleLoaded when advapi32.dll loaded)
    UninstallPCLStatsEtwHooks();

    // Uninstall DPI hooks
    display_commanderhooks::dpi::UninstallDpiHooks();

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

    g_api_hooks_installed.store(false);
    LogInfo("API hooks uninstalled successfully");
}
// Game window (atomic): same as SK's game_window.hWnd. Set when we install the WNDPROC hook and when
// we get the swapchain output window. GetGameWindow() returns this. Single source of truth for the
// hooked/game window.
void SetGameWindow(HWND hwnd) { g_last_swapchain_hwnd.store(hwnd, std::memory_order_release); }

}  // namespace display_commanderhooks
