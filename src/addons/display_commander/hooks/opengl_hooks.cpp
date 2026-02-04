#include "opengl_hooks.hpp"
#include <MinHook.h>
#include <windows.h>
#include <wingdi.h>
#include <array>
#include "../globals.hpp"
#include "../gpu_completion_monitoring.hpp"
#include "../performance_types.hpp"
#include "../swapchain_events.hpp"
#include "../utils.hpp"
#include "../utils/detour_call_tracker.hpp"
#include "../utils/logging.hpp"
#include "../utils/timing.hpp"
#include "hook_suppression_manager.hpp"

// WGL function pointers (dynamically loaded)
static wglSwapBuffers_pfn wglSwapBuffers_ptr = nullptr;
static wglMakeCurrent_pfn wglMakeCurrent_ptr = nullptr;
static wglCreateContext_pfn wglCreateContext_ptr = nullptr;
static wglDeleteContext_pfn wglDeleteContext_ptr = nullptr;
static wglChoosePixelFormat_pfn wglChoosePixelFormat_ptr = nullptr;
static wglSetPixelFormat_pfn wglSetPixelFormat_ptr = nullptr;
static wglGetPixelFormat_pfn wglGetPixelFormat_ptr = nullptr;
static wglDescribePixelFormat_pfn wglDescribePixelFormat_ptr = nullptr;
static wglGetProcAddress_pfn wglGetProcAddress_ptr = nullptr;

// Original function pointers
wglSwapBuffers_pfn wglSwapBuffers_Original = nullptr;
wglMakeCurrent_pfn wglMakeCurrent_Original = nullptr;
wglCreateContext_pfn wglCreateContext_Original = nullptr;
wglDeleteContext_pfn wglDeleteContext_Original = nullptr;
wglChoosePixelFormat_pfn wglChoosePixelFormat_Original = nullptr;
wglSetPixelFormat_pfn wglSetPixelFormat_Original = nullptr;
wglGetPixelFormat_pfn wglGetPixelFormat_Original = nullptr;
wglDescribePixelFormat_pfn wglDescribePixelFormat_Original = nullptr;
wglCreateContextAttribsARB_pfn wglCreateContextAttribsARB_Original = nullptr;
wglChoosePixelFormatARB_pfn wglChoosePixelFormatARB_Original = nullptr;
wglGetPixelFormatAttribivARB_pfn wglGetPixelFormatAttribivARB_Original = nullptr;
wglGetPixelFormatAttribfvARB_pfn wglGetPixelFormatAttribfvARB_Original = nullptr;
wglGetProcAddress_pfn wglGetProcAddress_Original = nullptr;
wglSwapIntervalEXT_pfn wglSwapIntervalEXT_Original = nullptr;
wglGetSwapIntervalEXT_pfn wglGetSwapIntervalEXT_Original = nullptr;

// Hook installation state
static std::atomic<bool> g_opengl_hooks_installed{false};

// Hook detour functions
BOOL WINAPI wglSwapBuffers_Detour(HDC hdc) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    g_opengl_hook_counters[OPENGL_HOOK_WGL_SWAPBUFFERS].fetch_add(1);
    g_opengl_hook_total_count.fetch_add(1);

    // Call OnPresentFlags2 with flags = 0 (no flags for OpenGL)
    OnPresentFlags2(true);  // Called from present_detour

    RecordNativeFrameTime();

    // Record per-frame FPS sample for background aggregation
    // RecordFrameTime(FrameTimeMode::kPresent);

    // Call original function
    BOOL result = wglSwapBuffers_Original(hdc);

    // Handle GPU completion for OpenGL (assumes immediate completion)
    HandleOpenGLGPUCompletion();

    // Call OnPresentUpdateAfter2 after the present
    OnPresentUpdateAfter2(false);

    return result;
}

BOOL WINAPI wglMakeCurrent_Detour(HDC hdc, HGLRC hglrc) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    g_opengl_hook_counters[OPENGL_HOOK_WGL_MAKECURRENT].fetch_add(1);
    g_opengl_hook_total_count.fetch_add(1);
    return wglMakeCurrent_Original(hdc, hglrc);
}

