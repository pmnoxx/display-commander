// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "opengl_hooks.hpp"
#include "../../globals.hpp"
#include "../../performance_types.hpp"
#include "../../swapchain_events.hpp"
#include "../../utils/detour_call_tracker.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../dxgi/dxgi_present_hooks.hpp"
#include "../hook_suppression_manager.hpp"
#include "../present_traffic_tracking.hpp"

// Libraries <standard C++>
#include <atomic>

// Libraries <Windows> (Windows types via opengl_hooks.hpp; MinHook follows)
#include <MinHook.h>

static wglSwapBuffers_pfn wglSwapBuffers_ptr = nullptr;

wglSwapBuffers_pfn wglSwapBuffers_Original = nullptr;

static std::atomic<bool> g_opengl_hooks_installed{false};

BOOL WINAPI wglSwapBuffers_Detour(HDC hdc) {
    const LONGLONG now_ns = utils::get_now_ns();
    display_commanderhooks::g_last_opengl_swapbuffers_time_ns.store(static_cast<uint64_t>(now_ns),
                                                                    std::memory_order_relaxed);
    CALL_GUARD(now_ns);

    ChooseFpsLimiter(static_cast<uint64_t>(now_ns), FpsLimiterCallSite::opengl_swapbuffers);
    bool use_fps_limiter = GetChosenFpsLimiter(FpsLimiterCallSite::opengl_swapbuffers);
    if (use_fps_limiter) {
        OnPresentFlags2(true, false);  // Called from present_detour
        RecordNativeFrameTime();
    }
    if (GetChosenFrameTimeLocation() == FpsLimiterCallSite::opengl_swapbuffers) {
        RecordFrameTime(FrameTimeMode::kPresent);
    }

    BOOL result = wglSwapBuffers_Original(hdc);

    if (use_fps_limiter) {
        display_commanderhooks::dxgi::HandlePresentAfter(false);
    }
    OnPresentUpdateAfter2(false);

    return result;
}

bool InstallOpenGLHooks(HMODULE hModule) {
    if (g_opengl_hooks_installed.load()) {
        LogInfo("OpenGL hooks already installed");
        return true;
    }

    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::OPENGL)) {
        LogInfo("OpenGL hooks installation suppressed by user setting");
        return false;
    }

    if (g_shutdown.load()) {
        LogInfo("OpenGL hooks installation skipped - shutdown in progress");
        return false;
    }

    if (!hModule) {
        LogWarn("InstallOpenGLHooks: null module handle, skipping OpenGL hooks");
        return false;
    }

    LogInfo("Installing OpenGL hooks...");

    wglSwapBuffers_ptr = reinterpret_cast<wglSwapBuffers_pfn>(GetProcAddress(hModule, "wglSwapBuffers"));
    if (!wglSwapBuffers_ptr) {
        LogError("Failed to load wglSwapBuffers from opengl32.dll");
        return false;
    }

    if (!CreateAndEnableHook(wglSwapBuffers_ptr, wglSwapBuffers_Detour, (LPVOID*)&wglSwapBuffers_Original,
                             "wglSwapBuffers")) {
        LogError("Failed to create and enable wglSwapBuffers hook");
        return false;
    }

    g_opengl_hooks_installed.store(true);
    LogInfo("OpenGL hooks installed successfully");

    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
        display_commanderhooks::HookType::OPENGL);

    return true;
}

void UninstallOpenGLHooks() {
    if (!g_opengl_hooks_installed.load()) {
        LogInfo("OpenGL hooks not installed");
        return;
    }

    LogInfo("Uninstalling OpenGL hooks...");

    if (wglSwapBuffers_ptr) {
        MH_DisableHook(wglSwapBuffers_ptr);
        MH_RemoveHook(wglSwapBuffers_ptr);
    }

    wglSwapBuffers_Original = nullptr;
    wglSwapBuffers_ptr = nullptr;

    g_opengl_hooks_installed.store(false);
    LogInfo("OpenGL hooks uninstalled successfully");
}
