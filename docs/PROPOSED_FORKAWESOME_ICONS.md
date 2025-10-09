# Proposed ForkAwesome Icons for Display Commander

## Current Usage Analysis
Display Commander currently uses:
- ➕ ➖ `ICON_FK_PLUS`, `ICON_FK_MINUS` - Add/remove, window controls
- ✔️ `ICON_FK_OK` - Apply changes
- ⚠️ `ICON_FK_WARNING` - Warnings
- 🔄 `ICON_FK_REFRESH` - Reset stats
- 🔍 `ICON_FK_SEARCH` - Performance overlay
- ↶ `ICON_FK_UNDO` - Reset to defaults

## Proposed Additions (44 icons)

### 🖥️ Display & Monitor Management (8 icons)

| Emoji | Icon Name | Usage |
|-------|-----------|-------|
| 🖥️ | `ICON_FK_DESKTOP` | Desktop/Monitor settings |
| 📺 | `ICON_FK_TV` | TV/Display mode |
| ⛶ | `ICON_FK_EXPAND` | Fullscreen/Expand |
| ⛶ | `ICON_FK_COMPRESS` | Exit fullscreen/Windowed |
| ↔️ | `ICON_FK_RESIZE` | Resize/Arrows |
| 🗖 | `ICON_FK_MAXIMIZE` | Maximize window |
| 🗕 | `ICON_FK_MINIMIZE_WINDOW` | Minimize window |
| 🗗 | `ICON_FK_RESTORE` | Restore window |

```xml
<glyph orgId="61704" newId="61704" orgName="desktop" newName="desktop"/>
<glyph orgId="61815" newId="61815" orgName="tv" newName="tv"/>
<glyph orgId="61534" newId="61534" orgName="expand" newName="expand"/>
<glyph orgId="61532" newId="61532" orgName="compress" newName="compress"/>
<glyph orgId="61730" newId="61730" orgName="arrows-alt" newName="resize"/>
<glyph orgId="61842" newId="61842" orgName="window-maximize" newName="maximize"/>
<glyph orgId="61843" newId="61843" orgName="window-minimize" newName="minimize-window"/>
<glyph orgId="61841" newId="61841" orgName="window-restore" newName="restore"/>
```

### ⚙️ Settings & Configuration (5 icons)

| Emoji | Icon Name | Usage |
|-------|-----------|-------|
| ⚙️ | `ICON_FK_COG` | Settings/Configuration |
| 🔧 | `ICON_FK_WRENCH` | Tools/Repair |
| 🎚️ | `ICON_FK_SLIDERS` | Sliders/Adjustments |
| ⚙️⚙️ | `ICON_FK_COGS` | Advanced settings |
| 📊 | `ICON_FK_TACHOMETER` | Performance gauge |

```xml
<glyph orgId="61459" newId="61459" orgName="cog" newName="cog"/>
<glyph orgId="61613" newId="61613" orgName="wrench" newName="wrench"/>
<glyph orgId="61584" newId="61584" orgName="sliders" newName="sliders"/>
<glyph orgId="61541" newId="61541" orgName="cogs" newName="cogs"/>
<glyph orgId="61608" newId="61608" orgName="tachometer" newName="tachometer"/>
```

### 🔘 Toggle & State Icons (6 icons)

| Emoji | Icon Name | Usage |
|-------|-----------|-------|
| ⏾ | `ICON_FK_TOGGLE_OFF` | Toggle disabled |
| ⏾ | `ICON_FK_TOGGLE_ON` | Toggle enabled |
| 👁️ | `ICON_FK_EYE` | Visible/Show |
| 👁️‍🗨️ | `ICON_FK_EYE_SLASH` | Hidden/Hide |
| ☑️ | `ICON_FK_CHECK_SQUARE` | Checkbox checked |
| ☐ | `ICON_FK_SQUARE_O` | Checkbox unchecked |

