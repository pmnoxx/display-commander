#pragma once

// Architecture-aware short spin-wait hint.
// Maps to a low-power CPU hint on each architecture.

#if defined(_MSC_VER)
#include <intrin.h>
#endif

// x86 / x64: use PAUSE
#if defined(_M_X64) || defined(_M_IX86)
#include <immintrin.h>
#define DC_CPU_RELAX() _mm_pause()

// ARM / ARM64: use YIELD
#elif defined(_M_ARM64) || defined(_M_ARM)
#define DC_CPU_RELAX() __yield()

// Fallback: use Windows YieldProcessor
#else
#include <Windows.h>
#define DC_CPU_RELAX() YieldProcessor()
#endif