HGLRC WINAPI wglCreateContext_Detour(HDC hdc) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    g_opengl_hook_counters[OPENGL_HOOK_WGL_CREATECONTEXT].fetch_add(1);
    g_opengl_hook_total_count.fetch_add(1);
    return wglCreateContext_Original(hdc);
}

BOOL WINAPI wglDeleteContext_Detour(HGLRC hglrc) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    g_opengl_hook_counters[OPENGL_HOOK_WGL_DELETECONTEXT].fetch_add(1);
    g_opengl_hook_total_count.fetch_add(1);
    return wglDeleteContext_Original(hglrc);
}

int WINAPI wglChoosePixelFormat_Detour(HDC hdc, const PIXELFORMATDESCRIPTOR* ppfd) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    g_opengl_hook_counters[OPENGL_HOOK_WGL_CHOOSEPIXELFORMAT].fetch_add(1);
    g_opengl_hook_total_count.fetch_add(1);
    return wglChoosePixelFormat_Original(hdc, ppfd);
}

BOOL WINAPI wglSetPixelFormat_Detour(HDC hdc, int iPixelFormat, const PIXELFORMATDESCRIPTOR* ppfd) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    g_opengl_hook_counters[OPENGL_HOOK_WGL_SETPIXELFORMAT].fetch_add(1);
    g_opengl_hook_total_count.fetch_add(1);
    return wglSetPixelFormat_Original(hdc, iPixelFormat, ppfd);
}

int WINAPI wglGetPixelFormat_Detour(HDC hdc) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    g_opengl_hook_counters[OPENGL_HOOK_WGL_GETPIXELFORMAT].fetch_add(1);
    g_opengl_hook_total_count.fetch_add(1);
    return wglGetPixelFormat_Original(hdc);
}

BOOL WINAPI wglDescribePixelFormat_Detour(HDC hdc, int iPixelFormat, UINT nBytes, LPPIXELFORMATDESCRIPTOR ppfd) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    g_opengl_hook_counters[OPENGL_HOOK_WGL_DESCRIBEPIXELFORMAT].fetch_add(1);
    g_opengl_hook_total_count.fetch_add(1);
    return wglDescribePixelFormat_Original(hdc, iPixelFormat, nBytes, ppfd);
}

HGLRC WINAPI wglCreateContextAttribsARB_Detour(HDC hdc, HGLRC hshareContext, const int* attribList) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    g_opengl_hook_counters[OPENGL_HOOK_WGL_CREATECONTEXTATTRIBSARB].fetch_add(1);
    g_opengl_hook_total_count.fetch_add(1);
    return wglCreateContextAttribsARB_Original(hdc, hshareContext, attribList);
}

BOOL WINAPI wglChoosePixelFormatARB_Detour(HDC hdc, const int* piAttribIList, const FLOAT* pfAttribFList,
                                           UINT nMaxFormats, int* piFormats, UINT* nNumFormats) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    g_opengl_hook_counters[OPENGL_HOOK_WGL_CHOOSEPIXELFORMATARB].fetch_add(1);
    g_opengl_hook_total_count.fetch_add(1);
    return wglChoosePixelFormatARB_Original(hdc, piAttribIList, pfAttribFList, nMaxFormats, piFormats, nNumFormats);
}

BOOL WINAPI wglGetPixelFormatAttribivARB_Detour(HDC hdc, int iPixelFormat, int iLayerPlane, UINT nAttributes,
                                                const int* piAttributes, int* piValues) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    g_opengl_hook_counters[OPENGL_HOOK_WGL_GETPIXELFORMATATTRIBIVARB].fetch_add(1);
    g_opengl_hook_total_count.fetch_add(1);
    return wglGetPixelFormatAttribivARB_Original(hdc, iPixelFormat, iLayerPlane, nAttributes, piAttributes, piValues);
}

BOOL WINAPI wglGetPixelFormatAttribfvARB_Detour(HDC hdc, int iPixelFormat, int iLayerPlane, UINT nAttributes,
                                                const int* piAttributes, FLOAT* pfValues) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    g_opengl_hook_counters[OPENGL_HOOK_WGL_GETPIXELFORMATATTRIBFVARB].fetch_add(1);
    g_opengl_hook_total_count.fetch_add(1);
    return wglGetPixelFormatAttribfvARB_Original(hdc, iPixelFormat, iLayerPlane, nAttributes, piAttributes, pfValues);
}

