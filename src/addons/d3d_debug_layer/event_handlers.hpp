#pragma once

#include <reshade.hpp>
#include <thread>
#include <atomic>

namespace d3d_debug_layer {

// Global state for the debug layer addon
extern std::atomic<bool> g_initialized;
extern std::atomic<bool> g_shutdown;
extern std::thread g_message_processor_thread;

// Forward declarations for ReShade event handlers
void OnInitDevice(reshade::api::device* device);
void OnDestroyDevice(reshade::api::device* device);
void OnPresent(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain,
    const reshade::api::rect* source_rect, const reshade::api::rect* dest_rect,
    uint32_t dirty_rect_count, const reshade::api::rect* dirty_rects);

// Helper function to determine if device is D3D12
bool IsD3D12Device(reshade::api::device* device);

// Message processing thread function
void MessageProcessorThread();

} // namespace d3d_debug_layer
