# Special-K Gamepad Chord Combinations

This document lists all gamepad chord combinations (key combinations) used by Special-K for various functions.

## Overview

Special-K uses gamepad chords to provide additional functionality without interfering with game input. Chords are activated by holding a special button (Guide/PlayStation button) and pressing another button simultaneously.

## Enabling Gamepad Chords

Gamepad chords must be enabled in Special-K's control panel:
- **Setting**: `Enable Gamepad Chords using (Xbox / PlayStation)`
- **Config Path**: `config.input.gamepad.scepad.enhanced_ps_button`
- **Note**: Requires a valid UI controller slot (not set to "Nothing")

## XInput/Generic Gamepad Chords

These chords work with Xbox controllers and any XInput-compatible gamepad using the **Guide button** (Xbox button).

### System Functions

| Chord | Action | Description |
|-------|--------|-------------|
| **Guide + Y** (release) | Power Off Controller | Powers off the controller when Y is released while Guide is held |
| **Guide + X** | Show Game Window | Brings the game window to foreground and restores if minimized |
| **Guide + B** | Show Last App Window | Switches to the last foreground application window |
| **Guide** (release alone) | Toggle UI | Toggles Special-K control panel visibility and navigation |

### Media Controls

| Chord | Action | Description |
|-------|--------|-------------|
| **Guide + Left Thumbstick** | Media Play/Pause | Sends VK_MEDIA_PLAY_PAUSE keyboard event |
| **Guide + Left Shoulder (LB)** | Media Previous Track | Sends VK_MEDIA_PREV_TRACK keyboard event |
| **Guide + Right Shoulder (RB)** | Media Next Track | Sends VK_MEDIA_NEXT_TRACK keyboard event |

### Audio Controls

| Chord | Action | Description |
|-------|--------|-------------|
| **Guide + D-Pad Up** | Volume Up | Increases system volume by 10.0 (`Sound.Volume += 10.0`) |
| **Guide + D-Pad Down** | Volume Down | Decreases system volume by 10.0 (`Sound.Volume -= 10.0`) |

### HDR Controls

| Chord | Action | Description |
|-------|--------|-------------|
| **Guide + D-Pad Right** | Increase HDR Luminance | Increases HDR luminance by 0.125 (`HDR.Luminance += 0.125`) |
| **Guide + D-Pad Left** | Decrease HDR Luminance | Decreases HDR luminance by 0.125 (`HDR.Luminance -= 0.125`) |

### Screenshot Functions

| Chord | Action | Description |
|-------|--------|-------------|
| **Guide + Back** | Take Screenshot | Takes a screenshot using Steam API |
| **Guide + Start** | HUD Free Screenshot | Triggers HUD-free screenshot (D3D11 only, requires tracking) |

### Window Management

| Chord | Action | Description |
|-------|--------|-------------|
| **Guide + Left Trigger** | Restore Game Window | Restores game window if minimized |
| **Guide + Right Trigger** | Minimize Game Window | Minimizes game window and handles background window positioning |

### Unassigned

| Chord | Action | Description |
|-------|--------|-------------|
| **Guide + A** | Unassigned | Currently unassigned (reserved for screensaver activation in SKIF) |

## PlayStation Controller Chords

These chords work with PlayStation controllers (DualShock 4, DualSense) using the **PlayStation button**.

### LatentSync Controls

| Chord | Action | Description |
|-------|--------|-------------|
| **PlayStation + L3** | Toggle FCAT Bars | Toggles LatentSync FCAT bar visibility |
| **PlayStation + D-Pad Up** | Adjust Tear Location (Up) | Adjusts LatentSync tear location upward (5x `--` commands + resync rate adjustment) |
| **PlayStation + D-Pad Down** | Adjust Tear Location (Down) | Adjusts LatentSync tear location downward (5x `++` commands + resync rate adjustment) |

## Alternative UI Toggle

| Chord | Action | Description |
|-------|--------|-------------|
| **Back + Start** | Toggle UI | Toggles Special-K control panel (only if triggers are not pressed, to avoid interference with Final Fantasy X soft reset) |

## VLC-Specific Controls

When VLC media player is detected as the host application, the following **non-chord** buttons provide media control (no Guide button required):

| Button | Action | Description |
|--------|--------|-------------|
| **A** | Media Play/Pause | Sends VK_MEDIA_PLAY_PAUSE keyboard event |
| **D-Pad Left** | Previous Track | Sends VK_LEFT keyboard event |
| **D-Pad Right** | Next Track | Sends VK_RIGHT keyboard event |
| **D-Pad Down** | Volume Down | Sends VK_DOWN keyboard event |
| **D-Pad Up** | Volume Up | Sends VK_UP keyboard event |
| **Left Shoulder** | Previous Track | Sends VK_MEDIA_PREV_TRACK keyboard event |
| **Right Shoulder** | Next Track | Sends VK_MEDIA_NEXT_TRACK keyboard event |

## Implementation Details

### Chord Activation Logic

- Chords are processed when `config.input.gamepad.scepad.enhanced_ps_button` is enabled
- Chord processing requires special buttons to be allowed (`bAllowSpecialButtons`)
- A `bChordActivated` flag prevents chord repeats
- Guide button press enters "chord processing mode"
- Guide button release (without other buttons) toggles UI if no chord was activated

### Frame Tracking

- PlayStation controller chords use frame-based tracking (`ullLastChordFrame`, `ullFirstChordFrame`)
- Prevents accidental activation and handles timing correctly

### Button State Detection

- Uses `_JustPressed` and `_JustReleased` helper functions to detect button state changes
- Prevents repeated activation on the same button press
- Checks both current and previous button states

## Code References

- **XInput Chords**: `external-src/SpecialK/include/imgui/imgui_user.inl` (lines ~1980-2360)
- **PlayStation Chords**: `external-src/SpecialK/src/input/sce_pad.cpp` (lines ~313-438)
- **UI Configuration**: `external-src/SpecialK/src/control_panel/cfg_input.cpp` (lines ~1355-1361)

## Notes

- Chords work even when the game window is not in focus (if `disabled_to_game` allows it)
- Chord input does not count for screensaver deactivation
- Some chords may be disabled in certain games or scenarios
- VLC-specific controls only work when VLC is the host application
- PlayStation button chords require HID polling support

