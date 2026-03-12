#pragma once

#include <windows.h>
#include <atomic>
#include <cstdint>

namespace display_commanderhooks::debug_output {

// Function pointer types for debug output functions
using OutputDebugStringA_pfn = void(WINAPI *)(LPCSTR lpOutputString);
using OutputDebugStringW_pfn = void(WINAPI *)(LPCWSTR lpOutputString);

// Original function pointers
extern OutputDebugStringA_pfn OutputDebugStringA_Original;
extern OutputDebugStringW_pfn OutputDebugStringW_Original;

// Hooked debug output functions
void WINAPI OutputDebugStringA_Detour(LPCSTR lpOutputString);
void WINAPI OutputDebugStringW_Detour(LPCWSTR lpOutputString);

// Hook management
bool InstallDebugOutputHooks();
void UninstallDebugOutputHooks();

// Statistics
struct DebugOutputStats {
    std::atomic<uint64_t> output_debug_string_a_calls{0};
    std::atomic<uint64_t> output_debug_string_w_calls{0};
    std::atomic<uint64_t> total_bytes_logged{0};
};

DebugOutputStats& GetDebugOutputStats();

} // namespace display_commanderhooks::debug_output