```xml
<glyph orgId="61668" newId="61668" orgName="toggle-off" newName="toggle-off"/>
<glyph orgId="61669" newId="61669" orgName="toggle-on" newName="toggle-on"/>
<glyph orgId="61587" newId="61587" orgName="eye" newName="eye"/>
<glyph orgId="61588" newId="61588" orgName="eye-slash" newName="eye-slash"/>
<glyph orgId="61452" newId="61452" orgName="check-square" newName="check-square"/>
<glyph orgId="61461" newId="61461" orgName="square-o" newName="square-o"/>
```

### 🎮 Gaming & Input (4 icons)

| Emoji | Icon Name | Usage |
|-------|-----------|-------|
| 🎮 | `ICON_FK_GAMEPAD` | Controller/Gamepad |
| ⌨️ | `ICON_FK_KEYBOARD` | Keyboard input |
| 🖱️ | `ICON_FK_MOUSE` | Mouse pointer |
| ⊕ | `ICON_FK_CROSSHAIRS` | Crosshair/Target |

```xml
<glyph orgId="61671" newId="61671" orgName="gamepad" newName="gamepad"/>
<glyph orgId="61724" newId="61724" orgName="keyboard-o" newName="keyboard"/>
<glyph orgId="61925" newId="61925" orgName="mouse-pointer" newName="mouse"/>
<glyph orgId="61483" newId="61483" orgName="crosshairs" newName="crosshairs"/>
```

### ℹ️ Information & Help (4 icons)

| Emoji | Icon Name | Usage |
|-------|-----------|-------|
| ℹ️ | `ICON_FK_INFO` | Information |
| ❓ | `ICON_FK_HELP` | Help/Question |
| ❗ | `ICON_FK_ERROR` | Error/Alert |
| 💡 | `ICON_FK_TIP` | Tip/Suggestion |

```xml
<glyph orgId="61530" newId="61530" orgName="info-circle" newName="info"/>
<glyph orgId="61529" newId="61529" orgName="question-circle" newName="help"/>
<glyph orgId="61546" newId="61546" orgName="exclamation-circle" newName="error"/>
<glyph orgId="61675" newId="61675" orgName="lightbulb-o" newName="tip"/>
```

### 🔊 Audio & Volume (3 icons)

| Emoji | Icon Name | Usage |
|-------|-----------|-------|
| 🔊 | `ICON_FK_VOLUME_UP` | Volume high |
| 🔉 | `ICON_FK_VOLUME_DOWN` | Volume low |
| 🔇 | `ICON_FK_VOLUME_OFF` | Muted/Silent |

```xml
<glyph orgId="61480" newId="61480" orgName="volume-up" newName="volume-up"/>
<glyph orgId="61479" newId="61479" orgName="volume-down" newName="volume-down"/>
<glyph orgId="61478" newId="61478" orgName="volume-off" newName="volume-off"/>
```

### ⚡ Performance & Power (4 icons)

| Emoji | Icon Name | Usage |
|-------|-----------|-------|
| ⚡ | `ICON_FK_POWER` | Power/Performance |
| 🔋 | `ICON_FK_BATTERY` | Battery/Power saving |
| 🚀 | `ICON_FK_BOOST` | Boost/Rocket mode |
| ⏱️ | `ICON_FK_TIMER` | Timer/Clock/Latency |

```xml
<glyph orgId="61671" newId="61671" orgName="bolt" newName="power"/>
<glyph orgId="61760" newId="61760" orgName="battery-full" newName="battery"/>
<glyph orgId="61749" newId="61749" orgName="rocket" newName="boost"/>
<glyph orgId="61463" newId="61463" orgName="clock-o" newName="timer"/>
```

### 📋 Navigation & Control (7 icons)

