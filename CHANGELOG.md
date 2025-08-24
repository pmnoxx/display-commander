## v0.2.4 (2025-01-27)

- **Improved compatibility** - Dropped custom DLL requirement, making addon more accessible to all users
- **New developer toggle** - Added "Enable unstable ReShade features" checkbox for advanced users who need custom dxgi.dll
- **Input control gating** - Input blocking features now require explicit opt-in via developer toggle for safety
- **Better user experience** - Clear separation between stable and experimental features
- **Enhanced safety** - Prevents accidental use of potentially unstable features by default

## v0.2.3 (2025-01-27)

- **V-Sync 2x-4x implementation** - Implemented v-sync 2x, 3x, 4x functionality in supported games
- **Swapchain improvements** - Enhanced swapchain event handling for better v-sync compatibility
- **Window management updates** - Improved window management system for v-sync modes
- **Code optimization** - Streamlined addon structure and removed unnecessary code

## v0.2.2 (2025-01-27)

- **Critical bug fix** - Fixed sync interval crashes by preventing invalid Present calls on flip-model swap chains
- **DXGI swap effect detection** - Added detection of swap effect type to avoid invalid sync intervals
- **Multi-vblank safety** - Clamp multi-vblank (2-4) to 1 on flip-model chains to prevent crashes
- **V-Sync 2x-4x compatibility** - Only allow 2-4 v-blanks on bitblt swap effects (SEQUENTIAL/DISCARD)
- **Stability improvements** - Removed present_mode override that could cause compatibility issues
- **Crash prevention** - Fixes games crashing with sync_value == 3 (V-Sync 2x)

## v0.2.1 (2025-08-23)

- **Documentation improvements** - Added direct download links to latest builds in README for easier access
- **Build accessibility** - Users can now directly download the latest x64 and x86 builds from the README

## v0.2.0 (2025-08-23)

- **Complete UI rewrite** - New modern interface with improved tabs and better organization
- **Per-display persistent settings** - Support for up to 4 displays with individual resolution and refresh rate settings
- **Sync interval feature** - Added V-sync 2x-4x modes support for enhanced synchronization
- **Input blocking** - Implemented background input blocking functionality
- **32-bit build support** - Added support for 32-bit builds with dedicated build script
- **Build system improvements** - Removed renodx- prefix from build outputs, improved build scripts and CI
- **Enhanced monitor management** - Better multi-monitor support with per-display settings persistence
- **Bug fixes** - Fixed v-sync 2x-4x modes and various build issues
- **Documentation updates** - Added comprehensive known bugs section and improved README

## v0.1.1 (2025-08-23)

- Added FPS counter with Min FPS and 1% Low metrics to Important Information section
- Enhanced performance monitoring with real-time FPS statistics

## v0.1.0 (2025-08-23)

- Initial release of Display Commander addon
- Added Audio settings (volume, manual mute, background mute options)
- Unmute-on-start when "Audio Mute" is unchecked
- Custom FPS limiter integration and background limit
- Basic window mode/resolution controls and monitor targeting
- NVIDIA Reflex toggle and status display