PROC WINAPI wglGetProcAddress_Detour(LPCSTR lpszProc) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    g_opengl_hook_counters[OPENGL_HOOK_WGL_GETPROCADDRESS].fetch_add(1);
    g_opengl_hook_total_count.fetch_add(1);
    return wglGetProcAddress_Original(lpszProc);
}

BOOL WINAPI wglSwapIntervalEXT_Detour(int interval) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    g_opengl_hook_counters[OPENGL_HOOK_WGL_SWAPINTERVALEXT].fetch_add(1);
    g_opengl_hook_total_count.fetch_add(1);
    return wglSwapIntervalEXT_Original(interval);
}

int WINAPI wglGetSwapIntervalEXT_Detour(void) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    g_opengl_hook_counters[OPENGL_HOOK_WGL_GETSWAPINTERVALEXT].fetch_add(1);
    g_opengl_hook_total_count.fetch_add(1);
    return wglGetSwapIntervalEXT_Original();
}

// Hook installation function
bool InstallOpenGLHooks() {
    if (g_opengl_hooks_installed.load()) {
        LogInfo("OpenGL hooks already installed");
        return true;
    }

    // Check if OpenGL hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::OPENGL)) {
        LogInfo("OpenGL hooks installation suppressed by user setting");
        return false;
    }

    if (g_shutdown.load()) {
        LogInfo("OpenGL hooks installation skipped - shutdown in progress");
        return false;
    }

    LogInfo("Installing OpenGL hooks...");

    // Get OpenGL module handle
    HMODULE opengl_module = GetModuleHandleW(L"opengl32.dll");
    if (!opengl_module) {
        LogWarn("opengl32.dll not loaded, skipping OpenGL hooks");
        return false;
    }

    // Dynamically load WGL functions
    wglSwapBuffers_ptr = reinterpret_cast<wglSwapBuffers_pfn>(GetProcAddress(opengl_module, "wglSwapBuffers"));
    wglMakeCurrent_ptr = reinterpret_cast<wglMakeCurrent_pfn>(GetProcAddress(opengl_module, "wglMakeCurrent"));
    wglCreateContext_ptr = reinterpret_cast<wglCreateContext_pfn>(GetProcAddress(opengl_module, "wglCreateContext"));
    wglDeleteContext_ptr = reinterpret_cast<wglDeleteContext_pfn>(GetProcAddress(opengl_module, "wglDeleteContext"));
    wglChoosePixelFormat_ptr =
        reinterpret_cast<wglChoosePixelFormat_pfn>(GetProcAddress(opengl_module, "wglChoosePixelFormat"));
    wglSetPixelFormat_ptr = reinterpret_cast<wglSetPixelFormat_pfn>(GetProcAddress(opengl_module, "wglSetPixelFormat"));
    wglGetPixelFormat_ptr = reinterpret_cast<wglGetPixelFormat_pfn>(GetProcAddress(opengl_module, "wglGetPixelFormat"));
    wglDescribePixelFormat_ptr =
        reinterpret_cast<wglDescribePixelFormat_pfn>(GetProcAddress(opengl_module, "wglDescribePixelFormat"));
    wglGetProcAddress_ptr = reinterpret_cast<wglGetProcAddress_pfn>(GetProcAddress(opengl_module, "wglGetProcAddress"));

    // Check if all required functions were loaded
    if (!wglSwapBuffers_ptr || !wglMakeCurrent_ptr || !wglCreateContext_ptr || !wglDeleteContext_ptr
        || !wglChoosePixelFormat_ptr || !wglSetPixelFormat_ptr || !wglGetPixelFormat_ptr || !wglDescribePixelFormat_ptr
        || !wglGetProcAddress_ptr) {
        LogError("Failed to load required WGL functions from opengl32.dll");
        return false;
    }

    // Hook wglSwapBuffers
    if (!CreateAndEnableHook(wglSwapBuffers_ptr, wglSwapBuffers_Detour, (LPVOID*)&wglSwapBuffers_Original,
                             "wglSwapBuffers")) {
        LogError("Failed to create and enable wglSwapBuffers hook");
        return false;
    }

    // Hook wglMakeCurrent
    if (!CreateAndEnableHook(wglMakeCurrent_ptr, wglMakeCurrent_Detour, (LPVOID*)&wglMakeCurrent_Original,
                             "wglMakeCurrent")) {
        LogError("Failed to create and enable wglMakeCurrent hook");
        return false;
    }

    // Hook wglCreateContext
    if (!CreateAndEnableHook(wglCreateContext_ptr, wglCreateContext_Detour, (LPVOID*)&wglCreateContext_Original,
                             "wglCreateContext")) {
        LogError("Failed to create and enable wglCreateContext hook");
        return false;
    }

    // Hook wglDeleteContext
    if (!CreateAndEnableHook(wglDeleteContext_ptr, wglDeleteContext_Detour, (LPVOID*)&wglDeleteContext_Original,
                             "wglDeleteContext")) {
        LogError("Failed to create and enable wglDeleteContext hook");
        return false;
    }

    // Hook wglChoosePixelFormat
    if (!CreateAndEnableHook(wglChoosePixelFormat_ptr, wglChoosePixelFormat_Detour,
                             (LPVOID*)&wglChoosePixelFormat_Original, "wglChoosePixelFormat")) {
        LogError("Failed to create and enable wglChoosePixelFormat hook");
        return false;
    }

    // Hook wglSetPixelFormat
    if (!CreateAndEnableHook(wglSetPixelFormat_ptr, wglSetPixelFormat_Detour, (LPVOID*)&wglSetPixelFormat_Original,
                             "wglSetPixelFormat")) {
        LogError("Failed to create and enable wglSetPixelFormat hook");
        return false;
    }

    // Hook wglGetPixelFormat
    if (!CreateAndEnableHook(wglGetPixelFormat_ptr, wglGetPixelFormat_Detour, (LPVOID*)&wglGetPixelFormat_Original,
                             "wglGetPixelFormat")) {
        LogError("Failed to create and enable wglGetPixelFormat hook");
        return false;
    }

    // Hook wglDescribePixelFormat
    if (!CreateAndEnableHook(wglDescribePixelFormat_ptr, wglDescribePixelFormat_Detour,
                             (LPVOID*)&wglDescribePixelFormat_Original, "wglDescribePixelFormat")) {
        LogError("Failed to create and enable wglDescribePixelFormat hook");
        return false;
    }

    // Hook wglGetProcAddress
    if (!CreateAndEnableHook(wglGetProcAddress_ptr, wglGetProcAddress_Detour, (LPVOID*)&wglGetProcAddress_Original,
                             "wglGetProcAddress")) {
        LogError("Failed to create and enable wglGetProcAddress hook");
        return false;
    }

    // Hook extension functions (these may not be available on all systems)
    // wglCreateContextAttribsARB
    auto wglCreateContextAttribsARB_sys =
        reinterpret_cast<wglCreateContextAttribsARB_pfn>(wglGetProcAddress_ptr("wglCreateContextAttribsARB"));
    if (wglCreateContextAttribsARB_sys) {
        if (!CreateAndEnableHook(wglCreateContextAttribsARB_sys, wglCreateContextAttribsARB_Detour,
                                 (LPVOID*)&wglCreateContextAttribsARB_Original, "wglCreateContextAttribsARB")) {
            LogWarn("Failed to create and enable wglCreateContextAttribsARB hook");
        }
    } else {
        LogInfo("wglCreateContextAttribsARB not available");
    }

    // wglChoosePixelFormatARB
    auto wglChoosePixelFormatARB_sys =
        reinterpret_cast<wglChoosePixelFormatARB_pfn>(wglGetProcAddress_ptr("wglChoosePixelFormatARB"));
    if (wglChoosePixelFormatARB_sys) {
        if (!CreateAndEnableHook(wglChoosePixelFormatARB_sys, wglChoosePixelFormatARB_Detour,
                                 (LPVOID*)&wglChoosePixelFormatARB_Original, "wglChoosePixelFormatARB")) {
            LogWarn("Failed to create and enable wglChoosePixelFormatARB hook");
        }
    } else {
        LogInfo("wglChoosePixelFormatARB not available");
    }

    // wglGetPixelFormatAttribivARB
    auto wglGetPixelFormatAttribivARB_sys =
        reinterpret_cast<wglGetPixelFormatAttribivARB_pfn>(wglGetProcAddress_ptr("wglGetPixelFormatAttribivARB"));
    if (wglGetPixelFormatAttribivARB_sys) {
        if (!CreateAndEnableHook(wglGetPixelFormatAttribivARB_sys, wglGetPixelFormatAttribivARB_Detour,
                                 (LPVOID*)&wglGetPixelFormatAttribivARB_Original, "wglGetPixelFormatAttribivARB")) {
            LogWarn("Failed to create and enable wglGetPixelFormatAttribivARB hook");
        }
    } else {
        LogInfo("wglGetPixelFormatAttribivARB not available");
    }

    // wglGetPixelFormatAttribfvARB
    auto wglGetPixelFormatAttribfvARB_sys =
        reinterpret_cast<wglGetPixelFormatAttribfvARB_pfn>(wglGetProcAddress_ptr("wglGetPixelFormatAttribfvARB"));
    if (wglGetPixelFormatAttribfvARB_sys) {
        if (!CreateAndEnableHook(wglGetPixelFormatAttribfvARB_sys, wglGetPixelFormatAttribfvARB_Detour,
                                 (LPVOID*)&wglGetPixelFormatAttribfvARB_Original, "wglGetPixelFormatAttribfvARB")) {
            LogWarn("Failed to create and enable wglGetPixelFormatAttribfvARB hook");
        }
    } else {
        LogInfo("wglGetPixelFormatAttribfvARB not available");
    }

    // wglSwapIntervalEXT
    auto wglSwapIntervalEXT_sys = reinterpret_cast<wglSwapIntervalEXT_pfn>(wglGetProcAddress_ptr("wglSwapIntervalEXT"));
    if (wglSwapIntervalEXT_sys) {
        if (!CreateAndEnableHook(wglSwapIntervalEXT_sys, wglSwapIntervalEXT_Detour,
                                 (LPVOID*)&wglSwapIntervalEXT_Original, "wglSwapIntervalEXT")) {
            LogWarn("Failed to create and enable wglSwapIntervalEXT hook");
        }
    } else {
        LogInfo("wglSwapIntervalEXT not available");
    }

    // wglGetSwapIntervalEXT
    auto wglGetSwapIntervalEXT_sys =
        reinterpret_cast<wglGetSwapIntervalEXT_pfn>(wglGetProcAddress_ptr("wglGetSwapIntervalEXT"));
    if (wglGetSwapIntervalEXT_sys) {
        if (!CreateAndEnableHook(wglGetSwapIntervalEXT_sys, wglGetSwapIntervalEXT_Detour,
                                 (LPVOID*)&wglGetSwapIntervalEXT_Original, "wglGetSwapIntervalEXT")) {
            LogWarn("Failed to create and enable wglGetSwapIntervalEXT hook");
        }
    } else {
        LogInfo("wglGetSwapIntervalEXT not available");
    }

    g_opengl_hooks_installed.store(true);
    LogInfo("OpenGL hooks installed successfully");

    // Mark OpenGL hooks as installed
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
        display_commanderhooks::HookType::OPENGL);

    return true;
}

