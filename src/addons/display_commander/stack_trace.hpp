#pragma once

#include <string>
#include <vector>
#include <windows.h>

namespace stack_trace {

// Stack frame information
struct StackFrame {
    std::string module_name;
    std::string function_name;
    std::string file_name;
    DWORD64 address;
    DWORD line_number;
    DWORD64 offset;
};

// Initialize stack trace functionality
bool Initialize();

// Shutdown stack trace functionality
void Shutdown();

// Capture current stack trace
std::vector<StackFrame> CaptureStackTrace(DWORD max_frames = 64, CONTEXT* context = nullptr);

// Capture stack trace from exception context
std::vector<StackFrame> CaptureStackTraceFromException(EXCEPTION_POINTERS* exception_info, DWORD max_frames = 64);

// Format stack trace as string for logging
std::string FormatStackTrace(const std::vector<StackFrame>& frames, bool include_addresses = true);

// Get symbol information for a specific address
StackFrame GetSymbolInfo(DWORD64 address);

// Check if stack trace functionality is available
bool IsAvailable();

} // namespace stack_trace
