#include "hdr_upgrade.hpp"
#include "../globals.hpp"
#include "../utils/logging.hpp"
#include "../utils/srwlock_registry.hpp"
#include "../utils/srwlock_wrapper.hpp"

#include <algorithm>
#include <dxgi1_4.h>
#include <dxgi1_6.h>
#include <unordered_set>
#include <wrl/client.h>

namespace display_commander::hdr_upgrade {

namespace {

int ComputeIntersectionArea(int ax1, int ay1, int ax2, int ay2, int bx1, int by1, int bx2, int by2) {
    const int w = (std::max)(0, (std::min)(ax2, bx2) - (std::max)(ax1, bx1));
    const int h = (std::max)(0, (std::min)(ay2, by2) - (std::max)(ay1, by1));
    return w * h;
}

bool CheckDisplayHdrSupport(IDXGIFactory1* factory, HWND hwnd) {
    if (factory == nullptr || hwnd == nullptr) return false;
    IDXGIAdapter* adapter = nullptr;
    if (FAILED(factory->EnumAdapters(0, &adapter)) || adapter == nullptr) return false;
    IDXGIOutput* best_output = nullptr;
    int best_intersect = -1;
    RECT rect = {};
    if (!GetWindowRect(hwnd, &rect)) {
        adapter->Release();
        return false;
    }
    const int ax1 = rect.left, ay1 = rect.top, ax2 = rect.right, ay2 = rect.bottom;
    for (UINT i = 0;; ++i) {
        IDXGIOutput* output = nullptr;
        if (adapter->EnumOutputs(i, &output) == DXGI_ERROR_NOT_FOUND) break;
        if (output == nullptr) continue;
        DXGI_OUTPUT_DESC out_desc = {};
        if (FAILED(output->GetDesc(&out_desc))) {
            output->Release();
            continue;
        }
        const RECT& r = out_desc.DesktopCoordinates;
        int area = ComputeIntersectionArea(ax1, ay1, ax2, ay2, r.left, r.top, r.right, r.bottom);
        if (area > best_intersect) {
            if (best_output) best_output->Release();
            best_output = output;
            best_intersect = area;
        } else {
            output->Release();
        }
    }
    adapter->Release();
    if (best_output == nullptr) return false;
    IDXGIOutput6* output6 = nullptr;
    bool supported = false;
    if (SUCCEEDED(best_output->QueryInterface(__uuidof(IDXGIOutput6), reinterpret_cast<void**>(&output6)))) {
        DXGI_OUTPUT_DESC1 desc1 = {};
        if (SUCCEEDED(output6->GetDesc1(&desc1)))
            supported = (desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
        output6->Release();
    }
    best_output->Release();
    return supported;
}

std::unordered_set<uint64_t> g_back_buffers;
std::atomic<bool> g_display_hdr_support_cached{false};
std::atomic<bool> g_display_hdr_supported{false};

}  // namespace

bool ModifyCreateSwapchainDesc(reshade::api::device_api api, reshade::api::swapchain_desc& desc, bool use_hdr10) {
    if (api != reshade::api::device_api::d3d10 && api != reshade::api::device_api::d3d11
        && api != reshade::api::device_api::d3d12)
        return false;
    if (use_hdr10) {
        desc.back_buffer.texture.format = reshade::api::format::r10g10b10a2_unorm;
        LogInfo("HdrUpgrade: create_swapchain desc modified to HDR10 (R10G10B10A2_UNORM), FLIP_DISCARD, ALLOW_TEARING");
    } else {
        desc.back_buffer.texture.format = reshade::api::format::r16g16b16a16_float;
        LogInfo("HdrUpgrade: create_swapchain desc modified to scRGB (R16G16B16A16_FLOAT), FLIP_DISCARD, ALLOW_TEARING");
    }
    if (desc.back_buffer_count < 2) desc.back_buffer_count = 2;
    desc.present_mode = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.present_flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    return true;
}

void OnInitSwapchain(reshade::api::swapchain* swapchain, bool resize, bool use_hdr10) {
    if (swapchain == nullptr) return;
    const auto api = swapchain->get_device()->get_api();
    if (api != reshade::api::device_api::d3d11 && api != reshade::api::device_api::d3d12) return;

    IUnknown* native = reinterpret_cast<IUnknown*>(swapchain->get_native());
    if (native == nullptr) return;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> sc4;
    if (FAILED(native->QueryInterface(IID_PPV_ARGS(&sc4)))) return;

    HWND hwnd = static_cast<HWND>(swapchain->get_hwnd());
    if (!g_display_hdr_support_cached.load(std::memory_order_relaxed)) {
        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
        if (SUCCEEDED(sc4->GetParent(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(factory.GetAddressOf())))) {
            g_display_hdr_supported.store(CheckDisplayHdrSupport(factory.Get(), hwnd), std::memory_order_relaxed);
        }
        g_display_hdr_support_cached.store(true, std::memory_order_relaxed);
    }
    if (!g_display_hdr_supported.load(std::memory_order_relaxed)) {
        LogInfo("HdrUpgrade: init_swapchain skipped - display HDR not supported (enable Windows HDR or use HDR display)");
        return;
    }

    DXGI_SWAP_CHAIN_DESC1 desc1 = {};
    if (FAILED(sc4->GetDesc1(&desc1))) return;

    const DXGI_FORMAT new_format =
        use_hdr10 ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R16G16B16A16_FLOAT;
    const DXGI_COLOR_SPACE_TYPE new_color_space =
        use_hdr10 ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 : DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;

    if (desc1.Format != new_format) {
        HRESULT hr = sc4->ResizeBuffers(desc1.BufferCount, desc1.Width, desc1.Height, new_format, desc1.Flags);
        if (FAILED(hr)) {
            LogInfo("HdrUpgrade: ResizeBuffers to %s failed 0x%lx", use_hdr10 ? "HDR10" : "scRGB",
                    static_cast<unsigned long>(hr));
            return;
        }
        LogInfo("HdrUpgrade: ResizeBuffers to %s succeeded", use_hdr10 ? "R10G10B10A2_UNORM (HDR10)" : "R16G16B16A16_FLOAT (scRGB)");
    }

    UINT support = 0;
    if (FAILED(sc4->CheckColorSpaceSupport(new_color_space, &support))
        || (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == 0) {
        LogInfo("HdrUpgrade: %s color space not supported for present", use_hdr10 ? "HDR10" : "scRGB");
        return;
    }
    if (FAILED(sc4->SetColorSpace1(new_color_space))) {
        LogInfo("HdrUpgrade: SetColorSpace1(%s) failed", use_hdr10 ? "HDR10" : "scRGB");
        return;
    }

    reshade::api::effect_runtime* runtime = GetFirstReShadeRuntime();
    if (runtime != nullptr) {
        runtime->set_color_space(use_hdr10 ? reshade::api::color_space::hdr10_st2084
                                           : reshade::api::color_space::extended_srgb_linear);
    }

    // Track back buffers after ResizeBuffers so handles are current
    {
        utils::SRWLockExclusive lock(utils::g_hdr_upgrade_back_buffers_lock);
        for (uint32_t i = 0; i < swapchain->get_back_buffer_count(); ++i)
            g_back_buffers.insert(swapchain->get_back_buffer(i).handle);
    }
    LogInfo("HdrUpgrade: init_swapchain applied %s format and color space", use_hdr10 ? "HDR10" : "scRGB");
}

void OnDestroySwapchain(reshade::api::swapchain* swapchain, bool resize) {
    if (swapchain == nullptr) return;
    utils::SRWLockExclusive lock(utils::g_hdr_upgrade_back_buffers_lock);
    for (uint32_t i = 0; i < swapchain->get_back_buffer_count(); ++i)
        g_back_buffers.erase(swapchain->get_back_buffer(i).handle);
}

bool OnCreateResourceView(reshade::api::device* device, reshade::api::resource resource,
                          reshade::api::resource_usage usage_type, reshade::api::resource_view_desc& desc) {
    if (device == nullptr) return false;
    bool is_back_buffer = false;
    {
        utils::SRWLockShared lock(utils::g_hdr_upgrade_back_buffers_lock);
        is_back_buffer = (g_back_buffers.count(resource.handle) != 0);
    }
    if (!is_back_buffer) return false;
    const reshade::api::resource_desc res_desc = device->get_resource_desc(resource);
    if (res_desc.texture.format == reshade::api::format::r16g16b16a16_float) {
        desc.format = reshade::api::format::r16g16b16a16_float;
        return true;
    }
    if (res_desc.texture.format == reshade::api::format::r10g10b10a2_unorm) {
        desc.format = reshade::api::format::r10g10b10a2_unorm;
        return true;
    }
    return false;
}

}  // namespace display_commander::hdr_upgrade