| Emoji | Icon Name | Usage |
|-------|-----------|-------|
| ☰ | `ICON_FK_MENU` | Menu/Hamburger |
| 📋 | `ICON_FK_LIST` | List view |
| 📄 | `ICON_FK_LIST_DETAILED` | Detailed list |
| › | `ICON_FK_CHEVRON_RIGHT` | Right arrow |
| ‹ | `ICON_FK_CHEVRON_LEFT` | Left arrow |
| ∨ | `ICON_FK_CHEVRON_DOWN` | Down arrow |
| ∧ | `ICON_FK_CHEVRON_UP` | Up arrow |

```xml
<glyph orgId="61641" newId="61641" orgName="bars" newName="menu"/>
<glyph orgId="61505" newId="61505" orgName="list" newName="list"/>
<glyph orgId="61568" newId="61568" orgName="th-list" newName="list-detailed"/>
<glyph orgId="61521" newId="61521" orgName="chevron-right" newName="chevron-right"/>
<glyph orgId="61520" newId="61520" orgName="chevron-left" newName="chevron-left"/>
<glyph orgId="61523" newId="61523" orgName="chevron-down" newName="chevron-down"/>
<glyph orgId="61522" newId="61522" orgName="chevron-up" newName="chevron-up"/>
```

### 🟢 Status & Indicators (5 icons)

| Emoji | Icon Name | Usage |
|-------|-----------|-------|
| 🔴 | `ICON_FK_CIRCLE` | Filled circle/Indicator |
| ⭕ | `ICON_FK_CIRCLE_O` | Empty circle |
| ✅ | `ICON_FK_SUCCESS` | Success/Check |
| ❌ | `ICON_FK_FAIL` | Fail/Error |
| ↻ | `ICON_FK_LOADING` | Loading spinner |

```xml
<glyph orgId="61713" newId="61713" orgName="circle" newName="circle"/>
<glyph orgId="61708" newId="61708" orgName="circle-o" newName="circle-o"/>
<glyph orgId="61445" newId="61445" orgName="check-circle" newName="success"/>
<glyph orgId="61527" newId="61527" orgName="times-circle" newName="fail"/>
<glyph orgId="61680" newId="61680" orgName="spinner" newName="loading"/>
```

### ☕ Social & Support (2 icons)

| Emoji | Icon Name | Usage |
|-------|-----------|-------|
| ☕ | `ICON_FK_COFFEE` | Coffee/Ko-fi donations |
| ❤️ | `ICON_FK_HEART` | Heart/Support/Favorite |

```xml
<glyph orgId="61563" newId="61563" orgName="coffee" newName="coffee"/>
<glyph orgId="61444" newId="61444" orgName="heart" newName="heart"/>
```

## Usage Examples

### 🖥️ Display Management Tab
```cpp
ImGui::Text(ICON_FK_DESKTOP " Monitor Settings");      // 🖥️ Monitor Settings
if (ImGui::Button(ICON_FK_EXPAND " Go Fullscreen")) {  // ⛶ Go Fullscreen
    // ...
}
if (ImGui::Button(ICON_FK_RESIZE " Change Resolution")) { // ↔️ Change Resolution
    // ...
}
if (ImGui::Button(ICON_FK_MAXIMIZE " Maximize")) {     // 🗖 Maximize
    // ...
}
```

### ⚙️ Settings Widgets
```cpp
ImGui::SeparatorText(ICON_FK_COG " Configuration");         // ⚙️ Configuration
ImGui::Text(ICON_FK_SLIDERS " Adjust Settings");           // 🎚️ Adjust Settings
ImGui::Text(ICON_FK_TACHOMETER " Performance Monitor");    // 📊 Performance Monitor
ImGui::Text(ICON_FK_WRENCH " Tools");                      // 🔧 Tools
```

### 🔘 Toggle States
```cpp
// Toggle switch
ImGui::Text(enabled ? ICON_FK_TOGGLE_ON : ICON_FK_TOGGLE_OFF); // ⏾ ON/OFF

// Visibility
ImGui::Text(visible ? ICON_FK_EYE : ICON_FK_EYE_SLASH);       // 👁️ Show/Hide

// Checkboxes
ImGui::Text(checked ? ICON_FK_CHECK_SQUARE : ICON_FK_SQUARE_O); // ☑️/☐
```

