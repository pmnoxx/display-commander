// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "dxgi_hooks.hpp"
#include "../windows_hooks/api_hooks.hpp"
#include "../../globals.hpp"
#include "../hook_suppression_manager.hpp"
#include "../loadlibrary_hooks.hpp"
#include "../../utils/detour_call_tracker.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/timing.hpp"

// Libraries <standard C++>
// (none)

// Libraries <Windows.h> — before other Windows headers
#include <Windows.h>

// Libraries <Windows> / SDK
#include <d3d11.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_3.h>
#include <MinHook.h>

namespace display_commanderhooks {

bool InstallDxgiFactoryHooks(HMODULE dxgi_module) {
    if (!display_commanderhooks::g_hooked_before_reshade.load()) {
        return true;
    }
    CALL_GUARD(utils::get_now_ns());
    // Check if this module is ReShade's proxy by checking for ReShade exports
    FARPROC reshade_register = GetProcAddress(dxgi_module, "ReShadeRegisterAddon");
    FARPROC reshade_unregister = GetProcAddress(dxgi_module, "ReShadeUnregisterAddon");
    if (reshade_register != nullptr || reshade_unregister != nullptr) {
        LogInfo("Skipping DXGI hooks installation - detected ReShade proxy module (0x%p)", dxgi_module);
        return true;
    }

    static bool dxgi_hooks_installed = false;
    if (dxgi_hooks_installed) {
        LogInfo("DXGI hooks already installed");
        return true;
    }

    // Check if DXGI hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::DXGI_FACTORY)) {
        LogInfo("DXGI hooks installation suppressed by user setting");
        return false;
    }

    dxgi_hooks_installed = true;

    // Initialize MinHook (only if not already initialized)
    MH_STATUS init_status = SafeInitializeMinHook(display_commanderhooks::HookType::DXGI_FACTORY);
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for DXGI hooks - Status: %d", init_status);
        return false;
    }

    if (init_status == MH_ERROR_ALREADY_INITIALIZED) {
        LogInfo("MinHook already initialized, proceeding with DXGI hooks");
    } else {
        LogInfo("MinHook initialized successfully for DXGI hooks");
    }

    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
        display_commanderhooks::HookType::DXGI_FACTORY);

    // Hook CreateDXGIFactory - try both system and ReShade versions
    auto CreateDXGIFactory_sys =
        reinterpret_cast<decltype(&CreateDXGIFactory)>(GetProcAddress(dxgi_module, "CreateDXGIFactory"));
    if (CreateDXGIFactory_sys != nullptr) {
        if (!CreateAndEnableHook(CreateDXGIFactory_sys, CreateDXGIFactory_Detour,
                                 reinterpret_cast<LPVOID*>(&CreateDXGIFactory_Original), "CreateDXGIFactory")) {
            LogError("Failed to create and enable CreateDXGIFactory system hook");
            return false;
        }
        LogInfo("CreateDXGIFactory system hook created successfully");
    } else {
        LogWarn("Failed to get CreateDXGIFactory system address, trying ReShade version");
        if (!CreateAndEnableHook(CreateDXGIFactory, CreateDXGIFactory_Detour,
                                 reinterpret_cast<LPVOID*>(&CreateDXGIFactory_Original), "CreateDXGIFactory")) {
            LogError("Failed to create and enable CreateDXGIFactory ReShade hook");
            return false;
        }
        LogInfo("CreateDXGIFactory ReShade hook created successfully");
    }

    // Hook CreateDXGIFactory1 - try both system and ReShade versions
    auto CreateDXGIFactory1_sys =
        reinterpret_cast<decltype(&CreateDXGIFactory1)>(GetProcAddress(dxgi_module, "CreateDXGIFactory1"));
    if (CreateDXGIFactory1_sys != nullptr) {
        if (!CreateAndEnableHook(CreateDXGIFactory1_sys, CreateDXGIFactory1_Detour,
                                 reinterpret_cast<LPVOID*>(&CreateDXGIFactory1_Original), "CreateDXGIFactory1")) {
            LogError("Failed to create and enable CreateDXGIFactory1 system hook");
            return false;
        }
        LogInfo("CreateDXGIFactory1 system hook created successfully");
    } else {
        LogWarn("Failed to get CreateDXGIFactory1 system address, trying ReShade version");
        if (!CreateAndEnableHook(CreateDXGIFactory1, CreateDXGIFactory1_Detour,
                                 reinterpret_cast<LPVOID*>(&CreateDXGIFactory1_Original), "CreateDXGIFactory1")) {
            LogError("Failed to create and enable CreateDXGIFactory1 ReShade hook");
            return false;
        }
        LogInfo("CreateDXGIFactory1 ReShade hook created successfully");
    }

    // Hook CreateDXGIFactory2 - try both system and ReShade versions
    auto CreateDXGIFactory2_sys =
        reinterpret_cast<CreateDXGIFactory2_pfn>(GetProcAddress(dxgi_module, "CreateDXGIFactory2"));
    if (CreateDXGIFactory2_sys != nullptr) {
        if (!CreateAndEnableHook(CreateDXGIFactory2_sys, CreateDXGIFactory2_Detour,
                                 reinterpret_cast<LPVOID*>(&CreateDXGIFactory2_Original), "CreateDXGIFactory2")) {
            LogError("Failed to create and enable CreateDXGIFactory2 system hook");
            return false;
        }
        LogInfo("CreateDXGIFactory2 system hook created successfully");
    } else {
        LogWarn("Failed to get CreateDXGIFactory2 system address, trying ReShade version");
        if (!CreateAndEnableHook(CreateDXGIFactory2, CreateDXGIFactory2_Detour,
                                 reinterpret_cast<LPVOID*>(&CreateDXGIFactory2_Original), "CreateDXGIFactory2")) {
            LogError("Failed to create and enable CreateDXGIFactory2 ReShade hook");
            return false;
        }
        LogInfo("CreateDXGIFactory2 ReShade hook created successfully");
    }

    LogInfo("DXGI Factory hooks installed successfully");

    return true;
}

