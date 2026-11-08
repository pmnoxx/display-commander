// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "reshade_export_proxy.hpp"

// Source Code <Display Commander>
#include "../globals.hpp"

// Libraries <standard C++>
#include <algorithm>
#include <atomic>
#include <cstdint>

// Libraries <Windows.h>
#include <Windows.h>

// Libraries <Windows>
#include <Psapi.h>

namespace {

using ReShadeRegisterAddonFn = bool (*)(void* module, uint32_t api_version);
using ReShadeUnregisterAddonFn = void (*)(void* module);

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

}  // namespace

// Stubs live in this image so MFG Unlocker's local-dxgi Eligible() check
// (CreateDXGIFactory* + ReShadeRegisterAddon/UnregisterAddon as ImageEntry of
// dxgi.dll) accepts Display Commander as dxgi.dll. Calls forward to real ReShade.
extern "C" bool ReShadeRegisterAddon(void* module, uint32_t api_version) {
    HMODULE reshade = ResolveRealReshadeModule();
    if (reshade == nullptr) {
        return false;
    }
    auto* fn = reinterpret_cast<ReShadeRegisterAddonFn>(GetProcAddress(reshade, "ReShadeRegisterAddon"));
    if (fn == nullptr) {
        return false;
    }
    return fn(module, api_version);
}

extern "C" void ReShadeUnregisterAddon(void* module) {
    HMODULE reshade = ResolveRealReshadeModule();
    if (reshade == nullptr) {
        return;
    }
    auto* fn = reinterpret_cast<ReShadeUnregisterAddonFn>(GetProcAddress(reshade, "ReShadeUnregisterAddon"));
    if (fn == nullptr) {
        return;
    }
    fn(module);
}
