#include "winmm_joystick_hooks.hpp"
#include "windows_hooks/windows_message_hooks.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"
#include "hook_suppression_manager.hpp"
#include <MinHook.h>
#include <atomic>

// Minimal WinMM joystick types to avoid linking winmm.lib (project loads winmm dynamically).
// Match JOYINFO / JOYINFOEX layout; use local constant names to avoid Windows SDK macros.
namespace {
using WinMM_MMRESULT = UINT;
constexpr WinMM_MMRESULT kWinMM_MMSYSERR_INVALPARAM = 11;
constexpr WinMM_MMRESULT kWinMM_JOYERR_PARMS = 165;

struct JOYINFO_LOCAL {
    UINT wXpos;
    UINT wYpos;
    UINT wZpos;
    UINT wButtons;
};

struct JOYINFOEX_LOCAL {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwXpos;
    DWORD dwYpos;
    DWORD dwZpos;
    DWORD dwRpos;
    DWORD dwUpos;
    DWORD dwVpos;
    DWORD dwButtons;
    DWORD dwButtonNumber;
    DWORD dwPOV;
    DWORD dwReserved1;
    DWORD dwReserved2;
};

using joyGetPos_pfn = WinMM_MMRESULT(WINAPI*)(UINT uJoyID, JOYINFO_LOCAL* pji);
using joyGetPosEx_pfn = WinMM_MMRESULT(WINAPI*)(UINT uJoyID, JOYINFOEX_LOCAL* pji);
}  // namespace

namespace display_commanderhooks {

static std::atomic<bool> g_winmm_joystick_hooks_installed{false};

joyGetPos_pfn joyGetPos_Original = nullptr;
joyGetPosEx_pfn joyGetPosEx_Original = nullptr;

static WinMM_MMRESULT WINAPI joyGetPos_Detour(UINT uJoyID, JOYINFO_LOCAL* pji) {
    g_hook_stats[HOOK_joyGetPos].increment_total();
    display_commanderhooks::UpdateHookLastCallTime(HOOK_joyGetPos);
    if (pji == nullptr) {
        return kWinMM_MMSYSERR_INVALPARAM;
    }
    if (joyGetPos_Original) {
        WinMM_MMRESULT ret = joyGetPos_Original(uJoyID, pji);
        g_hook_stats[HOOK_joyGetPos].increment_unsuppressed();
        return ret;
    }
    return kWinMM_JOYERR_PARMS;
}

static WinMM_MMRESULT WINAPI joyGetPosEx_Detour(UINT uJoyID, JOYINFOEX_LOCAL* pji) {
    g_hook_stats[HOOK_joyGetPosEx].increment_total();
    display_commanderhooks::UpdateHookLastCallTime(HOOK_joyGetPosEx);
    if (pji == nullptr) {
        return kWinMM_MMSYSERR_INVALPARAM;
    }
    if (joyGetPosEx_Original) {
        WinMM_MMRESULT ret = joyGetPosEx_Original(uJoyID, pji);
        g_hook_stats[HOOK_joyGetPosEx].increment_unsuppressed();
        return ret;
    }
    return kWinMM_JOYERR_PARMS;
}

bool InstallWinMMJoystickHooks(HMODULE hWinMM) {
    if (hWinMM == nullptr) {
        return false;
    }
    if (g_winmm_joystick_hooks_installed.load()) {
        LogInfo("WinMM joystick hooks already installed");
        return true;
    }

    if (HookSuppressionManager::GetInstance().ShouldSuppressHook(HookType::WINMM_JOYSTICK)) {
        LogInfo("WinMM joystick hooks installation suppressed by user setting");
        return false;
    }

    MH_STATUS init_status = SafeInitializeMinHook(HookType::WINMM_JOYSTICK);
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for WinMM joystick hooks - Status: %d", init_status);
        return false;
    }

    LogInfo("To suppress WinMM joystick hooks, set WinMMJoystickHooks=1 in [DisplayCommander.HookSuppression] section of DisplayCommander.toml");

    int installed = 0;
    if (CreateAndEnableHookFromModule(hWinMM, "joyGetPos", reinterpret_cast<LPVOID>(joyGetPos_Detour),
                                      reinterpret_cast<LPVOID*>(&joyGetPos_Original), "joyGetPos")) {
        installed++;
    } else {
        LogWarn("Failed to install joyGetPos hook");
    }
    if (CreateAndEnableHookFromModule(hWinMM, "joyGetPosEx", reinterpret_cast<LPVOID>(joyGetPosEx_Detour),
                                      reinterpret_cast<LPVOID*>(&joyGetPosEx_Original), "joyGetPosEx")) {
        installed++;
    } else {
        LogWarn("Failed to install joyGetPosEx hook");
    }

    if (installed > 0) {
        g_winmm_joystick_hooks_installed.store(true);
        HookSuppressionManager::GetInstance().MarkHookInstalled(HookType::WINMM_JOYSTICK);
        LogInfo("WinMM joystick hooks installed: %d hooks", installed);
        return true;
    }
    return false;
}

}  // namespace display_commanderhooks
