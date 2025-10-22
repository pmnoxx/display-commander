#pragma once

#include <string>
#include <vector>

namespace stack_trace {

// Generate a stack trace and return it as a vector of strings
std::vector<std::string> GenerateStackTrace();

// Generate a stack trace and write it to DbgView using OutputDebugString
void PrintStackTraceToDbgView();

// Generate a stack trace and return it as a single formatted string
std::string GetStackTraceString();

} // namespace stack_trace
