// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "streamline_proxy_dxgi.hpp"

#include "../../globals.hpp"
#include "../../hooks/dxgi/dxgi_present_hooks.hpp"
#include "../../hooks/hook_suppression_manager.hpp"
#include "../../hooks/present_traffic_tracking.hpp"
#include "../../settings/advanced_tab_settings.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../swapchain_events.hpp"
#include "../../utils/detour_call_tracker.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/timing.hpp"

#include <MinHook.h>

// Libraries <standard C++>
#include <atomic>
#include <cstddef>
#include <cstdint>

// Libraries <Windows.h> — before other Windows headers
#include <Windows.h>

// Libraries <Windows>
#include <dxgi1_2.h>
#include <wrl/client.h>

namespace display_commander::features::streamline {

static std::atomic<uint64_t> g_sl_proxy_dxgi_detour_counts[static_cast<std::size_t>(
    StreamlineProxyDxgiDetourCounter::Count_)];

namespace {

// ReShade-aligned factory vtable indices — see external/reshade/source/dxgi/dxgi.cpp.
enum IDXGIFactoryVTable : int {
    IDXGIFactory_CreateSwapChain = 10,
    IDXGIFactory1_CreateSwapChainForHwnd = 15,
    IDXGIFactory1_CreateSwapChainForCoreWindow = 16,
    IDXGIFactory2_CreateSwapChainForComposition = 24,
};

using IDXGIFactory_CreateSwapChain_pfn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory* This, IUnknown* pDevice,
                                                                     DXGI_SWAP_CHAIN_DESC* pDesc,
                                                                     IDXGISwapChain** ppSwapChain);
using IDXGIFactory1_CreateSwapChainForHwnd_pfn =
    HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory1* This, IUnknown* pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1* pDesc,
                                const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGIOutput* pRestrictToOutput,
                                IDXGISwapChain1** ppSwapChain);
using IDXGIFactory1_CreateSwapChainForCoreWindow_pfn =
    HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory1* This, IUnknown* pDevice, IUnknown* pWindow,
                              const DXGI_SWAP_CHAIN_DESC1* pDesc, IDXGIOutput* pRestrictToOutput,
                              IDXGISwapChain1** ppSwapChain);
using IDXGIFactory2_CreateSwapChainForComposition_pfn =
    HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2* This, IUnknown* pDevice, const DXGI_SWAP_CHAIN_DESC1* pDesc,
                              IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain);

IDXGIFactory_CreateSwapChain_pfn IDXGIFactory_CreateSwapChain_Streamline_Original = nullptr;
IDXGIFactory1_CreateSwapChainForHwnd_pfn IDXGIFactory1_CreateSwapChainForHwnd_Streamline_Original = nullptr;
IDXGIFactory1_CreateSwapChainForCoreWindow_pfn IDXGIFactory1_CreateSwapChainForCoreWindow_Streamline_Original =
    nullptr;
IDXGIFactory2_CreateSwapChainForComposition_pfn IDXGIFactory2_CreateSwapChainForComposition_Streamline_Original =
    nullptr;

display_commanderhooks::dxgi::IDXGISwapChain_Present_pfn IDXGISwapChain_Present_Streamline_Original = nullptr;
display_commanderhooks::dxgi::IDXGISwapChain_Present1_pfn IDXGISwapChain_Present1_Streamline_Original = nullptr;

void LogDxgiErrorUpTo10(const char* method, HRESULT hr, int* pCount) {
    if (SUCCEEDED(hr) || pCount == nullptr || *pCount >= 10) return;
    LogError("[DXGI error] %s returned 0x%08X", method, static_cast<unsigned>(hr));
    (*pCount)++;
}

int VsyncOverrideComboIndexToApiValue(int combo_index) {
    static const int kMap[] = {-1, 1, 2, 3, 4, 0};
    if (combo_index < 0 || combo_index > 5) return -1;
    return kMap[combo_index];
}

bool HookStreamlineProxySwapchainImpl(IDXGISwapChain* swapchain);

