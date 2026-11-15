// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#pragma once

// ReShade installed as a global Vulkan implicit layer (VK_LAYER_reshade, e.g. C:\ProgramData\ReShade\ReShade64.json).
// When it is enabled, Display Commander loads the layer's ReShade DLL instead of its own Reshade64.dll / Reshade32.dll,
// so the Vulkan loader later reuses the same module (one ReShade; the layer still works).

// Libraries <standard C++>
#include <string>

namespace host::reshade_vulkan_layer {

struct LayerInfo {
    std::wstring manifest_path;  // the .json registered under ...\Khronos\Vulkan\ImplicitLayers
    std::wstring library_path;   // the ReShade DLL the manifest points to (absolute)
};

// True if an enabled VK_LAYER_reshade implicit layer for this process bitness is registered (HKLM or HKCU) and not
// disabled by its disable_environment variable. Registry + small file reads only: safe under the loader lock.
bool FindEnabledReShadeLayer(LayerInfo* out_info);

}  // namespace host::reshade_vulkan_layer
