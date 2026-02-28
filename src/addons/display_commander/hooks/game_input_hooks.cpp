#include "game_input_hooks.hpp"
#include "input_activity_stats.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"
#include <MinHook.h>
#include <atomic>

namespace display_commanderhooks {

namespace {

std::atomic<bool> g_game_input_hooks_installed{false};

// GameInputCreate(IGameInput** game_input) - we use void* to avoid depending on GameInput.h
using GameInputCreate_pfn = HRESULT(WINAPI*)(void** game_input);
GameInputCreate_pfn GameInputCreate_Original = nullptr;

HRESULT WINAPI GameInputCreate_Detour(void** game_input) {
    InputActivityStats::GetInstance().MarkActive(InputApiId::GameInput);
    return (GameInputCreate_Original != nullptr)
               ? GameInputCreate_Original(game_input)
               : E_FAIL;
}

}  // namespace

bool InstallGameInputHooks(HMODULE h_module) {
    if (h_module == nullptr) {
        return false;
    }
    if (g_game_input_hooks_installed.load()) {
        LogInfo("GameInput hooks already installed");
        return true;
    }

    FARPROC create_proc = GetProcAddress(h_module, "GameInputCreate");
    if (create_proc == nullptr) {
        LogInfo("GameInputCreate not found in module (GameInput.dll may use a different export)");
        return false;
    }

    if (CreateAndEnableHook(create_proc, reinterpret_cast<LPVOID>(GameInputCreate_Detour),
                            reinterpret_cast<LPVOID*>(&GameInputCreate_Original), "GameInputCreate")) {
        g_game_input_hooks_installed.store(true);
        LogInfo("GameInput (GameInputCreate) hook installed successfully");
        return true;
    }
    LogWarn("Failed to install GameInputCreate hook");
    return false;
}

}  // namespace display_commanderhooks
