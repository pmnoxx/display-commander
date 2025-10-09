// Preview: New icon definitions that will be generated after updating forkawesome.ifs
// These will be added to both:
//   - external/reshade/res/fonts/forkawesome.h (ReShade core)
//   - src/addons/display_commander/ui/forkawesome.h (Display Commander addon)

#pragma once

// EXISTING ICONS (keep these)
#define ICON_FK_CANCEL u8"\uf00d"
#define ICON_FK_FILE u8"\uf016"
#define ICON_FK_FILE_CODE u8"\uf1c9"
#define ICON_FK_FILE_IMAGE u8"\uf1c5"
#define ICON_FK_FLOPPY u8"\uf0c7"
#define ICON_FK_FOLDER u8"\uf114"
#define ICON_FK_FOLDER_OPEN u8"\uf115"
#define ICON_FK_MINUS u8"\uf068"
#define ICON_FK_OK u8"\uf00c"
#define ICON_FK_PENCIL u8"\uf040"
#define ICON_FK_PLUS u8"\uf067"
#define ICON_FK_REFRESH u8"\uf021"
#define ICON_FK_SEARCH u8"\uf002"
#define ICON_FK_UNDO u8"\uf0e2"
#define ICON_FK_WARNING u8"\uf071"

// NEW ADDITIONS - Display & Monitor
#define ICON_FK_DESKTOP u8"\uf108"          // 💻 Desktop monitor
#define ICON_FK_TV u8"\uf26c"               // 📺 TV/Display
#define ICON_FK_EXPAND u8"\uf065"           // ⛶ Fullscreen
#define ICON_FK_COMPRESS u8"\uf066"         // ⛶ Exit fullscreen
#define ICON_FK_RESIZE u8"\uf0b2"           // ⇔ Resize arrows
#define ICON_FK_MAXIMIZE u8"\uf2d0"         // 🗖 Maximize window
#define ICON_FK_MINIMIZE_WINDOW u8"\uf2d1"  // 🗕 Minimize window
#define ICON_FK_RESTORE u8"\uf2d2"          // 🗗 Restore window

// NEW ADDITIONS - Settings & Configuration
#define ICON_FK_COG u8"\uf013"              // ⚙ Settings gear
#define ICON_FK_WRENCH u8"\uf0ad"           // 🔧 Tools/config
#define ICON_FK_SLIDERS u8"\uf1de"          // 🎚 Sliders control
#define ICON_FK_COGS u8"\uf085"             // ⚙⚙ Advanced settings
#define ICON_FK_TACHOMETER u8"\uf0e4"       // 🕐 Performance gauge

// NEW ADDITIONS - Toggle & State
#define ICON_FK_TOGGLE_OFF u8"\uf204"       // ⏾ Toggle disabled
#define ICON_FK_TOGGLE_ON u8"\uf205"        // ⏾ Toggle enabled
#define ICON_FK_EYE u8"\uf06e"              // 👁 Visible
#define ICON_FK_EYE_SLASH u8"\uf070"        // 👁‍🗨 Hidden
#define ICON_FK_CHECK_SQUARE u8"\uf14a"     // ☑ Checkbox checked
#define ICON_FK_SQUARE_O u8"\uf096"         // ☐ Checkbox unchecked

// NEW ADDITIONS - Gaming & Input
#define ICON_FK_GAMEPAD u8"\uf11b"          // 🎮 Game controller
#define ICON_FK_KEYBOARD u8"\uf11c"         // ⌨ Keyboard
#define ICON_FK_MOUSE u8"\uf245"            // 🖱 Mouse pointer
#define ICON_FK_CROSSHAIRS u8"\uf05b"       // ⊕ Crosshair/target

// NEW ADDITIONS - Information & Help
#define ICON_FK_INFO u8"\uf05a"             // ℹ Information
#define ICON_FK_HELP u8"\uf059"             // ❓ Help
#define ICON_FK_ERROR u8"\uf06a"            // ⚠ Error
#define ICON_FK_TIP u8"\uf0eb"              // 💡 Tip/lightbulb

