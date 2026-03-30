// Source Code <Display Commander>
// Shared hook table entry for module-based detours (GetProcAddress + MinHook).
// Used by: api_hooks (user32/kernel32), ddraw_present_hooks, display_settings_hooks, dxgi_hooks, dxgi_present_hooks,
// opengl_hooks, timeslowdown_hooks, vulkan_loader_hooks, streamline_hooks, nvlowlatencyvk_hooks, ngx_hooks. Uses void* to avoid pulling in Windows.h here.

#pragma once

#include <cstddef>

namespace display_commanderhooks {

/** Hook table entry: name, detour, original. Used for Vulkan loader, Streamline,
 *  NvLowLatencyVk, and any GetProcAddress + MinHook table-driven install. */
struct HookEntry {
    const char* name;
    void* detour;
    void** original;
};

/** Install hooks from a table: resolves each entry via GetProcAddress(hModule, name),
 *  then CreateAndEnableHook. hModule is the module handle (e.g. from GetModuleHandleW).
 *  Returns false and logs on first failure. */
bool InstallHooksFromModule(void* hModule, const HookEntry* entries, std::size_t count);

/** Remove hooks from a table: resolves each entry via GetProcAddress(hModule, name),
 *  then MH_RemoveHook. Use the same table as InstallHooksFromModule for uninstall. */
void RemoveHooksFromModule(void* hModule, const HookEntry* entries, std::size_t count);

/** Set each entry's original pointer to nullptr. Call after RemoveHooksFromModule. */
void ResetOriginalsFromTable(const HookEntry* entries, std::size_t count);

/** Resolver callback: returns target address for hook, or nullptr if not available.
 *  Used for extension APIs (e.g. wglGetProcAddress) that are not in the module export table. */
using HookResolverFn = void* (*)(const char* name);

/** Install hooks from a table using a custom resolver (e.g. wglGetProcAddress).
 *  Missing targets are skipped with LogInfo; hook failures log LogWarn. Does not fail the overall install. */
void InstallHooksFromResolver(const HookEntry* entries, std::size_t count, HookResolverFn resolve);

/** Remove hooks from a table by original pointer (e.g. hooks installed via InstallHooksFromResolver).
 *  For each entry with non-null *original, calls MH_DisableHook and MH_RemoveHook. Call ResetOriginalsFromTable after. */
void RemoveHooksByOriginalTable(const HookEntry* entries, std::size_t count);

}  // namespace display_commanderhooks
