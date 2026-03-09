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

} // namespace stack_trace