// NEW ADDITIONS - Audio & Volume
#define ICON_FK_VOLUME_UP u8"\uf028"        // 🔊 Volume high
#define ICON_FK_VOLUME_DOWN u8"\uf027"      // 🔉 Volume low
#define ICON_FK_VOLUME_OFF u8"\uf026"       // 🔇 Muted

// NEW ADDITIONS - Performance & Power
#define ICON_FK_POWER u8"\uf0e7"            // ⚡ Power/performance
#define ICON_FK_BATTERY u8"\uf240"          // 🔋 Battery/power saving
#define ICON_FK_BOOST u8"\uf135"            // 🚀 Boost mode
#define ICON_FK_TIMER u8"\uf017"            // ⏱ Timer/clock/latency

// NEW ADDITIONS - Navigation & Control
#define ICON_FK_MENU u8"\uf0c9"             // ≡ Menu bars
#define ICON_FK_LIST u8"\uf03a"             // ▤ List
#define ICON_FK_LIST_DETAILED u8"\uf00b"    // ☰ Detailed list
#define ICON_FK_CHEVRON_RIGHT u8"\uf054"    // › Right arrow
#define ICON_FK_CHEVRON_LEFT u8"\uf053"     // ‹ Left arrow
#define ICON_FK_CHEVRON_DOWN u8"\uf078"     // ∨ Down arrow
#define ICON_FK_CHEVRON_UP u8"\uf077"       // ∧ Up arrow

// NEW ADDITIONS - Status & Indicators
#define ICON_FK_CIRCLE u8"\uf111"           // ● Filled circle
#define ICON_FK_CIRCLE_O u8"\uf10c"         // ○ Empty circle
#define ICON_FK_SUCCESS u8"\uf058"          // ✓ Success/check circle
#define ICON_FK_FAIL u8"\uf057"             // ✗ Fail/times circle
#define ICON_FK_LOADING u8"\uf110"          // ↻ Loading spinner

// NEW ADDITIONS - Social & Support
#define ICON_FK_COFFEE u8"\uf0f4"           // ☕ Coffee/Ko-fi
#define ICON_FK_HEART u8"\uf004"            // ❤️ Heart/Support

// Usage Examples:
//
// Display tab:
//   ImGui::Text(ICON_FK_DESKTOP " Display Settings");
//   if (ImGui::Button(ICON_FK_EXPAND " Fullscreen")) { ... }
//
// Audio section:
//   ImGui::Text(muted ? ICON_FK_VOLUME_OFF : ICON_FK_VOLUME_UP);
//
// Performance overlay:
//   ImGui::Text(ICON_FK_TACHOMETER " FPS: 144");
//   ImGui::Text(ICON_FK_TIMER " Latency: 8ms");
//
// Settings:
//   ImGui::Text(ICON_FK_COG " Configuration");
//   ImGui::Text(ICON_FK_SLIDERS " Advanced Options");
//
// Input remapping:
//   ImGui::Text(ICON_FK_GAMEPAD " Controller Settings");
//   ImGui::Text(ICON_FK_KEYBOARD " Key Bindings");
//
// Toggle states:
//   ImGui::Text(enabled ? ICON_FK_TOGGLE_ON : ICON_FK_TOGGLE_OFF);
//   ImGui::Text(visible ? ICON_FK_EYE : ICON_FK_EYE_SLASH);
//
// Status indicators:
//   ImGui::TextColored(green, ICON_FK_SUCCESS " Connected");
//   ImGui::TextColored(red, ICON_FK_FAIL " Failed");
//   ImGui::Text(ICON_FK_LOADING " Loading...");
//
// Ko-fi support button:
//   if (ImGui::Button(ICON_FK_COFFEE " Buy me a coffee on Ko-fi")) {
//       ShellExecuteA(NULL, "open", "https://ko-fi.com/yourpage", NULL, NULL, SW_SHOWNORMAL);
//   }
//   ImGui::Text(ICON_FK_HEART " Support this project");

