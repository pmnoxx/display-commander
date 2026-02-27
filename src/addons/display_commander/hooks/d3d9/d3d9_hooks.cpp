#include "d3d9_hooks.hpp"
#include "../../globals.hpp"
#include "../../utils/logging.hpp"

#include <atomic>

namespace display_commanderhooks::d3d9 {

std::atomic<bool> g_dx9_hooks_installed{false};

bool InstallDX9Hooks(HMODULE hModule) {
    if (g_dx9_hooks_installed.load(std::memory_order_relaxed)) {
        LogInfo("InstallDX9Hooks: D3D9 hooks already installed");
        return true;
    }

    if (g_shutdown.load(std::memory_order_relaxed)) {
        LogInfo("InstallDX9Hooks: shutdown in progress, skipping");
        return false;
    }

    if (hModule == nullptr) {
        LogWarn("InstallDX9Hooks: null module handle, skipping");
        return false;
    }

    LogInfo("InstallDX9Hooks: d3d9.dll loaded (0x%p), D3D9 hook state ready", hModule);
    g_dx9_hooks_installed.store(true, std::memory_order_relaxed);
    return true;
}

void UninstallDX9Hooks() {
    if (!g_dx9_hooks_installed.load(std::memory_order_relaxed)) {
        return;
    }
    g_dx9_hooks_installed.store(false, std::memory_order_relaxed);
    LogInfo("UninstallDX9Hooks: D3D9 hook state cleared");
}

bool AreDX9HooksInstalled() {
    return g_dx9_hooks_installed.load(std::memory_order_relaxed);
}

}  // namespace display_commanderhooks::d3d9
