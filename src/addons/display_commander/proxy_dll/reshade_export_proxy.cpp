// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "reshade_export_proxy.hpp"

// Source Code <Display Commander>
#include "../globals.hpp"

// Libraries <standard C++>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>

// Libraries <Windows.h>
#include <Windows.h>

// Libraries <Windows>
#include <Psapi.h>

namespace {

bool IsDisplayCommanderModule(HMODULE module) noexcept {
    return module != nullptr && GetProcAddress(module, "GetDisplayCommanderState") != nullptr;
}

// Real ReShade (not this proxy). Prefer g_reshade_module; else scan for Register/Unregister
// without GetDisplayCommanderState (and prefer ImGui table export when present).
HMODULE ResolveRealReshadeModule() noexcept {
    HMODULE known = g_reshade_module.load(std::memory_order_acquire);
    if (known != nullptr && !IsDisplayCommanderModule(known)
        && GetProcAddress(known, "ReShadeRegisterAddon") != nullptr) {
        return known;
    }

    HMODULE modules[1024];
    DWORD bytes = 0;
    if (K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &bytes) == 0) {
        return nullptr;
    }
    const DWORD count =
        (std::min)(bytes / static_cast<DWORD>(sizeof(HMODULE)), static_cast<DWORD>(sizeof(modules) / sizeof(HMODULE)));

    HMODULE fallback = nullptr;
    for (DWORD i = 0; i < count; ++i) {
        HMODULE candidate = modules[i];
        if (candidate == nullptr || IsDisplayCommanderModule(candidate)) {
            continue;
        }
        if (GetProcAddress(candidate, "ReShadeRegisterAddon") == nullptr
            || GetProcAddress(candidate, "ReShadeUnregisterAddon") == nullptr) {
            continue;
        }
        if (GetProcAddress(candidate, "ReShadeGetImGuiFunctionTable") != nullptr) {
            return candidate;
        }
        if (fallback == nullptr) {
            fallback = candidate;
        }
    }
    return fallback;
}

// Per-export cache of the real ReShade function. Re-resolved when the real ReShade module changes
// (e.g. ReShade loaded after the first call, or the ReShade updater swapped it).
struct ForwardSlot {
    std::atomic<HMODULE> module{nullptr};
    std::atomic<FARPROC> proc{nullptr};
};

template <typename Fn>
Fn ResolveForward(ForwardSlot& slot, const char* name) noexcept {
    HMODULE reshade = ResolveRealReshadeModule();
    if (reshade == nullptr) {
        return nullptr;
    }
    if (slot.module.load(std::memory_order_acquire) != reshade) {
        FARPROC proc = GetProcAddress(reshade, name);
        slot.proc.store(proc, std::memory_order_release);
        slot.module.store(reshade, std::memory_order_release);
        return reinterpret_cast<Fn>(proc);
    }
    return reinterpret_cast<Fn>(slot.proc.load(std::memory_order_acquire));
}

#define DC_RESHADE_FORWARD(fn_type, name)       \
    static ForwardSlot s_slot;                  \
    const auto fn = ResolveForward<fn_type>(s_slot, name)

using OverlayCallback = void (*)(void* runtime);

}  // namespace

// Display Commander as dxgi.dll / other proxy exports the full ReShade add-on C API:
// - MFG Unlocker's local-dxgi Eligible() needs ReShadeRegisterAddon/UnregisterAddon on the same image.
// - Add-ons built with upstream reshade.hpp (e.g. RenoDX) pick the FIRST module exporting
//   Register/Unregister and then GetProcAddress every other API on it, so all of them must exist here.
// Every export forwards to the real ReShade module. ReShade identifies the calling add-on by the
// module argument or the callback address (never the return address), so forwarding is transparent.

extern "C" bool ReShadeRegisterAddon(void* module, uint32_t api_version) {
    DC_RESHADE_FORWARD(bool (*)(void*, uint32_t), "ReShadeRegisterAddon");
    return fn != nullptr && fn(module, api_version);
}

extern "C" void ReShadeUnregisterAddon(void* module) {
    DC_RESHADE_FORWARD(void (*)(void*), "ReShadeUnregisterAddon");
    if (fn != nullptr) fn(module);
}

extern "C" const void* ReShadeGetImGuiFunctionTable(uint32_t version) {
    DC_RESHADE_FORWARD(const void* (*)(uint32_t), "ReShadeGetImGuiFunctionTable");
    return fn != nullptr ? fn(version) : nullptr;
}

extern "C" void ReShadeLogMessage(void* module, int level, const char* message) {
    DC_RESHADE_FORWARD(void (*)(void*, int, const char*), "ReShadeLogMessage");
    if (fn != nullptr) fn(module, level, message);
}

