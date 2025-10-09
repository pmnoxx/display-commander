#pragma once

namespace ui::new_ui {

void InitMainNewTab();

void InitDeveloperNewTab();

// Draw the main new tab content
void DrawMainNewTab();

// Draw display settings section
void DrawDisplaySettings();

// Draw audio settings section
void DrawAudioSettings();

// Draw window controls section
void DrawWindowControls();

// Draw ADHD Multi-Monitor Mode controls section
void DrawAdhdMultiMonitorControls(bool hasBlackCurtainSetting);

// Draw important information section (Flip State)
void DrawImportantInfo();

// Draw frame time graph section
void DrawFrameTimeGraph();

}  // namespace ui::new_ui