void BumpSlProxyDxgiDetourCount(StreamlineProxyDxgiDetourCounter which) {
    const int i = static_cast<int>(which);
    if (i < 0 || i >= static_cast<int>(StreamlineProxyDxgiDetourCounter::Count_)) return;
    g_sl_proxy_dxgi_detour_counts[static_cast<std::size_t>(i)].fetch_add(1, std::memory_order_relaxed);
}

HRESULT STDMETHODCALLTYPE IDXGIFactory_CreateSwapChain_Streamline_Detour(IDXGIFactory* This, IUnknown* pDevice,
                                                                          DXGI_SWAP_CHAIN_DESC* pDesc,
                                                                          IDXGISwapChain** ppSwapChain) {
    BumpSlProxyDxgiDetourCount(StreamlineProxyDxgiDetourCounter::Factory_CreateSwapChain);
    CALL_GUARD_NO_TS();
    if (IDXGIFactory_CreateSwapChain_Streamline_Original == nullptr) {
        return This->CreateSwapChain(pDevice, pDesc, ppSwapChain);
    }
    HRESULT hr = IDXGIFactory_CreateSwapChain_Streamline_Original(This, pDevice, pDesc, ppSwapChain);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGIFactory::CreateSwapChain (Streamline)", hr, &s_err_count);
    if (SUCCEEDED(hr) && ppSwapChain != nullptr && *ppSwapChain != nullptr) {
        HookStreamlineProxySwapchainImpl(*ppSwapChain);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE IDXGIFactory1_CreateSwapChainForHwnd_Streamline_Detour(
    IDXGIFactory1* This, IUnknown* pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1* pDesc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGIOutput* pRestrictToOutput,
    IDXGISwapChain1** ppSwapChain) {
    BumpSlProxyDxgiDetourCount(StreamlineProxyDxgiDetourCounter::Factory1_CreateSwapChainForHwnd);
    CALL_GUARD_NO_TS();
    if (IDXGIFactory1_CreateSwapChainForHwnd_Streamline_Original == nullptr) {
        Microsoft::WRL::ComPtr<IDXGIFactory2> factory2;
        if (SUCCEEDED(This->QueryInterface(IID_PPV_ARGS(&factory2)))) {
            return factory2->CreateSwapChainForHwnd(pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput,
                                                    ppSwapChain);
        }
        return E_NOINTERFACE;
    }
    HRESULT hr = IDXGIFactory1_CreateSwapChainForHwnd_Streamline_Original(This, pDevice, hWnd, pDesc, pFullscreenDesc,
                                                                          pRestrictToOutput, ppSwapChain);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGIFactory1::CreateSwapChainForHwnd (Streamline)", hr, &s_err_count);
    if (SUCCEEDED(hr) && ppSwapChain != nullptr && *ppSwapChain != nullptr) {
        HookStreamlineProxySwapchainImpl(*ppSwapChain);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE IDXGIFactory1_CreateSwapChainForCoreWindow_Streamline_Detour(
    IDXGIFactory1* This, IUnknown* pDevice, IUnknown* pWindow, const DXGI_SWAP_CHAIN_DESC1* pDesc,
    IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain) {
    BumpSlProxyDxgiDetourCount(StreamlineProxyDxgiDetourCounter::Factory1_CreateSwapChainForCoreWindow);
    CALL_GUARD_NO_TS();
    if (IDXGIFactory1_CreateSwapChainForCoreWindow_Streamline_Original == nullptr) {
        Microsoft::WRL::ComPtr<IDXGIFactory2> factory2;
        if (SUCCEEDED(This->QueryInterface(IID_PPV_ARGS(&factory2)))) {
            return factory2->CreateSwapChainForCoreWindow(pDevice, pWindow, pDesc, pRestrictToOutput, ppSwapChain);
        }
        return E_NOINTERFACE;
    }
    HRESULT hr = IDXGIFactory1_CreateSwapChainForCoreWindow_Streamline_Original(This, pDevice, pWindow, pDesc,
                                                                                pRestrictToOutput, ppSwapChain);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGIFactory1::CreateSwapChainForCoreWindow (Streamline)", hr, &s_err_count);
    if (SUCCEEDED(hr) && ppSwapChain != nullptr && *ppSwapChain != nullptr) {
        HookStreamlineProxySwapchainImpl(*ppSwapChain);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE IDXGIFactory2_CreateSwapChainForComposition_Streamline_Detour(
    IDXGIFactory2* This, IUnknown* pDevice, const DXGI_SWAP_CHAIN_DESC1* pDesc, IDXGIOutput* pRestrictToOutput,
    IDXGISwapChain1** ppSwapChain) {
    BumpSlProxyDxgiDetourCount(StreamlineProxyDxgiDetourCounter::Factory2_CreateSwapChainForComposition);
    CALL_GUARD_NO_TS();
    if (IDXGIFactory2_CreateSwapChainForComposition_Streamline_Original == nullptr) {
        return E_NOINTERFACE;
    }
    HRESULT hr = IDXGIFactory2_CreateSwapChainForComposition_Streamline_Original(This, pDevice, pDesc,
                                                                                 pRestrictToOutput, ppSwapChain);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGIFactory2::CreateSwapChainForComposition (Streamline)", hr, &s_err_count);
    if (SUCCEEDED(hr) && ppSwapChain != nullptr && *ppSwapChain != nullptr) {
        HookStreamlineProxySwapchainImpl(*ppSwapChain);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_Present_Streamline_Detour(IDXGISwapChain* This, UINT SyncInterval,
                                                                 UINT PresentFlags) {
    BumpSlProxyDxgiDetourCount(StreamlineProxyDxgiDetourCounter::SwapChain_Present);
    display_commanderhooks::dxgi::DCDxgiSwapchainData data{};
    if (This != nullptr) {
        display_commanderhooks::dxgi::LoadDCDxgiSwapchainData(This, &data);
    }
    if (display_commanderhooks::dxgi::g_dxgi_present_nested_depth.load() > 0) {
        if (IDXGISwapChain_Present_Streamline_Original != nullptr) {
            return IDXGISwapChain_Present_Streamline_Original(This, SyncInterval, PresentFlags);
        }
        return This->Present(SyncInterval, PresentFlags);
    }
    const int override_val = VsyncOverrideComboIndexToApiValue(settings::g_mainTabSettings.vsync_override.GetValue());
    const UINT effective_interval = (override_val >= 0) ? static_cast<UINT>(override_val) : SyncInterval;
    if (override_val >= 1) {
        PresentFlags &= ~DXGI_PRESENT_ALLOW_TEARING;
    } else if (override_val == 0) {
        PresentFlags |= DXGI_PRESENT_ALLOW_TEARING;
    }
    const LONGLONG now_ns = utils::get_now_ns();
    display_commanderhooks::g_last_dxgi_present_time_ns.store(static_cast<uint64_t>(now_ns), std::memory_order_relaxed);
    CALL_GUARD(now_ns);

    if (settings::g_advancedTabSettings.flush_command_queue_before_sleep.GetValue()) {
        if (data.command_queue != nullptr) {
            data.command_queue->flush_immediate_command_list();
        }
    }

    bool use_fps_limiter = false;
    if (GetEffectiveUseStreamlineProxyFpsLimiter()) {
        ChooseFpsLimiter(static_cast<uint64_t>(utils::get_now_ns()), FpsLimiterCallSite::dxgi_swapchain_streamline_proxy);
        use_fps_limiter = GetChosenFpsLimiter(FpsLimiterCallSite::dxgi_swapchain_streamline_proxy);
        if (use_fps_limiter) {
            ::OnPresentFlags2(true, true);
            RecordNativeFrameTime();
        }
        if (GetChosenFrameTimeLocation() == FpsLimiterCallSite::dxgi_swapchain_streamline_proxy) {
            RecordFrameTime(FrameTimeMode::kPresent);
        }
    }
    if (IDXGISwapChain_Present_Streamline_Original == nullptr) {
        LogError("IDXGISwapChain_Present_Streamline_Detour: original is null");
        return This->Present(effective_interval, PresentFlags);
    }
    HRESULT res = IDXGISwapChain_Present_Streamline_Original(This, effective_interval, PresentFlags);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGISwapChain::Present (Streamline)", res, &s_err_count);
    if (use_fps_limiter) {
        display_commanderhooks::dxgi::HandlePresentAfter(true);
    }
    CALL_GUARD_NO_TS();
    return res;
}

HRESULT STDMETHODCALLTYPE IDXGISwapChain_Present1_Streamline_Detour(IDXGISwapChain1* This, UINT SyncInterval,
                                                                    UINT PresentFlags,
                                                                    const DXGI_PRESENT_PARAMETERS* pPresentParameters) {
    BumpSlProxyDxgiDetourCount(StreamlineProxyDxgiDetourCounter::SwapChain1_Present1);
    IDXGISwapChain* baseSwapChain = reinterpret_cast<IDXGISwapChain*>(This);
    display_commanderhooks::dxgi::DCDxgiSwapchainData data{};
    if (baseSwapChain != nullptr) {
        display_commanderhooks::dxgi::LoadDCDxgiSwapchainData(baseSwapChain, &data);
    }
    CALL_GUARD_NO_TS();
    const int override_val = VsyncOverrideComboIndexToApiValue(settings::g_mainTabSettings.vsync_override.GetValue());
    const UINT effective_interval = (override_val >= 0) ? static_cast<UINT>(override_val) : SyncInterval;
    if (override_val >= 1) {
        PresentFlags &= ~DXGI_PRESENT_ALLOW_TEARING;
    } else if (override_val == 0) {
        PresentFlags |= DXGI_PRESENT_ALLOW_TEARING;
    }

    if (settings::g_advancedTabSettings.flush_command_queue_before_sleep.GetValue()) {
        if (data.command_queue != nullptr) {
            data.command_queue->flush_immediate_command_list();
        }
    }

    bool use_fps_limiter = false;
    if (GetEffectiveUseStreamlineProxyFpsLimiter()) {
        ChooseFpsLimiter(static_cast<uint64_t>(utils::get_now_ns()),
                         FpsLimiterCallSite::dxgi_swapchain1_streamline_proxy);
        use_fps_limiter = GetChosenFpsLimiter(FpsLimiterCallSite::dxgi_swapchain1_streamline_proxy);
        if (use_fps_limiter) {
            ::OnPresentFlags2(true, false);
            RecordNativeFrameTime();
        }
        if (GetChosenFrameTimeLocation() == FpsLimiterCallSite::dxgi_swapchain1_streamline_proxy) {
            RecordFrameTime(FrameTimeMode::kPresent);
        }
    }
    display_commanderhooks::dxgi::g_dxgi_present_nested_depth.fetch_add(1);
    if (IDXGISwapChain_Present1_Streamline_Original == nullptr) {
        LogError("IDXGISwapChain_Present1_Streamline_Detour: original is null");
        HRESULT res = This->Present1(effective_interval, PresentFlags, pPresentParameters);
        display_commanderhooks::dxgi::g_dxgi_present_nested_depth.fetch_sub(1);
        return res;
    }
    HRESULT res =
        IDXGISwapChain_Present1_Streamline_Original(This, effective_interval, PresentFlags, pPresentParameters);
    display_commanderhooks::dxgi::g_dxgi_present_nested_depth.fetch_sub(1);
    static int s_err_count = 0;
    LogDxgiErrorUpTo10("IDXGISwapChain1::Present1 (Streamline)", res, &s_err_count);
    if (use_fps_limiter) {
        display_commanderhooks::dxgi::HandlePresentAfter(false);
    }
    return res;
}

bool HookStreamlineProxySwapchainImpl(IDXGISwapChain* swapchain) {
    if (swapchain == nullptr) {
        return false;
    }
    if (g_dx9_swapchain_detected.load()) {
        return false;
    }
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::SL_PROXY_DXGI_SWAPCHAIN)) {
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain1;
    Microsoft::WRL::ComPtr<IDXGISwapChain2> swapchain2;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain3;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapchain4;
    void** vtable = nullptr;
    int vtable_version = 0;

    if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain4)))) {
        vtable_version = 4;
        vtable = *reinterpret_cast<void***>(swapchain4.Get());
    } else if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain3)))) {
        vtable_version = 3;
        vtable = *reinterpret_cast<void***>(swapchain3.Get());
    } else if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain2)))) {
        vtable_version = 2;
        vtable = *reinterpret_cast<void***>(swapchain2.Get());
    } else if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain1)))) {
        vtable_version = 1;
        vtable = *reinterpret_cast<void***>(swapchain1.Get());
    } else {
        vtable = *reinterpret_cast<void***>(swapchain);
    }

    if (vtable == nullptr) {
        return false;
    }

    MH_STATUS init_status = SafeInitializeMinHook(display_commanderhooks::HookType::SL_PROXY_DXGI_SWAPCHAIN);
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("[Streamline] HookStreamlineProxySwapchain: MinHook init failed: %d", init_status);
        return false;
    }

    static bool streamline_proxy_present_hooked = false;
    static bool streamline_proxy_present1_hooked = false;
    bool any_hooked = false;

    if (!streamline_proxy_present_hooked
        && CreateAndEnableHook(vtable[8], reinterpret_cast<LPVOID>(IDXGISwapChain_Present_Streamline_Detour),
                               reinterpret_cast<LPVOID*>(&IDXGISwapChain_Present_Streamline_Original),
                               "IDXGISwapChain::Present (Streamline proxy)")) {
        streamline_proxy_present_hooked = true;
        any_hooked = true;
        LogInfo("[Streamline] proxy swapchain Present hooked");
    }

    if (vtable_version >= 1 && !streamline_proxy_present1_hooked
        && CreateAndEnableHook(vtable[22], reinterpret_cast<LPVOID>(IDXGISwapChain_Present1_Streamline_Detour),
                               reinterpret_cast<LPVOID*>(&IDXGISwapChain_Present1_Streamline_Original),
                               "IDXGISwapChain1::Present1 (Streamline proxy)")) {
        streamline_proxy_present1_hooked = true;
        any_hooked = true;
        LogInfo("[Streamline] proxy swapchain Present1 hooked");
    }

    if (any_hooked) {
        display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
            display_commanderhooks::HookType::SL_PROXY_DXGI_SWAPCHAIN);
    }
    return any_hooked;
}

}  // namespace