extern "C" void ReShadeGetBasePath(char* path, size_t* path_size) {
    DC_RESHADE_FORWARD(void (*)(char*, size_t*), "ReShadeGetBasePath");
    if (fn != nullptr) {
        fn(path, path_size);
    } else if (path_size != nullptr) {
        if (path != nullptr && *path_size != 0) path[0] = '\0';
        *path_size = 0;
    }
}

extern "C" bool ReShadeGetConfigValue(void* module, void* runtime, const char* section, const char* key, char* value,
                                      size_t* value_size) {
    DC_RESHADE_FORWARD(bool (*)(void*, void*, const char*, const char*, char*, size_t*), "ReShadeGetConfigValue");
    if (fn != nullptr) return fn(module, runtime, section, key, value, value_size);
    if (value_size != nullptr) *value_size = 0;
    return false;
}

extern "C" void ReShadeSetConfigValue(void* module, void* runtime, const char* section, const char* key,
                                      const char* value) {
    DC_RESHADE_FORWARD(void (*)(void*, void*, const char*, const char*, const char*), "ReShadeSetConfigValue");
    if (fn != nullptr) fn(module, runtime, section, key, value);
}

extern "C" void ReShadeSetConfigArray(void* module, void* runtime, const char* section, const char* key,
                                      const char* value, size_t value_size) {
    DC_RESHADE_FORWARD(void (*)(void*, void*, const char*, const char*, const char*, size_t), "ReShadeSetConfigArray");
    if (fn != nullptr) fn(module, runtime, section, key, value, value_size);
}

extern "C" void ReShadeRegisterEvent(uint32_t ev, void* callback) {
    DC_RESHADE_FORWARD(void (*)(uint32_t, void*), "ReShadeRegisterEvent");
    if (fn != nullptr) fn(ev, callback);
}

extern "C" void ReShadeRegisterEventForAddon(void* module, uint32_t ev, void* callback) {
    DC_RESHADE_FORWARD(void (*)(void*, uint32_t, void*), "ReShadeRegisterEventForAddon");
    if (fn != nullptr) fn(module, ev, callback);
}

extern "C" void ReShadeUnregisterEvent(uint32_t ev, void* callback) {
    DC_RESHADE_FORWARD(void (*)(uint32_t, void*), "ReShadeUnregisterEvent");
    if (fn != nullptr) fn(ev, callback);
}

extern "C" void ReShadeUnregisterEventForAddon(void* module, uint32_t ev, void* callback) {
    DC_RESHADE_FORWARD(void (*)(void*, uint32_t, void*), "ReShadeUnregisterEventForAddon");
    if (fn != nullptr) fn(module, ev, callback);
}

extern "C" void ReShadeRegisterOverlay(const char* title, OverlayCallback callback) {
    DC_RESHADE_FORWARD(void (*)(const char*, OverlayCallback), "ReShadeRegisterOverlay");
    if (fn != nullptr) fn(title, callback);
}

extern "C" void ReShadeRegisterOverlayForAddon(void* module, const char* title, OverlayCallback callback) {
    DC_RESHADE_FORWARD(void (*)(void*, const char*, OverlayCallback), "ReShadeRegisterOverlayForAddon");
    if (fn != nullptr) fn(module, title, callback);
}

extern "C" void ReShadeUnregisterOverlay(const char* title, OverlayCallback callback) {
    DC_RESHADE_FORWARD(void (*)(const char*, OverlayCallback), "ReShadeUnregisterOverlay");
    if (fn != nullptr) fn(title, callback);
}

extern "C" void ReShadeUnregisterOverlayForAddon(void* module, const char* title, OverlayCallback callback) {
    DC_RESHADE_FORWARD(void (*)(void*, const char*, OverlayCallback), "ReShadeUnregisterOverlayForAddon");
    if (fn != nullptr) fn(module, title, callback);
}

extern "C" bool ReShadeCreateEffectRuntime(int api, void* opaque_device, void* opaque_command_queue,
                                           void* opaque_swapchain, const char* config_path, void** out_runtime) {
    DC_RESHADE_FORWARD(bool (*)(int, void*, void*, void*, const char*, void**), "ReShadeCreateEffectRuntime");
    if (fn != nullptr) return fn(api, opaque_device, opaque_command_queue, opaque_swapchain, config_path, out_runtime);
    if (out_runtime != nullptr) *out_runtime = nullptr;
    return false;
}

extern "C" void ReShadeDestroyEffectRuntime(void* runtime) {
    DC_RESHADE_FORWARD(void (*)(void*), "ReShadeDestroyEffectRuntime");
    if (fn != nullptr) fn(runtime);
}

extern "C" void ReShadeUpdateAndPresentEffectRuntime(void* runtime) {
    DC_RESHADE_FORWARD(void (*)(void*), "ReShadeUpdateAndPresentEffectRuntime");
    if (fn != nullptr) fn(runtime);
}

#undef DC_RESHADE_FORWARD
