#pragma once

#include <reshade.hpp>
#include <d3d11_4.h>
#include <d3d12.h>

// GPU completion measurement functions
// Enqueue GPU completion measurement for the given swapchain
// This sets up a GPU fence that will be signaled when the GPU completes rendering
// command_queue is optional but recommended for D3D12 to signal the fence correctly
void EnqueueGPUCompletion(reshade::api::swapchain* swapchain, IDXGISwapChain* dxgi_swapchain, reshade::api::command_queue* command_queue = nullptr);

