#include "debug_layer.hpp"
#include <reshade.hpp>
#include <sstream>

// Define IID constants manually to avoid linking issues
static const GUID IID_ID3D11InfoQueue_Local = { 0x6543dbb6, 0x1b48, 0x42f5, {0xab, 0x82, 0xe9, 0x7e, 0xc7, 0x43, 0x26, 0xf6} };
static const GUID IID_ID3D12InfoQueue_Local = { 0x0742a90b, 0xc387, 0x483f, {0xb9, 0x46, 0x30, 0xa7, 0xe4, 0xe6, 0x14, 0x58} };

namespace d3d_debug_layer {

// Helper function to log messages (using ReShade's logging system)
void LogInfo(const std::string& message) {
    reshade::log::message(reshade::log::level::info, ("[D3D Debug] " + message).c_str());
}

void LogWarn(const std::string& message) {
    reshade::log::message(reshade::log::level::warning, ("[D3D Debug] " + message).c_str());
}

void LogError(const std::string& message) {
    reshade::log::message(reshade::log::level::error, ("[D3D Debug] " + message).c_str());
}

// DebugLayerManager implementation
DebugLayerManager& DebugLayerManager::GetInstance() {
    static DebugLayerManager instance;
    return instance;
}

bool DebugLayerManager::InitializeForDevice(void* device, bool is_d3d12) {
    if (!device) {
        LogError("Invalid device pointer provided");
        return false;
    }

    if (is_d3d12) {
        // Initialize D3D12 debug handler
        auto* handler = m_d3d12_handler.load();
        if (!handler) {
            auto new_handler = std::make_unique<D3D12DebugHandler>();
            D3D12DebugHandler* expected = nullptr;
            if (m_d3d12_handler.compare_exchange_strong(expected, new_handler.get())) {
                new_handler.release(); // Don't delete, it's now managed by the atomic
                handler = expected;
            } else {
                handler = expected;
            }
        }

        if (handler && handler->Initialize(static_cast<ID3D12Device*>(device))) {
            LogInfo("D3D12 debug layer initialized successfully");
            m_initialized = true;
            return true;
        } else {
            LogWarn("Failed to initialize D3D12 debug layer (may not be available)");
            return false;
        }
    } else {
        // Initialize D3D11 debug handler
        auto* handler = m_d3d11_handler.load();
        if (!handler) {
            auto new_handler = std::make_unique<D3D11DebugHandler>();
            D3D11DebugHandler* expected = nullptr;
            if (m_d3d11_handler.compare_exchange_strong(expected, new_handler.get())) {
                new_handler.release(); // Don't delete, it's now managed by the atomic
                handler = expected;
            } else {
                handler = expected;
            }
        }

        if (handler && handler->Initialize(static_cast<ID3D11Device*>(device))) {
            LogInfo("D3D11 debug layer initialized successfully");
            m_initialized = true;
            return true;
        } else {
            LogWarn("Failed to initialize D3D11 debug layer (may not be available)");
            return false;
        }
    }
}

void DebugLayerManager::CleanupForDevice(void* device) {
    auto* d3d11_handler = m_d3d11_handler.load();
    if (d3d11_handler && d3d11_handler->IsInitialized()) {
        d3d11_handler->Cleanup();
        LogInfo("D3D11 debug layer cleaned up");
    }

    auto* d3d12_handler = m_d3d12_handler.load();
    if (d3d12_handler && d3d12_handler->IsInitialized()) {
        d3d12_handler->Cleanup();
        LogInfo("D3D12 debug layer cleaned up");
    }

    m_initialized = false;
}

void DebugLayerManager::ProcessDebugMessages() {
    if (!m_initialized.load()) {
        return;
    }

    auto* d3d11_handler = m_d3d11_handler.load();
    if (d3d11_handler && d3d11_handler->IsInitialized()) {
        d3d11_handler->ProcessMessages();
    }

    auto* d3d12_handler = m_d3d12_handler.load();
    if (d3d12_handler && d3d12_handler->IsInitialized()) {
        d3d12_handler->ProcessMessages();
    }
}

bool DebugLayerManager::IsDebugLayerAvailable() const {
    return m_initialized.load();
}

// D3D11DebugHandler implementation
bool D3D11DebugHandler::Initialize(ID3D11Device* device) {
    if (!device) {
        LogError("D3D11: Invalid device pointer");
        return false;
    }

    m_device = device;

    // Query for ID3D11InfoQueue interface
    HRESULT hr = device->QueryInterface(IID_ID3D11InfoQueue_Local,
                                       reinterpret_cast<void**>(m_info_queue.GetAddressOf()));

    if (FAILED(hr)) {
        LogWarn("D3D11: InfoQueue interface not available (debug layer not enabled)");
        return false;
    }

    // Clear any existing messages
    m_info_queue->ClearStoredMessages();

    LogInfo("D3D11: InfoQueue interface acquired successfully");
    return true;
}

void D3D11DebugHandler::Cleanup() {
    if (m_info_queue) {
        m_info_queue->ClearStoredMessages();
        m_info_queue.Reset();
    }
    m_device.Reset();
}

void D3D11DebugHandler::ProcessMessages() {
    if (!m_info_queue) {
        return;
    }

    UINT64 message_count = m_info_queue->GetNumStoredMessages();
    if (message_count == 0) {
        return;
    }

    // Limit processing to avoid performance issues
    const UINT64 max_messages_per_frame = 50;
    UINT64 messages_to_process = (message_count < max_messages_per_frame) ? message_count : max_messages_per_frame;

    for (UINT64 i = 0; i < messages_to_process; ++i) {
        SIZE_T message_size = 0;
        HRESULT hr = m_info_queue->GetMessage(i, nullptr, &message_size);

        if (SUCCEEDED(hr) && message_size > 0) {
            auto message_buffer = std::make_unique<char[]>(message_size);
            D3D11_MESSAGE* message = reinterpret_cast<D3D11_MESSAGE*>(message_buffer.get());

            hr = m_info_queue->GetMessage(i, message, &message_size);
            if (SUCCEEDED(hr)) {
                LogMessage(*message);
            }
        }
    }

    // Clear processed messages to prevent memory buildup
    m_info_queue->ClearStoredMessages();

    if (message_count > max_messages_per_frame) {
        LogWarn("D3D11: " + std::to_string(message_count - max_messages_per_frame) +
                " additional debug messages skipped to maintain performance");
    }
}

void D3D11DebugHandler::LogMessage(const D3D11_MESSAGE& message) {
    std::ostringstream oss;
    oss << "D3D11 [" << GetSeverityString(message.Severity)
        << "] [" << GetCategoryString(message.Category)
        << "] ID:" << message.ID
        << " - " << std::string(message.pDescription, message.DescriptionByteLength - 1);

    // Log with appropriate severity
    switch (message.Severity) {
        case D3D11_MESSAGE_SEVERITY_CORRUPTION:
        case D3D11_MESSAGE_SEVERITY_ERROR:
            LogError(oss.str());
            break;
        case D3D11_MESSAGE_SEVERITY_WARNING:
            LogWarn(oss.str());
            break;
        case D3D11_MESSAGE_SEVERITY_INFO:
        case D3D11_MESSAGE_SEVERITY_MESSAGE:
        default:
            LogInfo(oss.str());
            break;
    }
}

const char* D3D11DebugHandler::GetSeverityString(D3D11_MESSAGE_SEVERITY severity) {
    switch (severity) {
        case D3D11_MESSAGE_SEVERITY_CORRUPTION: return "CORRUPTION";
        case D3D11_MESSAGE_SEVERITY_ERROR: return "ERROR";
        case D3D11_MESSAGE_SEVERITY_WARNING: return "WARNING";
        case D3D11_MESSAGE_SEVERITY_INFO: return "INFO";
        case D3D11_MESSAGE_SEVERITY_MESSAGE: return "MESSAGE";
        default: return "UNKNOWN";
    }
}

const char* D3D11DebugHandler::GetCategoryString(D3D11_MESSAGE_CATEGORY category) {
    switch (category) {
        case D3D11_MESSAGE_CATEGORY_APPLICATION_DEFINED: return "APPLICATION";
        case D3D11_MESSAGE_CATEGORY_MISCELLANEOUS: return "MISCELLANEOUS";
        case D3D11_MESSAGE_CATEGORY_INITIALIZATION: return "INITIALIZATION";
        case D3D11_MESSAGE_CATEGORY_CLEANUP: return "CLEANUP";
        case D3D11_MESSAGE_CATEGORY_COMPILATION: return "COMPILATION";
        case D3D11_MESSAGE_CATEGORY_STATE_CREATION: return "STATE_CREATION";
        case D3D11_MESSAGE_CATEGORY_STATE_SETTING: return "STATE_SETTING";
        case D3D11_MESSAGE_CATEGORY_STATE_GETTING: return "STATE_GETTING";
        case D3D11_MESSAGE_CATEGORY_RESOURCE_MANIPULATION: return "RESOURCE_MANIPULATION";
        case D3D11_MESSAGE_CATEGORY_EXECUTION: return "EXECUTION";
        case D3D11_MESSAGE_CATEGORY_SHADER: return "SHADER";
        default: return "UNKNOWN";
    }
}

// D3D12DebugHandler implementation
bool D3D12DebugHandler::Initialize(ID3D12Device* device) {
    if (!device) {
        LogError("D3D12: Invalid device pointer");
        return false;
    }

    m_device = device;

    // Query for ID3D12InfoQueue interface
    HRESULT hr = device->QueryInterface(IID_ID3D12InfoQueue_Local,
                                       reinterpret_cast<void**>(m_info_queue.GetAddressOf()));

    if (FAILED(hr)) {
        LogWarn("D3D12: InfoQueue interface not available (debug layer not enabled)");
        return false;
    }

    // Clear any existing messages
    m_info_queue->ClearStoredMessages();

    LogInfo("D3D12: InfoQueue interface acquired successfully");
    return true;
}

void D3D12DebugHandler::Cleanup() {
    if (m_info_queue) {
        m_info_queue->ClearStoredMessages();
        m_info_queue.Reset();
    }
    m_device.Reset();
}

void D3D12DebugHandler::ProcessMessages() {
    if (!m_info_queue) {
        return;
    }

    UINT64 message_count = m_info_queue->GetNumStoredMessages();
    if (message_count == 0) {
        return;
    }

    // Limit processing to avoid performance issues
    const UINT64 max_messages_per_frame = 50;
    UINT64 messages_to_process = (message_count < max_messages_per_frame) ? message_count : max_messages_per_frame;

    for (UINT64 i = 0; i < messages_to_process; ++i) {
        SIZE_T message_size = 0;
        HRESULT hr = m_info_queue->GetMessage(i, nullptr, &message_size);

        if (SUCCEEDED(hr) && message_size > 0) {
            auto message_buffer = std::make_unique<char[]>(message_size);
            D3D12_MESSAGE* message = reinterpret_cast<D3D12_MESSAGE*>(message_buffer.get());

            hr = m_info_queue->GetMessage(i, message, &message_size);
            if (SUCCEEDED(hr)) {
                LogMessage(*message);
            }
        }
    }

    // Clear processed messages to prevent memory buildup
    m_info_queue->ClearStoredMessages();

    if (message_count > max_messages_per_frame) {
        LogWarn("D3D12: " + std::to_string(message_count - max_messages_per_frame) +
                " additional debug messages skipped to maintain performance");
    }
}

void D3D12DebugHandler::LogMessage(const D3D12_MESSAGE& message) {
    std::ostringstream oss;
    oss << "D3D12 [" << GetSeverityString(message.Severity)
        << "] [" << GetCategoryString(message.Category)
        << "] ID:" << message.ID
        << " - " << std::string(message.pDescription, message.DescriptionByteLength - 1);

    // Log with appropriate severity
    switch (message.Severity) {
        case D3D12_MESSAGE_SEVERITY_CORRUPTION:
        case D3D12_MESSAGE_SEVERITY_ERROR:
            LogError(oss.str());
            break;
        case D3D12_MESSAGE_SEVERITY_WARNING:
            LogWarn(oss.str());
            break;
        case D3D12_MESSAGE_SEVERITY_INFO:
        case D3D12_MESSAGE_SEVERITY_MESSAGE:
        default:
            LogInfo(oss.str());
            break;
    }
}

const char* D3D12DebugHandler::GetSeverityString(D3D12_MESSAGE_SEVERITY severity) {
    switch (severity) {
        case D3D12_MESSAGE_SEVERITY_CORRUPTION: return "CORRUPTION";
        case D3D12_MESSAGE_SEVERITY_ERROR: return "ERROR";
        case D3D12_MESSAGE_SEVERITY_WARNING: return "WARNING";
        case D3D12_MESSAGE_SEVERITY_INFO: return "INFO";
        case D3D12_MESSAGE_SEVERITY_MESSAGE: return "MESSAGE";
        default: return "UNKNOWN";
    }
}

const char* D3D12DebugHandler::GetCategoryString(D3D12_MESSAGE_CATEGORY category) {
    switch (category) {
        case D3D12_MESSAGE_CATEGORY_APPLICATION_DEFINED: return "APPLICATION";
        case D3D12_MESSAGE_CATEGORY_MISCELLANEOUS: return "MISCELLANEOUS";
        case D3D12_MESSAGE_CATEGORY_INITIALIZATION: return "INITIALIZATION";
        case D3D12_MESSAGE_CATEGORY_CLEANUP: return "CLEANUP";
        case D3D12_MESSAGE_CATEGORY_COMPILATION: return "COMPILATION";
        case D3D12_MESSAGE_CATEGORY_STATE_CREATION: return "STATE_CREATION";
        case D3D12_MESSAGE_CATEGORY_STATE_SETTING: return "STATE_SETTING";
        case D3D12_MESSAGE_CATEGORY_STATE_GETTING: return "STATE_GETTING";
        case D3D12_MESSAGE_CATEGORY_RESOURCE_MANIPULATION: return "RESOURCE_MANIPULATION";
        case D3D12_MESSAGE_CATEGORY_EXECUTION: return "EXECUTION";
        case D3D12_MESSAGE_CATEGORY_SHADER: return "SHADER";
        // Note: D3D12_MESSAGE_CATEGORY_GPU_BASED_VALIDATION not available in all SDK versions
        // case D3D12_MESSAGE_CATEGORY_GPU_BASED_VALIDATION: return "GPU_VALIDATION";
        default: return "UNKNOWN";
    }
}

} // namespace d3d_debug_layer
