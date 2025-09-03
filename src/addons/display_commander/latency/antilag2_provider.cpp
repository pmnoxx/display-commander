#include "antilag2_provider.hpp"
#include "../globals.hpp"
#include "../utils.hpp"

AntiLag2Provider::AntiLag2Provider()
    : dx11_initialized_(false)
    , dx12_initialized_(false)
    , initialized_(false)
    , frame_id_(0)
    , low_latency_mode_(false)
    , boost_mode_(false)
    , use_markers_(false)
    , target_fps_(0.0f)
    , current_api_(APIType::None)
{
    // Initialize contexts to zero
    memset(&dx11_context_, 0, sizeof(dx11_context_));
    memset(&dx12_context_, 0, sizeof(dx12_context_));
}

AntiLag2Provider::~AntiLag2Provider() {
    Shutdown();
}

bool AntiLag2Provider::Initialize(reshade::api::device* device) {
    if (initialized_.load(std::memory_order_acquire)) {
        return true; // Already initialized
    }

    if (!device) {
        LogWarn("AntiLag2Provider: Invalid device provided");
        return false;
    }

    // Get native D3D device
    IUnknown* native_device = GetNativeD3DDeviceFromReshade(device);
    if (!native_device) {
        LogWarn("AntiLag2Provider: Failed to get native D3D device");
        return false;
    }

    // Determine which API to use
    bool is_d3d12 = IsD3D12Device(device);

    HRESULT hr = E_FAIL;

    if (is_d3d12) {
        // Try D3D12 first - cast to ID3D12Device*
        ID3D12Device* d3d12_device = nullptr;
        HRESULT query_hr = native_device->QueryInterface(IID_PPV_ARGS(&d3d12_device));
        if (SUCCEEDED(query_hr) && d3d12_device) {
            hr = AMD::AntiLag2DX12::Initialize(&dx12_context_, d3d12_device);
            d3d12_device->Release(); // Release the reference we got from QueryInterface
            if (SUCCEEDED(hr)) {
                dx12_initialized_ = true;
                current_api_ = APIType::D3D12;
                s_antilag2_state.store(AMD_AntiLag2_State::D3D12_Success, std::memory_order_release);
                LogInfo("AntiLag2Provider: Initialized with D3D12");
            } else {
                s_antilag2_state.store(AMD_AntiLag2_State::D3D12_Failed, std::memory_order_release);
                LogWarn("AntiLag2Provider: D3D12 initialization failed (0x%08X)", hr);
            }
        } else {
            s_antilag2_state.store(AMD_AntiLag2_State::D3D12_Failed, std::memory_order_release);
            LogWarn("AntiLag2Provider: Failed to query ID3D12Device interface");
        }
    } else {
        // Try D3D11
        hr = AMD::AntiLag2DX11::Initialize(&dx11_context_);
        if (SUCCEEDED(hr)) {
            dx11_initialized_ = true;
            current_api_ = APIType::D3D11;
            s_antilag2_state.store(AMD_AntiLag2_State::D3D11_Success, std::memory_order_release);
            LogInfo("AntiLag2Provider: Initialized with D3D11");
        } else {
            s_antilag2_state.store(AMD_AntiLag2_State::D3D11_Failed, std::memory_order_release);
            LogWarn("AntiLag2Provider: D3D11 initialization failed (0x%08X)", hr);
        }
    }

    if (SUCCEEDED(hr)) {
        initialized_.store(true, std::memory_order_release);
        return true;
    }

    // If we get here, both APIs failed, but we already set the appropriate failure state above
    LogWarn("AntiLag2Provider: Failed to initialize with any API");
    return false;
}

