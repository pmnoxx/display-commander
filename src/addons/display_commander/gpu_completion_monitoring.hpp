#pragma once

// Start/stop functions for GPU completion monitoring thread
void StartGPUCompletionMonitoring();
void StopGPUCompletionMonitoring();

// GPU completion callback for OpenGL (assumes immediate completion)
void HandleOpenGLGPUCompletion();

