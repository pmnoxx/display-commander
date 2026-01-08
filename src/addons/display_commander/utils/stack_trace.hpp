#pragma once

#include <string>
#include <vector>
#include <windows.h>

namespace stack_trace {

// Generate a stack trace and return it as a vector of strings
// Uses current context (RtlCaptureContext)
std::vector<std::string> GenerateStackTrace();

// Generate a stack trace from a specific context (e.g., exception context)
// Returns it as a vector of strings
std::vector<std::string> GenerateStackTrace(CONTEXT* context);

// Generate a stack trace and return it as a single formatted string
// Uses current context
std::string GetStackTraceString();

// Generate a stack trace from a specific context and return it as a single formatted string
std::string GetStackTraceString(CONTEXT* context);

// Check if nvngx_update.exe is currently running
bool IsNvngxUpdateRunning();

} // namespace stack_trace