// Hook uninstallation function
void UninstallOpenGLHooks() {
    if (!g_opengl_hooks_installed.load()) {
        LogInfo("OpenGL hooks not installed");
        return;
    }

    LogInfo("Uninstalling OpenGL hooks...");

    // Disable and remove hooks
    if (wglSwapBuffers_ptr) MH_DisableHook(wglSwapBuffers_ptr);
    if (wglMakeCurrent_ptr) MH_DisableHook(wglMakeCurrent_ptr);
    if (wglCreateContext_ptr) MH_DisableHook(wglCreateContext_ptr);
    if (wglDeleteContext_ptr) MH_DisableHook(wglDeleteContext_ptr);
    if (wglChoosePixelFormat_ptr) MH_DisableHook(wglChoosePixelFormat_ptr);
    if (wglSetPixelFormat_ptr) MH_DisableHook(wglSetPixelFormat_ptr);
    if (wglGetPixelFormat_ptr) MH_DisableHook(wglGetPixelFormat_ptr);
    if (wglDescribePixelFormat_ptr) MH_DisableHook(wglDescribePixelFormat_ptr);
    if (wglGetProcAddress_ptr) MH_DisableHook(wglGetProcAddress_ptr);

    // Remove hooks
    if (wglSwapBuffers_ptr) MH_RemoveHook(wglSwapBuffers_ptr);
    if (wglMakeCurrent_ptr) MH_RemoveHook(wglMakeCurrent_ptr);
    if (wglCreateContext_ptr) MH_RemoveHook(wglCreateContext_ptr);
    if (wglDeleteContext_ptr) MH_RemoveHook(wglDeleteContext_ptr);
    if (wglChoosePixelFormat_ptr) MH_RemoveHook(wglChoosePixelFormat_ptr);
    if (wglSetPixelFormat_ptr) MH_RemoveHook(wglSetPixelFormat_ptr);
    if (wglGetPixelFormat_ptr) MH_RemoveHook(wglGetPixelFormat_ptr);
    if (wglDescribePixelFormat_ptr) MH_RemoveHook(wglDescribePixelFormat_ptr);
    if (wglGetProcAddress_ptr) MH_RemoveHook(wglGetProcAddress_ptr);

    // Clean up extension hooks if they were installed
    if (wglCreateContextAttribsARB_Original) {
        MH_DisableHook(wglCreateContextAttribsARB_Original);
        MH_RemoveHook(wglCreateContextAttribsARB_Original);
    }
    if (wglChoosePixelFormatARB_Original) {
        MH_DisableHook(wglChoosePixelFormatARB_Original);
        MH_RemoveHook(wglChoosePixelFormatARB_Original);
    }
    if (wglGetPixelFormatAttribivARB_Original) {
        MH_DisableHook(wglGetPixelFormatAttribivARB_Original);
        MH_RemoveHook(wglGetPixelFormatAttribivARB_Original);
    }
    if (wglGetPixelFormatAttribfvARB_Original) {
        MH_DisableHook(wglGetPixelFormatAttribfvARB_Original);
        MH_RemoveHook(wglGetPixelFormatAttribfvARB_Original);
    }
    if (wglSwapIntervalEXT_Original) {
        MH_DisableHook(wglSwapIntervalEXT_Original);
        MH_RemoveHook(wglSwapIntervalEXT_Original);
    }
    if (wglGetSwapIntervalEXT_Original) {
        MH_DisableHook(wglGetSwapIntervalEXT_Original);
        MH_RemoveHook(wglGetSwapIntervalEXT_Original);
    }

    // Reset function pointers
    wglSwapBuffers_Original = nullptr;
    wglMakeCurrent_Original = nullptr;
    wglCreateContext_Original = nullptr;
    wglDeleteContext_Original = nullptr;
    wglChoosePixelFormat_Original = nullptr;
    wglSetPixelFormat_Original = nullptr;
    wglGetPixelFormat_Original = nullptr;
    wglDescribePixelFormat_Original = nullptr;
    wglCreateContextAttribsARB_Original = nullptr;
    wglChoosePixelFormatARB_Original = nullptr;
    wglGetPixelFormatAttribivARB_Original = nullptr;
    wglGetPixelFormatAttribfvARB_Original = nullptr;
    wglGetProcAddress_Original = nullptr;
    wglSwapIntervalEXT_Original = nullptr;
    wglGetSwapIntervalEXT_Original = nullptr;

    // Reset dynamically loaded function pointers
    wglSwapBuffers_ptr = nullptr;
    wglMakeCurrent_ptr = nullptr;
    wglCreateContext_ptr = nullptr;
    wglDeleteContext_ptr = nullptr;
    wglChoosePixelFormat_ptr = nullptr;
    wglSetPixelFormat_ptr = nullptr;
    wglGetPixelFormat_ptr = nullptr;
    wglDescribePixelFormat_ptr = nullptr;
    wglGetProcAddress_ptr = nullptr;

    g_opengl_hooks_installed.store(false);
    LogInfo("OpenGL hooks uninstalled successfully");
}

// Check if hooks are installed
bool AreOpenGLHooksInstalled() { return g_opengl_hooks_installed.load(); }
