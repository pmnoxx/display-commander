# ForkAwesome Icon Expansion Guide

## Summary

This guide explains how to add **44 additional icons** to Display Commander, expanding from 15 to 59 total glyphs.

**Size Impact:** ~9 KB increase in `reshade64.dll` (negligible)

## Quick Reference: Proposed Icons by Category

### 🖥️ Display & Monitor (8 icons)
- Desktop, TV, Expand, Compress, Resize, Maximize, Minimize, Restore

### ⚙️ Settings & Configuration (5 icons)
- Cog, Wrench, Sliders, Cogs, Tachometer

### 🔘 Toggle & State (6 icons)
- Toggle On/Off, Eye/Eye-Slash, Check-Square, Square-O

### 🎮 Gaming & Input (4 icons)
- Gamepad, Keyboard, Mouse, Crosshairs

### ℹ️ Information & Help (4 icons)
- Info, Help, Error, Tip

### 🔊 Audio & Volume (3 icons)
- Volume Up, Volume Down, Volume Off

### ⚡ Performance & Power (4 icons)
- Power, Battery, Boost, Timer

### 📋 Navigation & Control (7 icons)
- Menu, List, List-Detailed, 4× Chevrons (Up/Down/Left/Right)

### 🟢 Status & Indicators (5 icons)
- Circle, Circle-O, Success, Fail, Loading

### ☕ Social & Support (2 icons)
- Coffee, Heart

## Implementation Steps

### Step 1: Backup Current Files
```bash
cd external/reshade/res/fonts
cp forkawesome.ifs forkawesome.ifs.backup
cp forkawesome.h forkawesome.h.backup
cp forkawesome.inl forkawesome.inl.backup
```

### Step 2: Edit forkawesome.ifs

Open `external/reshade/res/fonts/forkawesome.ifs` and add the new glyphs from `docs/forkawesome_additions.xml` inside the `<glyphs>` section:

```xml
<glyphs>
    <!-- Existing 15 glyphs stay here -->
    <glyph orgId="61442" newId="61442" orgName="search" newName="search"/>
    ...

    <!-- ADD NEW GLYPHS HERE -->
    <glyph orgId="61704" newId="61704" orgName="desktop" newName="desktop"/>
    <glyph orgId="61815" newId="61815" orgName="tv" newName="tv"/>
    <!-- ... rest from forkawesome_additions.xml ... -->
</glyphs>
```

### Step 3: Regenerate Font Files (ImGuiFontStudio Required)

**Option A: Using ImGuiFontStudio**
1. Download ImGuiFontStudio from https://github.com/aiekick/ImGuiFontStudio
2. Open `forkawesome.ifs` in ImGuiFontStudio
3. Verify all glyphs are loaded correctly
4. Click "Generate" button
5. This will regenerate:
   - `forkawesome.inl` (compressed font data)
   - `forkawesome.h` (icon definitions)

**Option B: Request Regeneration**
If you don't have ImGuiFontStudio set up, you can provide the `.ifs` file and request that someone regenerate the files for you.

### Step 4: Update Display Commander's Icon Header

Copy the new icon definitions from `external/reshade/res/fonts/forkawesome.h` to `src/addons/display_commander/ui/forkawesome.h`.

Or use the preview file: `docs/forkawesome_new_icons.h`

### Step 5: Rebuild ReShade

```bash
# Windows
./bd.ps1

# Or build 32-bit
./bd32.ps1
```

### Step 6: Verify Icons in UI

Test that icons render correctly:
```cpp
// In any UI file
ImGui::Text(ICON_FK_DESKTOP " Display Settings");
ImGui::Text(ICON_FK_GAMEPAD " Controller");
ImGui::Text(ICON_FK_TACHOMETER " Performance");
```

## Real-World Usage Examples

### Main Tab - Display Section
```cpp
ImGui::SeparatorText(ICON_FK_DESKTOP " Display Configuration");

if (ImGui::Button(ICON_FK_EXPAND " Go Fullscreen")) {
    // Fullscreen logic
}

if (ImGui::Button(ICON_FK_COMPRESS " Exit Fullscreen")) {
    // Windowed logic
}

ImGui::Text(ICON_FK_RESIZE " Resolution: %dx%d", width, height);
```

