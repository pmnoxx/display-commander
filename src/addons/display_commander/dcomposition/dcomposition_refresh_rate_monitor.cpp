#include "dcomposition_refresh_rate_monitor.hpp"
#include "../utils/logging.hpp"
#include "../utils/srwlock_wrapper.hpp"
#include "../utils/timing.hpp"

#include <d3d11.h>
#include <dcomp.h>
#include <dcomptypes.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace display_commander::dcomposition {

namespace {

constexpr LONGLONG WINDOW_NS = utils::SEC_TO_NS;  // 1 second
constexpr unsigned POLL_MS = 2;

Microsoft::WRL::ComPtr<ID3D11Device> g_d3d11_device;
Microsoft::WRL::ComPtr<IDCompositionDevice> g_dcomp_device;
SRWLOCK g_dcomp_srwlock = SRWLOCK_INIT;
std::atomic<bool> g_active{false};

// Measured rate by counting refreshes (lastFrameTime changes)
LARGE_INTEGER g_prev_last_frame_time{};
uint64_t g_refresh_count = 0;
LONGLONG g_window_start_ns = 0;
std::atomic<double> g_measured_refresh_rate_hz{0.0};

std::thread g_monitor_thread;
std::atomic<bool> g_stop_monitor{false};

void MonitorThreadFunc() {
    while (!g_stop_monitor.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(POLL_MS));

        utils::SRWLockExclusive lock(g_dcomp_srwlock);
        if (g_dcomp_device.Get() == nullptr) {
            break;
        }

        DCOMPOSITION_FRAME_STATISTICS stats = {};
        HRESULT hr = g_dcomp_device->GetFrameStatistics(&stats);
        if (FAILED(hr)) {
            continue;
        }

        if (stats.lastFrameTime.QuadPart != g_prev_last_frame_time.QuadPart) {
            g_refresh_count++;
            g_prev_last_frame_time = stats.lastFrameTime;
        }

        LONGLONG now_ns = utils::get_now_ns();
        if (g_window_start_ns == 0) {
            g_window_start_ns = now_ns;
        }
        LONGLONG elapsed_ns = now_ns - g_window_start_ns;
        if (elapsed_ns >= WINDOW_NS) {
            double window_sec = static_cast<double>(elapsed_ns) / 1e9;
            g_measured_refresh_rate_hz.store(static_cast<double>(g_refresh_count) / window_sec,
                                             std::memory_order_relaxed);
            g_refresh_count = 0;
            g_window_start_ns = now_ns;
        }
    }
}

}  // namespace

void StartDCompRefreshRateMonitoring() {
    utils::SRWLockExclusive lock(g_dcomp_srwlock);
    if (g_dcomp_device.Get() != nullptr) {
        g_active = true;
        return;
    }

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    D3D_FEATURE_LEVEL feature_level;
    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &device,
        &feature_level,
        nullptr);
    if (FAILED(hr) || device.Get() == nullptr) {
        LogError("DComp: D3D11CreateDevice failed: 0x%08x", static_cast<unsigned>(hr));
        return;
    }

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
    hr = device.As(&dxgi_device);
    if (FAILED(hr) || dxgi_device.Get() == nullptr) {
        LogError("DComp: ID3D11Device::As(IDXGIDevice) failed: 0x%08x", static_cast<unsigned>(hr));
        return;
    }

    Microsoft::WRL::ComPtr<IDCompositionDevice> dcomp_device;
    hr = DCompositionCreateDevice(dxgi_device.Get(), __uuidof(IDCompositionDevice),
                                  reinterpret_cast<void**>(dcomp_device.GetAddressOf()));
    if (FAILED(hr) || dcomp_device.Get() == nullptr) {
        LogError("DComp: DCompositionCreateDevice failed: 0x%08x", static_cast<unsigned>(hr));
        return;
    }

    g_d3d11_device = device;
    g_dcomp_device = dcomp_device;
    g_active = true;

    g_prev_last_frame_time.QuadPart = 0;
    g_refresh_count = 0;
    g_window_start_ns = 0;
    g_measured_refresh_rate_hz.store(0.0, std::memory_order_relaxed);
    g_stop_monitor.store(false, std::memory_order_relaxed);
    g_monitor_thread = std::thread(MonitorThreadFunc);

    LogInfo("DComp: refresh rate monitoring started");
}

void StopDCompRefreshRateMonitoring() {
    g_stop_monitor.store(true, std::memory_order_relaxed);
    if (g_monitor_thread.joinable()) {
        g_monitor_thread.join();
    }

    utils::SRWLockExclusive lock(g_dcomp_srwlock);
    g_active = false;
    g_dcomp_device.Reset();
    g_d3d11_device.Reset();
    g_measured_refresh_rate_hz.store(0.0, std::memory_order_relaxed);
    LogInfo("DComp: refresh rate monitoring stopped");
}

bool IsDCompRefreshRateMonitoringActive() {
    return g_active && g_dcomp_device.Get() != nullptr;
}

double GetDCompCompositionRateHz() {
    utils::SRWLockExclusive lock(g_dcomp_srwlock);
    if (g_dcomp_device.Get() == nullptr) {
        return 0.0;
    }

    DCOMPOSITION_FRAME_STATISTICS stats = {};
    HRESULT hr = g_dcomp_device->GetFrameStatistics(&stats);
    if (FAILED(hr)) {
        return 0.0;
    }

    if (stats.currentCompositionRate.Denominator == 0) {
        return 0.0;
    }
    return static_cast<double>(stats.currentCompositionRate.Numerator) /
           static_cast<double>(stats.currentCompositionRate.Denominator);
}

double GetDCompMeasuredRefreshRateHz() {
    return g_measured_refresh_rate_hz.load(std::memory_order_relaxed);
}

}  // namespace display_commander::dcomposition