bool InstallD3D11DeviceHooks(HMODULE d3d11_module) {
    if (g_reshade_module != nullptr && !display_commanderhooks::g_hooked_before_reshade.load()) {
        return true;
    }
    // Check if this module is ReShade's proxy by checking for ReShade exports
    FARPROC reshade_register = GetProcAddress(d3d11_module, "ReShadeRegisterAddon");
    FARPROC reshade_unregister = GetProcAddress(d3d11_module, "ReShadeUnregisterAddon");
    if (reshade_register != nullptr && reshade_unregister != nullptr) {
        LogInfo("Skipping D3D11 hooks installation - detected ReShade proxy module (0x%p)", d3d11_module);
        return true;
    }

    static bool d3d11_device_hooks_installed = false;
    if (d3d11_device_hooks_installed) {
        LogInfo("D3D11 device hooks already installed");
        return true;
    }

    // Check if D3D11 device hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::D3D11_DEVICE)) {
        LogInfo("D3D11 device hooks installation suppressed by user setting");
        return false;
    }

    d3d11_device_hooks_installed = true;

    LogInfo("Installing D3D11 device creation hooks...");

    // Hook D3D11CreateDeviceAndSwapChain
    auto D3D11CreateDeviceAndSwapChain_sys = reinterpret_cast<decltype(&D3D11CreateDeviceAndSwapChain)>(
        GetProcAddress(d3d11_module, "D3D11CreateDeviceAndSwapChain"));
    if (D3D11CreateDeviceAndSwapChain_sys != nullptr) {
        if (!CreateAndEnableHook(D3D11CreateDeviceAndSwapChain_sys, D3D11CreateDeviceAndSwapChain_Detour,
                                 reinterpret_cast<LPVOID*>(&D3D11CreateDeviceAndSwapChain_Original),
                                 "D3D11CreateDeviceAndSwapChain")) {
            LogError("Failed to create and enable D3D11CreateDeviceAndSwapChain hook");
            return false;
        }
        LogInfo("D3D11CreateDeviceAndSwapChain hook created successfully");
    } else {
        LogWarn("Failed to get D3D11CreateDeviceAndSwapChain address from d3d11.dll");
    }

    // Hook D3D11CreateDevice
    auto D3D11CreateDevice_sys =
        reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(d3d11_module, "D3D11CreateDevice"));
    if (D3D11CreateDevice_sys != nullptr) {
        if (!CreateAndEnableHook(D3D11CreateDevice_sys, D3D11CreateDevice_Detour,
                                 reinterpret_cast<LPVOID*>(&D3D11CreateDevice_Original), "D3D11CreateDevice")) {
            LogError("Failed to create and enable D3D11CreateDevice hook");
            return false;
        }
        LogInfo("D3D11CreateDevice hook created successfully");
    } else {
        LogWarn("Failed to get D3D11CreateDevice address from d3d11.dll");
    }

    // Hook D3D11On12CreateDevice
    auto D3D11On12CreateDevice_sys =
        reinterpret_cast<decltype(&D3D11On12CreateDevice)>(GetProcAddress(d3d11_module, "D3D11On12CreateDevice"));
    if (D3D11On12CreateDevice_sys != nullptr) {
        if (!CreateAndEnableHook(D3D11On12CreateDevice_sys, D3D11On12CreateDevice_Detour,
                                 reinterpret_cast<LPVOID*>(&D3D11On12CreateDevice_Original), "D3D11On12CreateDevice")) {
            LogError("Failed to create and enable D3D11On12CreateDevice hook");
            return false;
        }
        LogInfo("D3D11On12CreateDevice hook created successfully");
    } else {
        LogWarn("Failed to get D3D11On12CreateDevice address from d3d11.dll");
    }

    LogInfo("D3D11 device hooks installed successfully");

    // Mark D3D11 device hooks as installed
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
        display_commanderhooks::HookType::D3D11_DEVICE);

    return true;
}