void AntiLag2Provider::Shutdown() {
    if (!initialized_.exchange(false, std::memory_order_release)) {
        return; // Already shutdown
    }

    if (dx11_initialized_) {
        AMD::AntiLag2DX11::DeInitialize(&dx11_context_);
        dx11_initialized_ = false;
        memset(&dx11_context_, 0, sizeof(dx11_context_));
    }

    if (dx12_initialized_) {
        AMD::AntiLag2DX12::DeInitialize(&dx12_context_);
        dx12_initialized_ = false;
        memset(&dx12_context_, 0, sizeof(dx12_context_));
    }

    current_api_ = APIType::None;
    frame_id_.store(0, std::memory_order_release);
    s_antilag2_state.store(AMD_AntiLag2_State::NotTried, std::memory_order_release);

    LogInfo("AntiLag2Provider: Shutdown complete");
}

bool AntiLag2Provider::IsInitialized() const {
    return initialized_.load(std::memory_order_acquire) &&
           (dx11_initialized_ || dx12_initialized_);
}

uint64_t AntiLag2Provider::IncreaseFrameId() {
    if (!IsInitialized()) return 0;
    return frame_id_.fetch_add(1, std::memory_order_acq_rel);
}

bool AntiLag2Provider::SetMarker(LatencyMarkerType marker) {
    if (!IsInitialized()) return false;

    // AMD Anti-Lag 2 doesn't have explicit marker support like NVIDIA Reflex
    // The markers are handled internally by the Update() call timing
    // We'll just increment frame ID and return success
    IncreaseFrameId();
    return true;
}

bool AntiLag2Provider::ApplySleepMode(bool low_latency, bool boost, bool use_markers) {
    if (!IsInitialized()) return false;

    // Store the configuration for use in Update() calls
    low_latency_mode_ = low_latency;
    boost_mode_ = boost;
    use_markers_ = use_markers;

    return true;
}

bool AntiLag2Provider::Sleep() {
    if (!IsInitialized()) return false;

    // AMD Anti-Lag 2 doesn't have a separate sleep call like NVIDIA Reflex
    // The sleep functionality is integrated into the Update() call
    // This is a no-op for AMD Anti-Lag 2
    return true;
}

bool AntiLag2Provider::Update() {
    if (!IsInitialized()) return false;

    // Calculate target FPS for frame limiter (0 = disabled)
    unsigned int max_fps = 0;
    if (target_fps_ > 0.0f) {
        max_fps = static_cast<unsigned int>(target_fps_);
    }

    HRESULT hr = E_FAIL;

    if (current_api_ == APIType::D3D11 && dx11_initialized_) {
        hr = AMD::AntiLag2DX11::Update(&dx11_context_, low_latency_mode_, max_fps);
    } else if (current_api_ == APIType::D3D12 && dx12_initialized_) {
        hr = AMD::AntiLag2DX12::Update(&dx12_context_, low_latency_mode_, max_fps);
    } else {
        LogWarn("AntiLag2Provider: Update called but no valid API context");
        return false;
    }

    if (FAILED(hr)) {
        LogWarn("AntiLag2Provider: Update failed (0x%08X)", hr);
        return false;
    }

    return true;
}

void AntiLag2Provider::SetTargetFPS(float fps) {
    target_fps_ = fps;
}

IUnknown* AntiLag2Provider::GetNativeD3DDeviceFromReshade(reshade::api::device* device) {
    if (!device) return nullptr;
    const uint64_t native = device->get_native();
    return reinterpret_cast<IUnknown*>(native);
}

bool AntiLag2Provider::IsD3D12Device(reshade::api::device* device) {
    if (!device) return false;

    // Check if the device is D3D12 by trying to query for D3D12 interfaces
    IUnknown* native_device = GetNativeD3DDeviceFromReshade(device);
    if (!native_device) return false;

    // Try to query for ID3D12Device interface
    ID3D12Device* d3d12_device = nullptr;
    HRESULT hr = native_device->QueryInterface(IID_PPV_ARGS(&d3d12_device));
    if (SUCCEEDED(hr) && d3d12_device) {
        d3d12_device->Release();
        return true;
    }

    return false;
}