uint64_t GetStreamlineProxyDxgiDetourCallCount(StreamlineProxyDxgiDetourCounter which) {
    const int i = static_cast<int>(which);
    if (i < 0 || i >= static_cast<int>(StreamlineProxyDxgiDetourCounter::Count_)) return 0;
    return g_sl_proxy_dxgi_detour_counts[static_cast<std::size_t>(i)].load(std::memory_order_relaxed);
}

const char* GetStreamlineProxyDxgiDetourCounterLabel(StreamlineProxyDxgiDetourCounter which) {
    switch (which) {
        case StreamlineProxyDxgiDetourCounter::Factory_CreateSwapChain:
            return "IDXGIFactory_CreateSwapChain_Streamline_Detour";
        case StreamlineProxyDxgiDetourCounter::Factory1_CreateSwapChainForHwnd:
            return "IDXGIFactory1_CreateSwapChainForHwnd_Streamline_Detour";
        case StreamlineProxyDxgiDetourCounter::Factory1_CreateSwapChainForCoreWindow:
            return "IDXGIFactory1_CreateSwapChainForCoreWindow_Streamline_Detour";
        case StreamlineProxyDxgiDetourCounter::Factory2_CreateSwapChainForComposition:
            return "IDXGIFactory2_CreateSwapChainForComposition_Streamline_Detour";
        case StreamlineProxyDxgiDetourCounter::SwapChain_Present:
            return "IDXGISwapChain_Present_Streamline_Detour";
        case StreamlineProxyDxgiDetourCounter::SwapChain1_Present1:
            return "IDXGISwapChain_Present1_Streamline_Detour";
        default:
            return "?";
    }
}

