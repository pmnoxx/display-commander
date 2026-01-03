#pragma once

namespace ui::new_ui {

// Draw features enabled by default section
void DrawFeaturesEnabledByDefault();

// Draw the developer new tab content
void DrawDeveloperNewTab();

// Draw developer settings section
void DrawDeveloperSettings();

// Draw HDR and display settings section
void DrawHdrDisplaySettings();

// Draw HDR and colorspace settings section
void DrawHdrColorspaceSettings();

// Draw NVAPI settings section
void DrawNvapiSettings();

// Draw resolution override settings section (Experimental)
void DrawResolutionOverrideSettings();

// Draw keyboard shortcuts settings section
void DrawKeyboardShortcutsSettings();

// Draw ReShade global config settings section
void DrawReShadeGlobalConfigSettings();

// Draw new experimental features section
void DrawNewExperimentalFeatures();

// Initialize developer tab
void InitDeveloperNewTab();

} // namespace ui::new_ui
