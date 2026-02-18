#include "vram_info.hpp"
#include <dxgi1_4.h>
#include <wrl/client.h>

namespace display_commander {
namespace dxgi {

bool GetVramInfo(uint64_t* out_used_bytes, uint64_t* out_total_bytes) {
    if (out_used_bytes == nullptr || out_total_bytes == nullptr) {
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr) || factory == nullptr) {
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter1;
    hr = factory->EnumAdapters1(0, &adapter1);
    if (FAILED(hr) || adapter1 == nullptr) {
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3;
    hr = adapter1->QueryInterface(IID_PPV_ARGS(&adapter3));
    if (FAILED(hr) || adapter3 == nullptr) {
        return false;
    }

    DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
    hr = adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info);
    if (FAILED(hr)) {
        return false;
    }

    *out_used_bytes = info.CurrentUsage;
    *out_total_bytes = info.Budget;
    return true;
}

}  // namespace dxgi
}  // namespace display_commander
