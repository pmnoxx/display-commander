# Module Loading Callback System

## Overview
The `OnModuleLoaded` function provides a centralized way to automatically install appropriate hooks when specific modules are loaded. This system is inspired by Special-K's comprehensive module detection and hook installation approach.

## How It Works

### 1. Automatic Hook Installation
When a module is loaded (either during initial enumeration or via LoadLibrary calls), the system automatically calls `OnModuleLoaded` with the module name and handle.

### 2. Module Type Detection
The function uses case-insensitive string matching to detect different types of modules:

- **XInput**: `xinput*.dll` → Installs XInput hooks
- **Windows.Gaming.Input**: `gameinput*.dll` → Installs Windows.Gaming.Input hooks
- **Direct3D**: `d3d*.dll` → Logs detection (hooks not yet implemented)
- **OpenGL**: `opengl*.dll` → Logs detection (hooks not yet implemented)
- **Vulkan**: `vulkan*.dll` → Logs detection (hooks not yet implemented)
- **Steam**: `steam*.dll` → Logs detection (hooks not yet implemented)
- **Epic Games**: `eos*.dll`, `epic*.dll` → Logs detection (hooks not yet implemented)
- **NVIDIA**: `nv*.dll`, `nvidia*.dll` → Logs detection (hooks not yet implemented)
- **AMD**: `amd*.dll`, `ati*.dll` → Logs detection (hooks not yet implemented)
- **Intel**: `intel*.dll` → Logs detection (hooks not yet implemented)

### 3. Integration Points
The callback is called from:
- `EnumerateLoadedModules()` - For modules loaded before hook installation
- `LoadLibraryA_Detour()` - For new modules loaded via LoadLibraryA
- `LoadLibraryW_Detour()` - For new modules loaded via LoadLibraryW

## Usage Example

```cpp
// When a module like "xinput1_4.dll" is loaded:
// 1. Module is detected and tracked
// 2. OnModuleLoaded("xinput1_4.dll", hModule) is called
// 3. Function detects "xinput" in the name
// 4. InstallXInputHooks() is called automatically
// 5. Success/failure is logged
```

## Benefits

1. **Automatic Hook Installation**: No need to manually check for specific modules
2. **Centralized Logic**: All module detection logic is in one place
3. **Extensible**: Easy to add new module types and their corresponding hooks
4. **Comprehensive Coverage**: Handles both existing and newly loaded modules
5. **Detailed Logging**: Provides clear feedback about what hooks are being installed

## Implementation Details

### Function Signature
```cpp
void OnModuleLoaded(const std::wstring& moduleName, HMODULE hModule);
```

### Parameters
- `moduleName`: The name of the loaded module (e.g., "xinput1_4.dll")
- `hModule`: The handle to the loaded module

### Return Value
None (void function)

## Adding New Module Types

To add support for a new module type:

1. Add a new condition in the `OnModuleLoaded` function
2. Implement the corresponding hook installation function
3. Add the appropriate include for the hook header
4. Test with the target module

Example:
```cpp
// Add this to OnModuleLoaded function
else if (lowerModuleName.find(L"mymodule") != std::wstring::npos) {
    LogInfo("MyModule detected: %ws", moduleName.c_str());
    if (InstallMyModuleHooks()) {
        LogInfo("MyModule hooks installed successfully");
    } else {
        LogError("Failed to install MyModule hooks");
    }
}
```

## Current Status

✅ **Implemented**:
- XInput hooks
- Windows.Gaming.Input hooks
- Module detection and logging for other types

🔄 **Ready for Implementation**:
- Direct3D hooks
- OpenGL hooks
- Vulkan hooks
- Steam hooks
- Epic Games hooks
- NVIDIA hooks
- AMD hooks
- Intel hooks

The system is designed to be easily extensible as new hook types are implemented.