bool InstallD3D12DeviceHooks(HMODULE d3d12_module) {
    if (!display_commanderhooks::g_hooked_before_reshade.load()) {
        return true;
    }
    // Check if this module is ReShade's proxy by checking for ReShade exports
    FARPROC reshade_register = GetProcAddress(d3d12_module, "ReShadeRegisterAddon");
    FARPROC reshade_unregister = GetProcAddress(d3d12_module, "ReShadeUnregisterAddon");
    if (reshade_register != nullptr && reshade_unregister != nullptr) {
        LogInfo("Skipping D3D12 hooks installation - detected ReShade proxy module (0x%p)", d3d12_module);
        return true;
    }

    static bool d3d12_device_hooks_installed = false;
    if (d3d12_device_hooks_installed) {
        LogInfo("D3D12 device hooks already installed");
        return true;
    }

    // Check if D3D12 device hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::D3D12_DEVICE)) {
        LogInfo("D3D12 device hooks installation suppressed by user setting");
        return false;
    }

    d3d12_device_hooks_installed = true;

    LogInfo("Installing D3D12 device creation hooks...");

    // Hook D3D12CreateDevice
    auto D3D12CreateDevice_sys =
        reinterpret_cast<decltype(&D3D12CreateDevice)>(GetProcAddress(d3d12_module, "D3D12CreateDevice"));
    if (D3D12CreateDevice_sys != nullptr) {
        if (!CreateAndEnableHook(D3D12CreateDevice_sys, D3D12CreateDevice_Detour,
                                 reinterpret_cast<LPVOID*>(&D3D12CreateDevice_Original), "D3D12CreateDevice")) {
            LogError("Failed to create and enable D3D12CreateDevice hook");
            return false;
        }
        LogInfo("D3D12CreateDevice hook created successfully");
    } else {
        LogWarn("Failed to get D3D12CreateDevice address from d3d12.dll");
    }

    LogInfo("D3D12 device hooks installed successfully");

    // Mark D3D12 device hooks as installed
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
        display_commanderhooks::HookType::D3D12_DEVICE);

    return true;
}

}  // namespace display_commanderhooks
