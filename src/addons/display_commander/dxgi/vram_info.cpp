#include "vram_info.hpp"
#include "globals.hpp"
#include "utils/srwlock_wrapper.hpp"
#include <dxgi1_4.h>
#include <wrl/client.h>

namespace display_commander {
namespace dxgi {

namespace {

SRWLOCK g_vram_adapter_srwlock = SRWLOCK_INIT;
Microsoft::WRL::ComPtr<IDXGIAdapter3> g_vram_adapter;

// Returns true if adapter was obtained (cached or newly created). Uses shared factory and caches adapter 0.
bool GetOrCreateVramAdapter(IDXGIAdapter3** out_adapter) {
    if (out_adapter == nullptr) {
        return false;
    }
    // Fast path: use cached adapter under shared lock
    {
        utils::SRWLockShared lock(g_vram_adapter_srwlock);
        if (g_vram_adapter != nullptr) {
            *out_adapter = g_vram_adapter.Get();
            (*out_adapter)->AddRef();
            return true;
        }
    }
    // Slow path: create adapter once under exclusive lock
    utils::SRWLockExclusive lock(g_vram_adapter_srwlock);
    if (g_vram_adapter != nullptr) {
        *out_adapter = g_vram_adapter.Get();
        (*out_adapter)->AddRef();
        return true;
    }
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory = GetSharedDXGIFactory();
    if (factory == nullptr) {
        return false;
    }
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter1;
    HRESULT hr = factory->EnumAdapters1(0, &adapter1);
    if (FAILED(hr) || adapter1 == nullptr) {
        return false;
    }
    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3;
    hr = adapter1->QueryInterface(IID_PPV_ARGS(&adapter3));
    if (FAILED(hr) || adapter3 == nullptr) {
        return false;
    }
    g_vram_adapter = adapter3;
    *out_adapter = g_vram_adapter.Get();
    (*out_adapter)->AddRef();
    return true;
}

void ClearVramAdapterCache() {
    utils::SRWLockExclusive lock(g_vram_adapter_srwlock);
    g_vram_adapter.Reset();
}

}  // namespace

bool GetVramInfo(uint64_t* out_used_bytes, uint64_t* out_total_bytes) {
    if (out_used_bytes == nullptr || out_total_bytes == nullptr) {
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter;
    if (!GetOrCreateVramAdapter(adapter.GetAddressOf())) {
        return false;
    }

    DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
    HRESULT hr = adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info);
    if (FAILED(hr)) {
        ClearVramAdapterCache();
        return false;
    }

    *out_used_bytes = info.CurrentUsage;
    *out_total_bytes = info.Budget;
    return true;
}

}  // namespace dxgi
}  // namespace display_commander
