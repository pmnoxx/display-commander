/**
 * DX11 Proxy Device Manager Implementation
 */

#include "dx11_proxy_manager.hpp"
#include "dx11_proxy_shared_resources.hpp"
#include "../utils.hpp"
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

void DX11ProxyManager::CopyThreadLoop() {
    LogInfo("[COPY-THREAD] Copy thread loop started");

    while (copy_thread_running_.load()) {
        // Copy one frame
        if (CopyFrame()) {
            frames_copied_.fetch_add(1);
        }

        // Sleep for 1 second (1000 milliseconds)
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    LogInfo("[COPY-THREAD] Copy thread loop exiting");
}

bool DX11ProxyManager::CopyFrame() {
    // Ensure we have valid swapchains
    if (!source_swapchain_ || !swapchain_) {
        return false;
    }

    if (!device_ || !context_) {
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

    // Use shared resources if devices are different
    if (use_shared_resources_) {
        if (!shared_texture_ || !source_context_) {
            LogError("DX11ProxyManager::CopyFrame: Shared resources not initialized");
            return false;
        }

        // Step 1: Copy from game's backbuffer to shared texture (on game's device)
        source_context_->CopyResource(shared_texture_.Get(), source_backbuffer.Get());

        // Flush to ensure the copy completes before the next step
        source_context_->Flush();

        // Step 2: Copy from shared texture to our backbuffer (on our device)
        context_->CopyResource(dest_backbuffer.Get(), shared_texture_.Get());
    } else {
        // Direct copy - same device
        context_->CopyResource(dest_backbuffer.Get(), source_backbuffer.Get());
    }

    // Present the copied frame
    hr = swapchain_->Present(0, 0);
    if (FAILED(hr)) {
        std::stringstream ss;
        ss << "DX11ProxyManager::CopyFrame: Present failed with HRESULT 0x" << std::hex << hr;
        LogWarn(ss.str().c_str());
        return false;
    }

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
    DWORD style = WS_OVERLAPPEDWINDOW;
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
        "DX11 Proxy Test Window - 4K (3840x2160)",
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

    // Show the window
    ShowWindow(test_hwnd, SW_SHOW);
    UpdateWindow(test_hwnd);

    // Track test window
    test_windows_.push_back(test_hwnd);

    std::stringstream ss;
    ss << "DX11ProxyManager::CreateTestWindow4K: Created test window 0x"
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
    LogInfo("DX11ProxyManager::InitializeSharedResources: Initializing shared resources for cross-device copy");

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

    // Create shared texture on SOURCE device (game's device)
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

    ComPtr<ID3D11Texture2D> shared_texture_on_source;
    hr = source_device_->CreateTexture2D(&shared_desc, nullptr, &shared_texture_on_source);
    if (FAILED(hr)) {
        std::stringstream ss;
        ss << "InitializeSharedResources: Failed to create shared texture on source device, HRESULT 0x" << std::hex << hr;
        LogError(ss.str().c_str());
        return false;
    }

    LogInfo("InitializeSharedResources: Created shared texture on source device");

    // Get shared handle from the texture
    ComPtr<IDXGIResource> dxgi_resource;
    hr = shared_texture_on_source.As(&dxgi_resource);
    if (FAILED(hr)) {
        LogError("InitializeSharedResources: Failed to get IDXGIResource");
        return false;
    }

    HANDLE shared_handle = nullptr;
    hr = dxgi_resource->GetSharedHandle(&shared_handle);
    if (FAILED(hr)) {
        LogError("InitializeSharedResources: Failed to get shared handle");
        return false;
    }

    LogInfo("InitializeSharedResources: Got shared handle: 0x%p", shared_handle);

    // Open shared resource on OUR device (test window's device)
    hr = device_->OpenSharedResource(shared_handle, IID_PPV_ARGS(&shared_texture_));
    if (FAILED(hr)) {
        std::stringstream ss;
        ss << "InitializeSharedResources: Failed to open shared resource on dest device, HRESULT 0x" << std::hex << hr;
        LogError(ss.str().c_str());
        return false;
    }

    LogInfo("InitializeSharedResources: Successfully opened shared resource on dest device");
    LogInfo("InitializeSharedResources: Shared resources initialized successfully!");

    return true;
}

void DX11ProxyManager::CleanupSharedResources() {
    if (shared_texture_) {
        LogInfo("DX11ProxyManager::CleanupSharedResources: Cleaning up shared resources");
    }

    shared_texture_.Reset();
    source_context_.Reset();
    source_device_.Reset();

    use_shared_resources_ = false;
    shared_texture_width_ = 0;
    shared_texture_height_ = 0;
    shared_texture_format_ = DXGI_FORMAT_UNKNOWN;
}

} // namespace dx11_proxy

