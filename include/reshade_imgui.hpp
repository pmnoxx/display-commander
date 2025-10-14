#pragma once

// Ensure ImTextureID is defined as ImU64 before including imgui.h
// This is required by ReShade for proper texture handling
#ifndef ImTextureID
#define ImTextureID ImU64
#endif

// Include ImGui first, then ReShade
// This order is critical for proper compilation and functionality
#include <imgui.h>
#include <reshade.hpp>