### Experimental Tab - Performance
```cpp
ImGui::SeparatorText(ICON_FK_TACHOMETER " Performance Settings");

ImGui::Text(ICON_FK_POWER " Power Mode:");
ImGui::SameLine();
ImGui::Combo("##power", &power_mode, "Balanced\0Performance\0Power Saver\0");

ImGui::Text(ICON_FK_TIMER " Frame Time: %.2fms", frame_time);
```

### Settings with Toggle States
```cpp
bool enabled = GetFeatureEnabled();
ImGui::Text(enabled ? ICON_FK_TOGGLE_ON : ICON_FK_TOGGLE_OFF);
ImGui::SameLine();
ImGui::Checkbox("Enable Feature", &enabled);
```

### Audio Controls
```cpp
ImGui::SeparatorText(ICON_FK_VOLUME_UP " Audio Settings");

const char* volume_icon = muted ? ICON_FK_VOLUME_OFF :
                         volume > 66 ? ICON_FK_VOLUME_UP :
                         volume > 33 ? ICON_FK_VOLUME_DOWN :
                         ICON_FK_VOLUME_OFF;

ImGui::Text("%s Volume: %d%%", volume_icon, volume);
```

### Input Remapping Widget
```cpp
ImGui::TabItemButton(ICON_FK_GAMEPAD " Controller");
ImGui::TabItemButton(ICON_FK_KEYBOARD " Keyboard");
ImGui::TabItemButton(ICON_FK_MOUSE " Mouse");
```

### Status Indicators
```cpp
// Connection status
if (connected) {
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
                      ICON_FK_SUCCESS " Connected");
} else if (connecting) {
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                      ICON_FK_LOADING " Connecting...");
} else {
    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
                      ICON_FK_FAIL " Disconnected");
}
```

### Help & Information
```cpp
ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), ICON_FK_INFO);
if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("This is helpful information");
}

ImGui::SameLine();
ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), ICON_FK_TIP);
if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Pro tip: Use keyboard shortcuts!");
}
```

### Ko-fi Support Button (Your Actual Use Case!)
```cpp
// Perfect replacement for your current Ko-fi button
if (ImGui::Button(ICON_FK_COFFEE " Buy me a coffee on Ko-fi")) {
    ShellExecuteA(NULL, "open", "https://ko-fi.com/yourpage", NULL, NULL, SW_SHOWNORMAL);
}

// Or with heart icon
if (ImGui::Button(ICON_FK_HEART " Support Development")) {
    ShellExecuteA(NULL, "open", "https://ko-fi.com/yourpage", NULL, NULL, SW_SHOWNORMAL);
}

// Compact version for smaller UI space
ImGui::SmallButton(ICON_FK_COFFEE);
if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Support on Ko-fi");
}
```

## Benefits

1. **Better Visual Communication**: Icons instantly convey meaning
2. **Improved UX**: Users recognize icons faster than text
3. **Professional Appearance**: Modern UI with proper iconography
4. **Consistency**: Standard icons across all tabs
5. **Space Efficiency**: Icons take less space than words
6. **Accessibility**: Icons + text labels help all users

## Size Comparison

| Configuration | Glyphs | Approx Size | Increase |
|--------------|--------|-------------|----------|
| Current | 15 | ~3 KB | - |
| Proposed | 59 | ~12 KB | +9 KB |
| Full FA | 740 | ~150 KB | +147 KB |

## Notes

- The compressed font data uses Base85 encoding
- Each glyph adds ~150-250 bytes when compressed
- Icon rendering has no performance impact
- All ForkAwesome icons are MIT licensed
- Icons work in any ImGui context

## Troubleshooting

**Icons show as boxes (□):**
- Font wasn't regenerated properly
- Check that glyph IDs match between .ifs and .h files

**Build errors:**
- Make sure you regenerated `.inl` file
- Verify `.h` file has matching `ICON_MIN_FK` and `ICON_MAX_FK` values

**Icons don't render:**
- Ensure you're using `u8` string literal prefix
- Check that font is loaded before rendering

## Alternative: Full ForkAwesome Set

If you want ALL 740 icons (not recommended for size):

1. Download complete ForkAwesome font from https://forkawesome.github.io/Fork-Awesome/
2. Generate full glyph set with ImGuiFontStudio
3. Accept ~150 KB size increase

This gives you every possible icon but is overkill for most addons.