### 🎮 Gaming Features
```cpp
ImGui::TabItemButton(ICON_FK_GAMEPAD " Controller");  // 🎮 Controller
ImGui::TabItemButton(ICON_FK_KEYBOARD " Keyboard");   // ⌨️ Keyboard
ImGui::TabItemButton(ICON_FK_MOUSE " Mouse");         // 🖱️ Mouse
ImGui::Text(ICON_FK_CROSSHAIRS " Aim Assist");        // ⊕ Aim Assist
```

### 🔊 Audio Controls
```cpp
const char* volume_icon = muted ? ICON_FK_VOLUME_OFF :
                         volume > 66 ? ICON_FK_VOLUME_UP :
                         ICON_FK_VOLUME_DOWN;
// 🔇 Muted or 🔊 High or 🔉 Low

ImGui::Text("%s Volume: %d%%", volume_icon, volume);
```

### ⚡ Performance & Power
```cpp
ImGui::Text(ICON_FK_TACHOMETER " FPS: %d", fps);      // 📊 FPS: 144
ImGui::Text(ICON_FK_TIMER " Latency: %.1fms", ms);    // ⏱️ Latency: 8.5ms
ImGui::Text(ICON_FK_POWER " Performance Mode");       // ⚡ Performance Mode
ImGui::Text(ICON_FK_BOOST " Boost Enabled");          // 🚀 Boost Enabled
```

### ℹ️ Information & Status
```cpp
// Help tooltips
ImGui::TextColored(info_color, ICON_FK_INFO);         // ℹ️
if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Additional information");

ImGui::TextColored(tip_color, ICON_FK_TIP);           // 💡
if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Pro tip!");

// Status indicators
if (connected) {
    ImGui::TextColored(green, ICON_FK_SUCCESS " Connected");    // ✅ Connected
} else if (connecting) {
    ImGui::Text(ICON_FK_LOADING " Connecting...");             // ↻ Connecting...
} else {
    ImGui::TextColored(red, ICON_FK_FAIL " Disconnected");     // ❌ Disconnected
}
```

### 📋 Navigation
```cpp
if (ImGui::Button(ICON_FK_CHEVRON_LEFT)) { /* Previous */ }   // ‹
if (ImGui::Button(ICON_FK_CHEVRON_RIGHT)) { /* Next */ }      // ›
if (ImGui::Button(ICON_FK_MENU " Menu")) { /* Open menu */ }  // ☰ Menu
```

### ☕ Social & Support
```cpp
// Ko-fi donation button (PERFECT FOR YOUR USE CASE!)
if (ImGui::Button(ICON_FK_COFFEE " Buy me a coffee on Ko-fi")) {  // ☕ Buy me a coffee
    ShellExecuteA(NULL, "open", "https://ko-fi.com/yourpage", NULL, NULL, SW_SHOWNORMAL);
}

// Support button with heart
if (ImGui::Button(ICON_FK_HEART " Support Development")) {  // ❤️ Support Development
    // ...
}

// Favorite/like feature
ImGui::Text(ICON_FK_HEART " %d favorites", favorite_count);  // ❤️ 42 favorites
```

## 📊 Size Impact
- **Current:** 15 glyphs = ~3 KB
- **After adding 44 more:** 59 glyphs = ~12 KB
- **Net increase:** ~9 KB (0.009 MB) - completely negligible!

## Implementation Steps
1. Add these glyph entries to `external/reshade/res/fonts/forkawesome.ifs`
2. Use ImGuiFontStudio to regenerate `forkawesome.inl` and `forkawesome.h`
3. Copy new icon defines to `src/addons/display_commander/ui/forkawesome.h`
4. Rebuild ReShade

