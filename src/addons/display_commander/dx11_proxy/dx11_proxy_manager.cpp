/**
 * DX11 Proxy Device Manager Implementation
 */

#include "dx11_proxy_manager.hpp"
#include "dx11_proxy_shared_resources.hpp"
#include "dx11_proxy_settings.hpp"
#include "../utils.hpp"
#include "../globals.hpp"
#include <sstream>
#include <chrono>
#include <thread>
#include <algorithm>
#include <vector>

namespace dx11_proxy {

bool DX11ProxyManager::Initialize(HWND game_hwnd, uint32_t width, uint32_t height, bool create_swapchain) {
    std::lock_guard lock(mutex_);

    if (is_initialized_.load()) {
        LogInfo("DX11ProxyManager::Initialize: Already initialized, shutting down first");
        CleanupResources();
    }

    LogInfo("DX11ProxyManager::Initialize: Starting initialization");

    game_hwnd_ = game_hwnd;
    swapchain_width_ = width;
    swapchain_height_ = height;

    // Step 1: Create DX11 device
    if (!CreateDevice()) {
        LogError("DX11ProxyManager::Initialize: Failed to create DX11 device");
        CleanupResources();
        return false;
    }

    // Step 2: Create swapchain (optional)
    if (create_swapchain) {
        if (!game_hwnd) {
            LogError("DX11ProxyManager::Initialize: HWND required for swapchain creation");
            CleanupResources();
            return false;
        }

        if (!CreateSwapChain(game_hwnd, width, height)) {
            LogWarn("DX11ProxyManager::Initialize: Failed to create swapchain (window may already have one)");
            LogInfo("DX11ProxyManager::Initialize: Continuing with device-only mode");
            // Don't fail - we can still use the device without our own swapchain
        }
    } else {
        LogInfo("DX11ProxyManager::Initialize: Swapchain creation skipped (device-only mode)");
    }

    is_initialized_.store(true);
    initialization_count_.fetch_add(1);

    std::stringstream ss;
    ss << "DX11ProxyManager::Initialize: Success! Device created";
    if (swapchain_) {
        ss << ", swapchain " << width << "x" << height;
    } else {
        ss << " (device-only mode, no swapchain)";
    }
    LogInfo(ss.str().c_str());

    return true;
}

void DX11ProxyManager::Shutdown() {
    std::lock_guard lock(mutex_);

    if (!is_initialized_.load()) {
        return;
    }

    LogInfo("DX11ProxyManager::Shutdown: Cleaning up resources");

    // Stop copy thread first
    StopCopyThread();

    // Clean up shared resources first
    SharedResourceManager::GetInstance().Cleanup();

    CleanupResources();

    is_initialized_.store(false);
    shutdown_count_.fetch_add(1);

    LogInfo("DX11ProxyManager::Shutdown: Complete");
}

bool DX11ProxyManager::CreateDevice() {
    LogInfo("DX11ProxyManager::CreateDevice: Creating D3D11 device");

    // Load D3D11 functions dynamically (following the pattern from directx.hpp)
    HMODULE d3d11_module = LoadLibraryW(L"d3d11.dll");
    if (!d3d11_module) {
        LogError("DX11ProxyManager::CreateDevice: Failed to load d3d11.dll");
        return false;
    }

    auto pD3D11CreateDevice = reinterpret_cast<decltype(&D3D11CreateDevice)>(
        GetProcAddress(d3d11_module, "D3D11CreateDevice"));
    if (!pD3D11CreateDevice) {
        LogError("DX11ProxyManager::CreateDevice: Failed to get D3D11CreateDevice function");
        return false;
    }

    // Create device with hardware acceleration
    D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    UINT create_flags = 0;
#ifdef _DEBUG
    create_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL feature_level_out;
    HRESULT hr = pD3D11CreateDevice(
        nullptr,                    // Adapter (null = default)
        D3D_DRIVER_TYPE_HARDWARE,   // Driver type
        nullptr,                    // Software rasterizer module
        create_flags,               // Flags
        feature_levels,             // Feature levels to try
        ARRAYSIZE(feature_levels),  // Number of feature levels
        D3D11_SDK_VERSION,          // SDK version
        &device_,                   // Output device
        &feature_level_out,         // Output feature level
        &context_                   // Output context
    );

    if (FAILED(hr)) {
        std::stringstream ss;
        ss << "DX11ProxyManager::CreateDevice: D3D11CreateDevice failed with HRESULT 0x"
           << std::hex << hr;
        LogError(ss.str().c_str());
        return false;
    }

    std::stringstream ss;
    ss << "DX11ProxyManager::CreateDevice: Success! Feature level: 0x"
       << std::hex << feature_level_out;
    LogInfo(ss.str().c_str());

    return true;
}

bool DX11ProxyManager::CreateSwapChain(HWND hwnd, uint32_t width, uint32_t height) {
    LogInfo("DX11ProxyManager::CreateSwapChain: Creating DXGI swapchain");

    // Load DXGI functions dynamically
    HMODULE dxgi_module = LoadLibraryW(L"dxgi.dll");
    if (!dxgi_module) {
        LogError("DX11ProxyManager::CreateSwapChain: Failed to load dxgi.dll");
        return false;
    }

    auto pCreateDXGIFactory2 = reinterpret_cast<decltype(&CreateDXGIFactory2)>(
        GetProcAddress(dxgi_module, "CreateDXGIFactory2"));
    if (!pCreateDXGIFactory2) {
        LogError("DX11ProxyManager::CreateSwapChain: Failed to get CreateDXGIFactory2");
        return false;
    }

    // Create DXGI factory
    HRESULT hr = pCreateDXGIFactory2(0, IID_PPV_ARGS(&factory_));
    if (FAILED(hr)) {
        std::stringstream ss;
        ss << "DX11ProxyManager::CreateSwapChain: CreateDXGIFactory2 failed with HRESULT 0x"
           << std::hex << hr;
        LogError(ss.str().c_str());
        return false;
    }

    // Get format from settings
    int format_index = g_dx11ProxySettings.swapchain_format.load();
    swapchain_format_ = GetFormatFromIndex(format_index);

    // Configure swapchain description for modern flip model
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.Format = swapchain_format_;
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2; // Double buffering minimum for flip model
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // Modern flip model
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING; // Enable tearing for VRR

    // Get DXGI device from D3D11 device
    ComPtr<IDXGIDevice> dxgi_device;
    hr = device_.As(&dxgi_device);
    if (FAILED(hr)) {
        LogError("DX11ProxyManager::CreateSwapChain: Failed to get IDXGIDevice");
        return false;
    }

    // Create swapchain
    ComPtr<IDXGISwapChain1> swapchain1;
    hr = factory_->CreateSwapChainForHwnd(
        dxgi_device.Get(),
        hwnd,
        &desc,
        nullptr, // Fullscreen desc (null = windowed)
        nullptr, // Restrict output (null = no restriction)
        &swapchain1
    );

    if (FAILED(hr)) {
        std::stringstream ss;
        ss << "DX11ProxyManager::CreateSwapChain: CreateSwapChainForHwnd failed with HRESULT 0x"
           << std::hex << hr;
        LogError(ss.str().c_str());
        return false;
    }

    // Get IDXGISwapChain interface
    hr = swapchain1.As(&swapchain_);
    if (FAILED(hr)) {
        LogError("DX11ProxyManager::CreateSwapChain: Failed to get IDXGISwapChain");
        return false;
    }

    // Disable Alt+Enter fullscreen switching (we'll handle this ourselves)
    factory_->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    std::stringstream ss;
    ss << "DX11ProxyManager::CreateSwapChain: Success! " << width << "x" << height
       << ", format: " << swapchain_format_;
    LogInfo(ss.str().c_str());

    return true;
}

void DX11ProxyManager::CleanupResources() {
    // Stop copy thread if running
    StopCopyThread();

    // Cleanup shared resources
    CleanupSharedResources();

    // Release in reverse order of creation
    source_swapchain_.Reset();
    swapchain_.Reset();
    factory_.Reset();
    context_.Reset();
    device_.Reset();

    game_hwnd_ = nullptr;
    swapchain_width_ = 0;
    swapchain_height_ = 0;
}

DX11ProxyManager::Stats DX11ProxyManager::GetStats() const {
    Stats stats;
    stats.frames_generated = frames_generated_.load();
    stats.frames_presented = frames_presented_.load();
    stats.frames_copied = frames_copied_.load();
    stats.initialization_count = initialization_count_.load();
    stats.shutdown_count = shutdown_count_.load();
    stats.is_initialized = is_initialized_.load();
    stats.copy_thread_running = copy_thread_running_.load();
    stats.swapchain_width = swapchain_width_;
    stats.swapchain_height = swapchain_height_;
    stats.swapchain_format = swapchain_format_;
    stats.has_swapchain = (swapchain_ != nullptr);
    return stats;
}

void DX11ProxyManager::StartCopyThread(IDXGISwapChain* source_swapchain) {
    if (!source_swapchain) {
        LogError("DX11ProxyManager::StartCopyThread: Invalid source swapchain");
        return;
    }

    if (copy_thread_running_.load()) {
        LogInfo("DX11ProxyManager::StartCopyThread: Copy thread already running");
        return;
    }

    // Store source swapchain
    source_swapchain_ = source_swapchain;

    // Check if we need shared resources (different devices)
    ComPtr<ID3D11Texture2D> source_backbuffer;
    HRESULT hr = source_swapchain->GetBuffer(0, IID_PPV_ARGS(&source_backbuffer));
    if (SUCCEEDED(hr)) {
        ComPtr<ID3D11Device> source_device;
        source_backbuffer->GetDevice(&source_device);

        if (source_device.Get() != device_.Get()) {
            LogInfo("DX11ProxyManager::StartCopyThread: Different devices detected, initializing shared resources");
            use_shared_resources_ = true;

            // Initialize shared resources
            if (!InitializeSharedResources(source_swapchain)) {
                LogError("DX11ProxyManager::StartCopyThread: Failed to initialize shared resources");
                use_shared_resources_ = false;
                return;
            }
        } else {
            LogInfo("DX11ProxyManager::StartCopyThread: Same device, using direct copy");
            use_shared_resources_ = false;
        }
    }

    // Start copy thread
    copy_thread_running_.store(true);
    copy_thread_ = std::thread([this]() { CopyThreadLoop(); });

    LogInfo("DX11ProxyManager::StartCopyThread: Copy thread started (shared resources: %s)",
            use_shared_resources_ ? "enabled" : "disabled");
}

void DX11ProxyManager::StopCopyThread() {
    if (!copy_thread_running_.load()) {
        return;
    }

    LogInfo("DX11ProxyManager::StopCopyThread: Stopping copy thread");

    // Signal thread to stop
    copy_thread_running_.store(false);

    // Wait for thread to finish
    if (copy_thread_.joinable()) {
        copy_thread_.join();
    }

    // Clear source swapchain reference
    source_swapchain_.Reset();

    LogInfo("DX11ProxyManager::StopCopyThread: Copy thread stopped");
}

void DX11ProxyManager::CopyThreadLoop2() {

    if (copy_thread_running_.load()) {
        LogInfo("[COPY-THREAD] Copy thread loop started");
        // Copy one frame
        if (CopyFrame()) {
            frames_copied_.fetch_add(1);
        }

        // Sleep for 1 second (1000 milliseconds)
       // std::this_thread::sleep_for(std::chrono::milliseconds(10));
        LogInfo("[COPY-THREAD] Copy thread loop exiting");
    }

}
void DX11ProxyManager::CopyThreadLoop() {
    /*LogInfo("[COPY-THREAD] Copy thread loop started");

    if (copy_thread_running_.load()) {
        // Copy one frame
        if (CopyFrame()) {
            frames_copied_.fetch_add(1);
        }

        // Sleep for 1 second (1000 milliseconds)
       // std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    LogInfo("[COPY-THREAD] Copy thread loop exiting");*/
}

bool DX11ProxyManager::CopyFrame() {
    // Ensure we have valid swapchains
    if (!source_swapchain_ || !swapchain_) {
        LogWarn("DX11ProxyManager::CopyFrame: Missing swapchains - source: %p, proxy: %p",
                source_swapchain_.Get(), swapchain_.Get());
        return false;
    }

    if (!device_ || !context_) {
        LogWarn("DX11ProxyManager::CopyFrame: Missing device/context - device: %p, context: %p",
                device_.Get(), context_.Get());
        return false;
    }

    HRESULT hr;

    // Get back buffer from source swapchain
    ComPtr<ID3D11Texture2D> source_backbuffer;
    hr = source_swapchain_->GetBuffer(0, IID_PPV_ARGS(&source_backbuffer));
    if (FAILED(hr)) {
        std::stringstream ss;
        ss << "DX11ProxyManager::CopyFrame: Failed to get source backbuffer, HRESULT 0x" << std::hex << hr;
        LogError(ss.str().c_str());
        return false;
    }

    // Get back buffer from destination swapchain
    ComPtr<ID3D11Texture2D> dest_backbuffer;
    hr = swapchain_->GetBuffer(0, IID_PPV_ARGS(&dest_backbuffer));
    if (FAILED(hr)) {
        LogError("DX11ProxyManager::CopyFrame: Failed to get destination backbuffer");
        return false;
    }

    // Debug: Log texture info
    D3D11_TEXTURE2D_DESC source_desc, dest_desc;
    source_backbuffer->GetDesc(&source_desc);
    dest_backbuffer->GetDesc(&dest_desc);

    LogInfo("DX11ProxyManager::CopyFrame: Copying %dx%d (%d) -> %dx%d (%d), shared_resources: %s",
            source_desc.Width, source_desc.Height, source_desc.Format,
            dest_desc.Width, dest_desc.Height, dest_desc.Format,
            use_shared_resources_ ? "enabled" : "disabled");

    // Use shared resources if devices are different (RenoDX strategy)
    if (use_shared_resources_) {
        if (!shared_texture_ || !shared_handle_ || !source_context_) {
            LogError("DX11ProxyManager::CopyFrame: Shared resources not initialized");
            return false;
        }

        // RenoDX Strategy: Open shared resource on source device (created on proxy device)
        ComPtr<ID3D11Texture2D> shared_texture_on_source;
        hr = source_device_->OpenSharedResource(shared_handle_, IID_PPV_ARGS(&shared_texture_on_source));
        if (FAILED(hr)) {
            std::stringstream ss;
            ss << "DX11ProxyManager::CopyFrame: Failed to open shared resource on source device, HRESULT 0x" << std::hex << hr;
            LogError(ss.str().c_str());
            return false;
        }

        // Step 1: Copy from game's backbuffer to shared texture (on game's device)
        source_context_->CopyResource(shared_texture_on_source.Get(), source_backbuffer.Get());

        // Flush to ensure the copy completes before the next step
        source_context_->Flush();

        // Use query-based fence instead of sleep for better synchronization
        // This ensures the copy operation on the source device completes before
        // proceeding to copy on the destination device, preventing race conditions
        if (!source_copy_query_) {
            // Create query object if not already created (reused for performance)
            D3D11_QUERY_DESC query_desc = {};
            query_desc.Query = D3D11_QUERY_EVENT;
            query_desc.MiscFlags = 0;

            HRESULT hr = source_device_->CreateQuery(&query_desc, &source_copy_query_);
            if (FAILED(hr)) {
                LogWarn("DX11ProxyManager::CopyFrame: Failed to create query, falling back to sleep");
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }

        if (source_copy_query_) {
            // Insert query after the copy operation to track completion
            source_context_->End(source_copy_query_.Get());
            source_context_->Flush();

            // Wait for the query to complete (fence-like behavior)
            // This blocks until the GPU has finished the copy operation
            BOOL query_data = FALSE;
            while (source_context_->GetData(source_copy_query_.Get(), &query_data, sizeof(query_data), 0) == S_FALSE) {
                // Query not ready yet, yield CPU time instead of busy waiting
                std::this_thread::yield();
            }
        }
        // Step 2: Copy from shared texture to our backbuffer (on our device)
        context_->CopyResource(dest_backbuffer.Get(), shared_texture_.Get());

        // Flush to ensure the copy completes before present
        context_->Flush();
    } else {
        // Direct copy - same device
    //    LogInfo("DX11ProxyManager::CopyFrame: Performing direct copy (same device)");
      //  context_->CopyResource(dest_backbuffer.Get(), source_backbuffer.Get());

        // Flush to ensure the copy completes before present
   //     context_->Flush();
    //    LogInfo("DX11ProxyManager::CopyFrame: Direct copy completed and flushed");
    }

    // Present the copied frame
    LogInfo("DX11ProxyManager::CopyFrame: Presenting frame...");
    hr = swapchain_->Present(0, 0);
    if (FAILED(hr)) {
        std::stringstream ss;
        ss << "DX11ProxyManager::CopyFrame: Present failed with HRESULT 0x" << std::hex << hr;
        LogWarn(ss.str().c_str());
        return false;
    }

    LogInfo("DX11ProxyManager::CopyFrame: Present succeeded");
    return true;
}

HWND DX11ProxyManager::CreateTestWindow4K() {
    LogInfo("DX11ProxyManager::CreateTestWindow4K: Creating 4K test window (3840x2160)");

    // Window class name
    const char* window_class_name = "DX11ProxyTestWindow4K";

    // Register window class if not already registered
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = window_class_name;
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    // Try to register (will fail if already registered, which is fine)
    RegisterClassExA(&wc);

    // Calculate window size with borders
    RECT window_rect = {0, 0, 3840, 2160};
    // Use WS_POPUP to remove title bar and borders for a clean fullscreen-like appearance
    DWORD style = WS_POPUP | WS_VISIBLE;
    DWORD ex_style = 0;

    if (!AdjustWindowRectEx(&window_rect, style, FALSE, ex_style)) {
        LogWarn("DX11ProxyManager::CreateTestWindow4K: AdjustWindowRectEx failed");
    }

    int window_width = window_rect.right - window_rect.left;
    int window_height = window_rect.bottom - window_rect.top;

    // Create window at center of primary monitor
    int screen_width = GetSystemMetrics(SM_CXSCREEN);
    int screen_height = GetSystemMetrics(SM_CYSCREEN);
    int x = (screen_width - window_width) / 2;
    int y = (screen_height - window_height) / 2;

    // Clamp to screen if window is too large
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    HWND test_hwnd = CreateWindowExA(
        ex_style,
        window_class_name,
        "DX11 Proxy Test Window - 4K (3840x2160) - No Title Bar",
        style,
        x, y,
        window_width, window_height,
        nullptr,
        nullptr,
        GetModuleHandleA(nullptr),
        nullptr
    );

    if (!test_hwnd) {
        DWORD error = GetLastError();
        std::stringstream ss;
        ss << "DX11ProxyManager::CreateTestWindow4K: CreateWindowExA failed with error " << error;
        LogError(ss.str().c_str());
        return nullptr;
    }

    // Show the window maximized
    ShowWindow(test_hwnd, SW_MAXIMIZE);
    UpdateWindow(test_hwnd);

    // Track test window
    test_windows_.push_back(test_hwnd);

    // Set global proxy HWND for filtering
    g_proxy_hwnd = test_hwnd;

    std::stringstream ss;
    ss << "DX11ProxyManager::CreateTestWindow4K: Created test window (no title bar) 0x"
       << std::hex << reinterpret_cast<uintptr_t>(test_hwnd)
       << ", size: " << std::dec << window_width << "x" << window_height;
    LogInfo(ss.str().c_str());

    return test_hwnd;
}

void DX11ProxyManager::DestroyTestWindow(HWND test_hwnd) {
    if (!test_hwnd || !IsWindow(test_hwnd)) {
        return;
    }

    std::stringstream ss;
    ss << "DX11ProxyManager::DestroyTestWindow: Destroying test window 0x"
       << std::hex << reinterpret_cast<uintptr_t>(test_hwnd);
    LogInfo(ss.str().c_str());

    // Remove from tracked windows
    auto it = std::find(test_windows_.begin(), test_windows_.end(), test_hwnd);
    if (it != test_windows_.end()) {
        test_windows_.erase(it);
    }

    // Clear global proxy HWND if this was the current one
    if (g_proxy_hwnd == test_hwnd) {
        g_proxy_hwnd = nullptr;
    }

    // Destroy the window
    DestroyWindow(test_hwnd);
}

bool DX11ProxyManager::TestRender(int color_index) {
    if (!device_ || !context_ || !swapchain_) {
        LogError("DX11ProxyManager::TestRender: Not initialized");
        return false;
    }

    // Color palette for testing (8 different colors)
    static const float colors[8][4] = {
        {1.0f, 0.0f, 0.0f, 1.0f},  // Red
        {0.0f, 1.0f, 0.0f, 1.0f},  // Green
        {0.0f, 0.0f, 1.0f, 1.0f},  // Blue
        {1.0f, 1.0f, 0.0f, 1.0f},  // Yellow
        {1.0f, 0.0f, 1.0f, 1.0f},  // Magenta
        {0.0f, 1.0f, 1.0f, 1.0f},  // Cyan
        {1.0f, 0.5f, 0.0f, 1.0f},  // Orange
        {0.5f, 0.0f, 1.0f, 1.0f},  // Purple
    };

    // Clamp color index
    color_index = color_index % 8;

    HRESULT hr;

    // Get backbuffer
    ComPtr<ID3D11Texture2D> backbuffer;
    hr = swapchain_->GetBuffer(0, IID_PPV_ARGS(&backbuffer));
    if (FAILED(hr)) {
        LogError("DX11ProxyManager::TestRender: Failed to get backbuffer");
        return false;
    }

    // Create render target view
    ComPtr<ID3D11RenderTargetView> rtv;
    hr = device_->CreateRenderTargetView(backbuffer.Get(), nullptr, &rtv);
    if (FAILED(hr)) {
        LogError("DX11ProxyManager::TestRender: Failed to create render target view");
        return false;
    }

    // Clear to selected color
    context_->ClearRenderTargetView(rtv.Get(), colors[color_index]);

    // Present
    hr = swapchain_->Present(0, 0);
    if (FAILED(hr)) {
        std::stringstream ss;
        ss << "DX11ProxyManager::TestRender: Present failed with HRESULT 0x" << std::hex << hr;
        LogError(ss.str().c_str());
        return false;
    }

    return true;
}

bool DX11ProxyManager::InitializeSharedResources(IDXGISwapChain* source_swapchain) {
    LogInfo("DX11ProxyManager::InitializeSharedResources: Initializing shared resources for cross-device copy (RenoDX strategy)");

    HRESULT hr;

    // Get source backbuffer to determine dimensions and format
    ComPtr<ID3D11Texture2D> source_backbuffer;
    hr = source_swapchain->GetBuffer(0, IID_PPV_ARGS(&source_backbuffer));
    if (FAILED(hr)) {
        LogError("InitializeSharedResources: Failed to get source backbuffer");
        return false;
    }

    // Get source device and context
    source_backbuffer->GetDevice(&source_device_);
    source_device_->GetImmediateContext(&source_context_);

    // Get texture description
    D3D11_TEXTURE2D_DESC source_desc;
    source_backbuffer->GetDesc(&source_desc);

    shared_texture_width_ = source_desc.Width;
    shared_texture_height_ = source_desc.Height;
    shared_texture_format_ = source_desc.Format;

    LogInfo("InitializeSharedResources: Source texture: %ux%u, format %d",
            shared_texture_width_, shared_texture_height_, shared_texture_format_);

    // RenoDX Strategy: Create shared texture on OUR device (proxy device) first
    // This is more reliable than creating on source device (DX9 → DX11 is easier than DX11 → DX9)
    D3D11_TEXTURE2D_DESC shared_desc = {};
    shared_desc.Width = shared_texture_width_;
    shared_desc.Height = shared_texture_height_;
    shared_desc.MipLevels = 1;
    shared_desc.ArraySize = 1;
    shared_desc.Format = shared_texture_format_;
    shared_desc.SampleDesc.Count = 1;
    shared_desc.SampleDesc.Quality = 0;
    shared_desc.Usage = D3D11_USAGE_DEFAULT;
    shared_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    shared_desc.CPUAccessFlags = 0;
    shared_desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;  // Enable sharing!

    // Create shared texture on OUR device (proxy device)
    hr = device_->CreateTexture2D(&shared_desc, nullptr, &shared_texture_);
    if (FAILED(hr)) {
        std::stringstream ss;
        ss << "InitializeSharedResources: Failed to create shared texture on proxy device, HRESULT 0x" << std::hex << hr;
        LogError(ss.str().c_str());
        return false;
    }

    LogInfo("InitializeSharedResources: Created shared texture on proxy device");

    // Get shared handle from OUR texture
    ComPtr<IDXGIResource> dxgi_resource;
    hr = shared_texture_.As(&dxgi_resource);
    if (FAILED(hr)) {
        LogError("InitializeSharedResources: Failed to get IDXGIResource");
        shared_texture_.Reset();
        return false;
    }

    hr = dxgi_resource->GetSharedHandle(&shared_handle_);
    if (FAILED(hr)) {
        LogError("InitializeSharedResources: Failed to get shared handle");
        shared_texture_.Reset();
        return false;
    }

    LogInfo("InitializeSharedResources: Got shared handle from proxy device: 0x%p", shared_handle_);
    LogInfo("InitializeSharedResources: Shared resources initialized successfully using RenoDX strategy!");

    return true;
}

void DX11ProxyManager::CleanupSharedResources() {
    if (shared_texture_) {
        LogInfo("DX11ProxyManager::CleanupSharedResources: Cleaning up shared resources");
    }

    shared_texture_.Reset();
    shared_handle_ = nullptr;
    source_context_.Reset();
    source_device_.Reset();
    source_copy_query_.Reset();  // Clean up query object

    use_shared_resources_ = false;
    shared_texture_width_ = 0;
    shared_texture_height_ = 0;
    shared_texture_format_ = DXGI_FORMAT_UNKNOWN;
}

bool DX11ProxyManager::CopyFrameFromGameThread(IDXGISwapChain* source_swapchain) {
    // Validate parameters
    if (true) {
        return false;
    }
    if (!source_swapchain) {
        return false;
    }
    // Check if this is the swapchain we're monitoring
  //  if (source_swapchain_ && source_swapchain != source_swapchain_.Get()) {
  //      LogError("DX11ProxyManager::CopyFrameFromGameThread: Different swapchain, ignore");
  //      return false; // Different swapchain, ignore
  //  }

    // Check if we have a valid proxy swapchain
    if (!swapchain_) {
        LogError("DX11ProxyManager::CopyFrameFromGameThread: Missing swapchain");
        return false;
    }
    // Check if we have a valid proxy swapchain
    if ( !device_) {
        LogError("DX11ProxyManager::CopyFrameFromGameThread: Missing device");
        return false;
    }
    // Check if we have a valid proxy swapchain
    if (!context_) {
        LogError("DX11ProxyManager::CopyFrameFromGameThread: Missing context");
        return false;
    }

    LogInfo("DX11ProxyManager::CopyFrameFromGameThread: Copying frame from game thread");

    HRESULT hr;

    // Get back buffer from source swapchain (game's swapchain)
    ComPtr<ID3D11Texture2D> source_backbuffer;
    hr = source_swapchain->GetBuffer(0, IID_PPV_ARGS(&source_backbuffer));
    if (FAILED(hr)) {
        return false;
    }

    // Get back buffer from destination swapchain (proxy swapchain)
    ComPtr<ID3D11Texture2D> dest_backbuffer;
    hr = swapchain_->GetBuffer(0, IID_PPV_ARGS(&dest_backbuffer));
    if (FAILED(hr)) {
        return false;
    }

    // Get the device from source backbuffer to get its context
    ComPtr<ID3D11Device> source_device;
    source_backbuffer->GetDevice(&source_device);

    ComPtr<ID3D11DeviceContext> source_context;
    source_device->GetImmediateContext(&source_context);

    // Check if devices are different (need shared resources)
    if (source_device.Get() == device_.Get()) {
        // Different devices - use RenoDX strategy:
        // 1. Create shared texture on proxy device (DX11)
        // 2. Open shared resource on source device (DX9/DX11)
        // 3. Copy on source device to shared texture
        // 4. Copy on proxy device from shared texture to backbuffer
        if (!shared_texture_) {
            return false;
        }

        // Step 1: Open shared resource on source device and copy from game's backbuffer
        ComPtr<ID3D11Texture2D> shared_texture_on_source;
        hr = source_device->OpenSharedResource(shared_handle_, IID_PPV_ARGS(&shared_texture_on_source));
        if (FAILED(hr)) {
            LogError("DX11ProxyManager::CopyFrameFromGameThread: Failed to open shared resource on source device");
            return false;
        }
        LogInfo("DX11ProxyManager::CopyFrameFromGameThread: Opened shared resource on source device successfully");

        // Copy from game's backbuffer to shared texture (on game's device, game's thread)
        source_context->CopyResource(shared_texture_on_source.Get(), source_backbuffer.Get());

        // Flush to ensure the copy completes before the next step
        source_context->Flush();

        // Step 2: Copy from shared texture to our backbuffer (on our device)
      //  context_->CopyResource(dest_backbuffer.Get(), shared_texture_.Get());

        // Flush to ensure the copy completes before present
      //  context_->Flush();
    } else {
        // Same device - direct copy
    //    context_->CopyResource(dest_backbuffer.Get(), source_backbuffer.Get());

        // Flush to ensure the copy completes before present
       // context_->Flush();
    }

    // Present the copied frame
    hr = swapchain_->Present(0, 0);
    if (FAILED(hr)) {
        return false;
    }

    // Increment frame counter
    frames_copied_.fetch_add(1);

    return true;
}

DXGI_FORMAT DX11ProxyManager::GetFormatFromIndex(int format_index) {
    switch (format_index) {
        case 0: return DXGI_FORMAT_R10G10B10A2_UNORM;  // HDR 10-bit
        case 1: return DXGI_FORMAT_R16G16B16A16_FLOAT; // HDR 16-bit float
        case 2: return DXGI_FORMAT_R8G8B8A8_UNORM;     // SDR 8-bit
        default:
            LogWarn("DX11ProxyManager::GetFormatFromIndex: Unknown format index %d, defaulting to R10G10B10A2", format_index);
            return DXGI_FORMAT_R10G10B10A2_UNORM;
    }
}

bool DX11ProxyManager::SetHDRColorSpace() {
    std::lock_guard lock(mutex_);

    if (!is_initialized_.load()) {
        LogError("DX11ProxyManager::SetHDRColorSpace: Manager not initialized");
        return false;
    }

    if (!swapchain_) {
        LogError("DX11ProxyManager::SetHDRColorSpace: No swap chain available");
        return false;
    }

    // Get IDXGISwapChain3 interface for SetColorSpace1
    ComPtr<IDXGISwapChain3> swapchain3;
    HRESULT hr = swapchain_.As(&swapchain3);
    if (FAILED(hr)) {
        LogError("DX11ProxyManager::SetHDRColorSpace: Failed to get IDXGISwapChain3 interface");
        return false;
    }

    // Get current format from settings
    int format_index = g_dx11ProxySettings.swapchain_format.load();
    DXGI_FORMAT current_format = GetFormatFromIndex(format_index);

    // Determine appropriate color space based on format
    DXGI_COLOR_SPACE_TYPE color_space;
    switch (current_format) {
        case DXGI_FORMAT_R10G10B10A2_UNORM:
            // HDR10 format - use HDR10 color space
            color_space = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
            break;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
            // FP16 format - use scRGB color space
            color_space = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
            break;
        case DXGI_FORMAT_R8G8B8A8_UNORM:
            // SDR format - use sRGB color space
            color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
            break;
        default:
            LogWarn("DX11ProxyManager::SetHDRColorSpace: Unknown format %d, using sRGB", static_cast<int>(current_format));
            color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
            break;
    }

    // Set the appropriate color space
    hr = swapchain3->SetColorSpace1(color_space);
    if (FAILED(hr)) {
        std::stringstream ss;
        ss << "DX11ProxyManager::SetHDRColorSpace: SetColorSpace1 failed with HRESULT 0x" << std::hex << hr;
        LogError(ss.str().c_str());
        return false;
    }

    std::stringstream ss;
    ss << "DX11ProxyManager::SetHDRColorSpace: Successfully set color space " << static_cast<int>(color_space)
       << " for format " << static_cast<int>(current_format) << " on proxy swap chain";
    LogInfo(ss.str().c_str());
    return true;
}

bool DX11ProxyManager::SetSourceColorSpace() {
    #if 0
    std::lock_guard lock(mutex_);

    if (!is_initialized_.load()) {
        LogError("DX11ProxyManager::SetSourceColorSpace: Manager not initialized");
        return false;
    }
    #endif

    // Get the game's swap chain from global variables
    void* game_swapchain_ptr = g_last_swapchain_ptr_unsafe.load();
    int game_api = g_last_reshade_device_api.load();

    if (!game_swapchain_ptr) {
        LogError("DX11ProxyManager::SetSourceColorSpace: No game swap chain detected");
        return false;
    }

    // Check if it's a compatible API (DX11 or DX12)
    bool is_dx11 = (game_api == 0xb000); // reshade::api::device_api::d3d11
    bool is_dx12 = (game_api == 0xc000); // reshade::api::device_api::d3d12
    if (!is_dx11 && !is_dx12) {
        LogError("DX11ProxyManager::SetSourceColorSpace: Incompatible API %d (need DX11 or DX12)", game_api);
        return false;
    }

    // Get the native swap chain handle from ReShade
    auto* reshade_swapchain = static_cast<reshade::api::swapchain*>(game_swapchain_ptr);
    uint64_t native_handle = reshade_swapchain->get_native();

    if (!native_handle) {
        LogError("DX11ProxyManager::SetSourceColorSpace: Failed to get native swap chain handle");
        return false;
    }

    // Cast to IDXGISwapChain
    auto* native_swapchain = reinterpret_cast<IDXGISwapChain*>(native_handle);
    ComPtr<IDXGISwapChain3> source_swapchain3;
    HRESULT hr = native_swapchain->QueryInterface(IID_PPV_ARGS(&source_swapchain3));
    if (FAILED(hr)) {
        LogError("DX11ProxyManager::SetSourceColorSpace: Failed to get IDXGISwapChain3 interface from game swap chain");
        return false;
    }

    // setfullscreen false
    hr = source_swapchain3->SetFullscreenState(FALSE, nullptr);
    if (FAILED(hr)) {
        LogError("DX11ProxyManager::SetSourceColorSpace: Failed to set fullscreen state to false");
        return false;
    }

    // Get current format from settings
    int format_index = g_dx11ProxySettings.swapchain_format.load();
    DXGI_FORMAT current_format = GetFormatFromIndex(format_index);

    // Get the actual swap chain format to see what the game is using
    DXGI_SWAP_CHAIN_DESC1 desc;
    ComPtr<IDXGISwapChain1> swapchain1;
    hr = source_swapchain3.As(&swapchain1);
    if (SUCCEEDED(hr)) {
        hr = swapchain1->GetDesc1(&desc);
        if (SUCCEEDED(hr)) {
            LogInfo("DX11ProxyManager::SetSourceColorSpace: Game swap chain format: %d", static_cast<int>(desc.Format));
        }
    }

    // Determine appropriate color space based on format
    DXGI_COLOR_SPACE_TYPE color_space;
    switch (current_format) {
        case DXGI_FORMAT_R10G10B10A2_UNORM:
            // HDR10 format - use HDR10 color space
            color_space = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
            break;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
            // FP16 format - use scRGB color space
            color_space = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
            break;
        case DXGI_FORMAT_R8G8B8A8_UNORM:
            // SDR format - use sRGB color space
            color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
            break;
        default:
            LogWarn("DX11ProxyManager::SetSourceColorSpace: Unknown format %d, using sRGB", static_cast<int>(current_format));
            color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
            break;
    }

    // Check if the color space is supported before trying to set it
    UINT color_space_support = 0;
    hr = source_swapchain3->CheckColorSpaceSupport(color_space, &color_space_support);
    if (FAILED(hr)) {
        std::stringstream ss;
        ss << "DX11ProxyManager::SetSourceColorSpace: CheckColorSpaceSupport failed with HRESULT 0x" << std::hex << hr;
        LogError(ss.str().c_str());
        return false;
    }

    if (color_space_support == 0) {
        LogError("DX11ProxyManager::SetSourceColorSpace: Color space %d not supported by game swap chain", static_cast<int>(color_space));
        return false;
    }

    LogInfo("DX11ProxyManager::SetSourceColorSpace: Color space %d is supported, proceeding with SetColorSpace1", static_cast<int>(color_space));

    // Set the appropriate color space on source swap chain
    hr = source_swapchain3->SetColorSpace1(color_space);
    if (FAILED(hr)) {
        std::stringstream ss;
        ss << "DX11ProxyManager::SetSourceColorSpace: SetColorSpace1 failed with HRESULT 0x" << std::hex << hr;
        LogError(ss.str().c_str());

        // Try fallback to basic sRGB color space
        LogInfo("DX11ProxyManager::SetSourceColorSpace: Trying fallback to sRGB color space");
        DXGI_COLOR_SPACE_TYPE fallback_color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

        UINT fallback_support = 0;
        hr = source_swapchain3->CheckColorSpaceSupport(fallback_color_space, &fallback_support);
        if (SUCCEEDED(hr) && fallback_support > 0) {
            hr = source_swapchain3->SetColorSpace1(fallback_color_space);
            if (SUCCEEDED(hr)) {
                LogInfo("DX11ProxyManager::SetSourceColorSpace: Successfully set fallback sRGB color space");
                color_space = fallback_color_space; // Update for success message
            } else {
                LogError("DX11ProxyManager::SetSourceColorSpace: Fallback sRGB color space also failed");
                return false;
            }
        } else {
            LogError("DX11ProxyManager::SetSourceColorSpace: Fallback sRGB color space not supported");
            return false;
        }
    }

    std::stringstream ss;
    ss << "DX11ProxyManager::SetSourceColorSpace: Successfully set color space " << static_cast<int>(color_space)
       << " for format " << static_cast<int>(current_format) << " on source swap chain";
    LogInfo(ss.str().c_str());
    return true;
}

} // namespace dx11_proxy

