# Build Scripts

This directory contains PowerShell scripts for building the ReShade Display Commander addons individually or together.

## Available Scripts

### Individual Addon Builds

- **`build_d3d_debug_layer.ps1`** - Builds only the D3D Debug Layer addon (`zzz_dxgi_debug_layer.addon64`)
- **`build_display_commander.ps1`** - Builds only the Display Commander addon (`zzz_display_commander.addon64`)

### Combined Builds

- **`build_all_addons.ps1`** - Builds both addons
- **`bd.ps1`** - Original build script with deployment to game directories

## Usage

Run any of these scripts from the project root directory:

```powershell
# Build only D3D Debug Layer
.\build_d3d_debug_layer.ps1

# Build only Display Commander
.\build_display_commander.ps1

# Build all addons
.\build_all_addons.ps1

# Original script with deployment
.\bd.ps1
```

## Output Files

- **Display Commander**: `build\src\addons\display_commander\zzz_display_commander.addon64`
- **D3D Debug Layer**: `build\src\addons\d3d_debug_layer\zzz_dxgi_debug_layer.addon64`

## Build Configuration

All scripts use the following configuration:
- **Generator**: Ninja
- **Build Type**: RelWithDebInfo (Release with Debug Info)
- **Experimental Tab**: Enabled

## Requirements

- CMake 3.20+
- Ninja build system
- Visual Studio Build Tools or Visual Studio
- PowerShell (for running the scripts)
