#pragma once

#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>
#include <atomic>
#include <memory>
#include <mutex>

using Microsoft::WRL::ComPtr;

namespace d3d_debug_layer {

// Forward declarations
class D3D11DebugHandler;
class D3D12DebugHandler;

// Debug layer manager class
class DebugLayerManager {
public:
    static DebugLayerManager& GetInstance();
    
    // Initialize debug layer for device
    bool InitializeForDevice(void* device, bool is_d3d12);
    
    // Cleanup debug layer for device
    void CleanupForDevice(void* device);
    
    // Process and log debug messages (called periodically)
    void ProcessDebugMessages();
    
    // Check if debug layer is available
    bool IsDebugLayerAvailable() const;
    
private:
    DebugLayerManager() = default;
    ~DebugLayerManager() = default;
    
    // Non-copyable
    DebugLayerManager(const DebugLayerManager&) = delete;
    DebugLayerManager& operator=(const DebugLayerManager&) = delete;
    
    std::unique_ptr<D3D11DebugHandler> m_d3d11_handler;
    std::unique_ptr<D3D12DebugHandler> m_d3d12_handler;
    std::mutex m_mutex;
    std::atomic<bool> m_initialized{false};
};

// D3D11 debug message handler
class D3D11DebugHandler {
public:
    bool Initialize(ID3D11Device* device);
    void Cleanup();
    void ProcessMessages();
    bool IsInitialized() const { return m_info_queue != nullptr; }
    
private:
    ComPtr<ID3D11InfoQueue> m_info_queue;
    ComPtr<ID3D11Device> m_device;
    void LogMessage(const D3D11_MESSAGE& message);
    const char* GetSeverityString(D3D11_MESSAGE_SEVERITY severity);
    const char* GetCategoryString(D3D11_MESSAGE_CATEGORY category);
};

// D3D12 debug message handler
class D3D12DebugHandler {
public:
    bool Initialize(ID3D12Device* device);
    void Cleanup();
    void ProcessMessages();
    bool IsInitialized() const { return m_info_queue != nullptr; }
    
private:
    ComPtr<ID3D12InfoQueue> m_info_queue;
    ComPtr<ID3D12Device> m_device;
    void LogMessage(const D3D12_MESSAGE& message);
    const char* GetSeverityString(D3D12_MESSAGE_SEVERITY severity);
    const char* GetCategoryString(D3D12_MESSAGE_CATEGORY category);
};

} // namespace d3d_debug_layer
