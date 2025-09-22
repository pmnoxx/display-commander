# Template Addon

A template ReShade addon with example ImGui interface. Use this as a starting point for your own addons.

## Features

This template demonstrates:

- **Basic ReShade addon structure** - Proper DLL entry point and ReShade registration
- **ImGui interface implementation** - Tabbed UI with multiple sections
- **Settings management** - Persistent configuration using ReShade's config system
- **Event handling** - ReShade event registration and handling
- **Logging** - Proper logging integration with ReShade
- **CMake build system** - Complete build configuration

## Files

- `addon.hpp` - Header file with declarations and namespace
- `addon.cpp` - Addon metadata exports
- `main_entry.cpp` - Main implementation with ReShade event handlers and UI
- `CMakeLists.txt` - Build configuration
- `README.md` - This documentation

## UI Components

The template includes three tabs:

1. **Main Tab** - Primary functionality with controls and status
2. **Settings Tab** - Configuration options and current values
3. **About Tab** - Information about the addon

## Usage

1. Copy this template to create your own addon
2. Rename the directory and files to match your addon name
3. Update the addon metadata in `addon.cpp`
4. Modify the UI in `main_entry.cpp` to implement your functionality
5. Add any additional source files as needed
6. Update the CMakeLists.txt if you add new dependencies

## Building

The template addon is automatically included in the main build system. Simply build the project as usual:

```bash
# Windows
build_display_commander.ps1

# Or use CMake directly
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

## Customization

### Adding New Controls

Add new UI controls in the `DrawMainTab()` function:

```cpp
// Example: Add a color picker
ImVec4 color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
if (ImGui::ColorEdit4("My Color", (float*)&color)) {
    // Handle color change
}
```

### Adding New Settings

1. Add atomic variables to the namespace in `addon.hpp`
2. Load/save the setting in `LoadSettings()` and `SaveSettings()`
3. Use the setting in your UI code

### Adding New Tabs

1. Create a new `DrawNewTab()` function
2. Add a tab item in the main `DrawUI()` function
3. Implement your tab's functionality

### Adding Dependencies

Update the `CMakeLists.txt` to include new libraries:

```cmake
# Add new include directories
target_include_directories(zzz_template_addon PRIVATE
    path/to/new/headers
)

# Link new libraries
target_link_libraries(zzz_template_addon PRIVATE
    new_library
)
```

## Notes

- The template uses atomic variables for thread-safe state management
- Settings are automatically saved when changed
- All logging goes through ReShade's logging system
- The addon follows ReShade's naming conventions (zzz_ prefix for alphabetical ordering)
