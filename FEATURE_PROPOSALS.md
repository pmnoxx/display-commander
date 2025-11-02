# Feature Proposals

This document tracks proposed features and ideas for future development. These are not planned features but rather ideas to keep track of for potential future implementation.

## Finished prosals

• ADHD Multi-screen mode

• XInput support

• Reflex with low-latency mode + boost

• AntiLag II/Xell support through fakenvapi

• Dual Sense to XInput remapping + Hiding HID devices from page

• DLSS-FG compatible frame limiter (using native reflex)

• Flip State Query support for games without Streamline

• DLSS Preset override game default/A/B/C/D/...

• VBlank Scanline Sync

• Add NVIDIA Ansel detection / supression feature

• Screensaver control options: "default behavior / Disable screensaver while game is in foreground / disable screensaver while game is running"

• DX9 -> DX9E swap chain upgrade

## Proposals

• PCL stats reporting (show PCV-AL latency in nvidia overlay)

• Flip State Query support for games running Streamline

• DLSS-FG compatible frame limiter (using custom fps limiter)

• Low latency custom frame limiter

• Native Dual Sense support (adding anti-dead zones, etc.)

• VRR detection / current reflex rate dection

• Track/present games setting their own gamma using IDXGIOutput::SetGammaControl, which could break other games. For example, BioShock 2.

• DLSS DLL Loader (https://github.com/beeradmoore/dlss-swapper-manifest-builder)

• DLSS Quality Preset override

• Add chimes for taking screenshots from DS controller.

• Rewrite gamepad remapping / gamepad chimes UI.

• Add UI for "enable HDR" for monitors, to enable at start of game, turn off at game close.

## Low Priority Proposals

• Add crashes interception, so we can print to logs when crash occurs.

• Bluetooth DualSense vibration support

• Gamepad keys remapping

• Gamepad keys to keyboard

• Record video gamepad chord

• DX9/OPENGL/VULKAN -> DX11 proxy

• Swap Chain upgrades to Flip State Swap chain