void ResetStreamlineProxyDxgiDetourCallCounts() {
    for (std::atomic<uint64_t>& c : g_sl_proxy_dxgi_detour_counts) {
        c.store(0, std::memory_order_relaxed);
    }
}

bool HookStreamlineProxyFactory(IUnknown* upgraded_iface) {
    if (upgraded_iface == nullptr) {
        return false;
    }
    if (g_dx9_swapchain_detected.load()) {
        return false;
    }
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::SL_PROXY_DXGI_SWAPCHAIN)) {
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIFactory> ifactory;
    HRESULT hr = upgraded_iface->QueryInterface(IID_PPV_ARGS(&ifactory));
    if (FAILED(hr) || ifactory == nullptr) {
        return false;
    }

    MH_STATUS init_status = SafeInitializeMinHook(display_commanderhooks::HookType::SL_PROXY_DXGI_SWAPCHAIN);
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("[Streamline] HookStreamlineProxyFactory: MinHook init failed: %d", init_status);
        return false;
    }

    static bool streamline_proxy_factory_hooked = false;

    bool any_hooked = false;
    {
        void** vtable = *reinterpret_cast<void***>(ifactory.Get());
        if (CreateAndEnableHook(vtable[IDXGIFactory_CreateSwapChain],
                                reinterpret_cast<LPVOID>(IDXGIFactory_CreateSwapChain_Streamline_Detour),
                                reinterpret_cast<LPVOID*>(&IDXGIFactory_CreateSwapChain_Streamline_Original),
                                "IDXGIFactory::CreateSwapChain (Streamline)")) {
            any_hooked = true;
            LogInfo("[Streamline] proxy factory CreateSwapChain hooked");
        }
    }

    Microsoft::WRL::ComPtr<IDXGIFactory1> ifactory1;
    hr = upgraded_iface->QueryInterface(IID_PPV_ARGS(&ifactory1));
    if (SUCCEEDED(hr) && ifactory1 != nullptr) {
        void** vtable1 = *reinterpret_cast<void***>(ifactory1.Get());
        if (CreateAndEnableHook(vtable1[IDXGIFactory1_CreateSwapChainForHwnd],
                                reinterpret_cast<LPVOID>(IDXGIFactory1_CreateSwapChainForHwnd_Streamline_Detour),
                                reinterpret_cast<LPVOID*>(&IDXGIFactory1_CreateSwapChainForHwnd_Streamline_Original),
                                "IDXGIFactory1::CreateSwapChainForHwnd (Streamline)")) {
            any_hooked = true;
            LogInfo("[Streamline] proxy factory CreateSwapChainForHwnd hooked");
        }
        if (CreateAndEnableHook(vtable1[IDXGIFactory1_CreateSwapChainForCoreWindow],
                                reinterpret_cast<LPVOID>(IDXGIFactory1_CreateSwapChainForCoreWindow_Streamline_Detour),
                                reinterpret_cast<LPVOID*>(&IDXGIFactory1_CreateSwapChainForCoreWindow_Streamline_Original),
                                "IDXGIFactory1::CreateSwapChainForCoreWindow (Streamline)")) {
            any_hooked = true;
            LogInfo("[Streamline] proxy factory CreateSwapChainForCoreWindow hooked");
        }
    }

    Microsoft::WRL::ComPtr<IDXGIFactory2> ifactory2;
    hr = upgraded_iface->QueryInterface(IID_PPV_ARGS(&ifactory2));
    if (SUCCEEDED(hr) && ifactory2 != nullptr) {
        void** vtable2 = *reinterpret_cast<void***>(ifactory2.Get());
        if (CreateAndEnableHook(vtable2[IDXGIFactory2_CreateSwapChainForComposition],
                                reinterpret_cast<LPVOID>(IDXGIFactory2_CreateSwapChainForComposition_Streamline_Detour),
                                reinterpret_cast<LPVOID*>(&IDXGIFactory2_CreateSwapChainForComposition_Streamline_Original),
                                "IDXGIFactory2::CreateSwapChainForComposition (Streamline)")) {
            any_hooked = true;
            LogInfo("[Streamline] proxy factory CreateSwapChainForComposition hooked");
        }
    }

    if (any_hooked) {
        streamline_proxy_factory_hooked = true;
        display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
            display_commanderhooks::HookType::SL_PROXY_DXGI_SWAPCHAIN);
    }
    return streamline_proxy_factory_hooked;
}

bool HookStreamlineProxySwapchain(IDXGISwapChain* swapchain) { return HookStreamlineProxySwapchainImpl(swapchain); }

}  // namespace display_commander::features::streamline
