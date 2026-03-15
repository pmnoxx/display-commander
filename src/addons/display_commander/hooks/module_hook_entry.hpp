// Source Code <Display Commander>
// Shared hook table entry for module-based detours (GetProcAddress + MinHook).
// Used by: api_hooks (user32/kernel32), vulkan_loader_hooks, streamline_hooks,
// nvlowlatencyvk_hooks, ngx_hooks. Uses void* to avoid pulling in Windows.h here.

#pragma once

namespace display_commanderhooks {

struct ModuleHookEntry {
    const char* name;
    void* detour;
    void** original;
};

}  // namespace display_commanderhooks
