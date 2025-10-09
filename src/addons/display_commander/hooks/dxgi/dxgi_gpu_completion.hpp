#pragma once

#include <reshade.hpp>

// GPU completion measurement functions
// Enqueue GPU completion measurement for the given swapchain
// This sets up a GPU fence that will be signaled when the GPU completes rendering
void EnqueueGPUCompletion(reshade::api::swapchain* swapchain);

