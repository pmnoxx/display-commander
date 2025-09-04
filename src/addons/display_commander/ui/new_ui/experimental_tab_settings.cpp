#include "experimental_tab_settings.hpp"

namespace ui::new_ui {

// Global instance
ExperimentalTabSettings g_experimentalTabSettings;

ExperimentalTabSettings::ExperimentalTabSettings()
    : auto_click_enabled("AutoClickEnabled", false, "Experimental")
    , sequence_1_enabled("Sequence1Enabled", false, "Experimental")
    , sequence_1_x("Sequence1X", 0, -10000, 10000, "Experimental")
    , sequence_1_y("Sequence1Y", 0, -10000, 10000, "Experimental")
    , sequence_1_interval("Sequence1Interval", 3000, 100, 60000, "Experimental")
    , sequence_2_enabled("Sequence2Enabled", false, "Experimental")
    , sequence_2_x("Sequence2X", 0, -10000, 10000, "Experimental")
    , sequence_2_y("Sequence2Y", 0, -10000, 10000, "Experimental")
    , sequence_2_interval("Sequence2Interval", 3000, 100, 60000, "Experimental")
    , sequence_3_enabled("Sequence3Enabled", false, "Experimental")
    , sequence_3_x("Sequence3X", 0, -10000, 10000, "Experimental")
    , sequence_3_y("Sequence3Y", 0, -10000, 10000, "Experimental")
    , sequence_3_interval("Sequence3Interval", 3000, 100, 60000, "Experimental")
    , sequence_4_enabled("Sequence4Enabled", false, "Experimental")
    , sequence_4_x("Sequence4X", 0, -10000, 10000, "Experimental")
    , sequence_4_y("Sequence4Y", 0, -10000, 10000, "Experimental")
    , sequence_4_interval("Sequence4Interval", 3000, 100, 60000, "Experimental")
    , sequence_5_enabled("Sequence5Enabled", false, "Experimental")
    , sequence_5_x("Sequence5X", 0, -10000, 10000, "Experimental")
    , sequence_5_y("Sequence5Y", 0, -10000, 10000, "Experimental")
    , sequence_5_interval("Sequence5Interval", 3000, 100, 60000, "Experimental")
{
    // Initialize the all_settings_ vector
    all_settings_ = {
        &auto_click_enabled,
        &sequence_1_enabled, &sequence_1_x, &sequence_1_y, &sequence_1_interval,
        &sequence_2_enabled, &sequence_2_x, &sequence_2_y, &sequence_2_interval,
        &sequence_3_enabled, &sequence_3_x, &sequence_3_y, &sequence_3_interval,
        &sequence_4_enabled, &sequence_4_x, &sequence_4_y, &sequence_4_interval,
        &sequence_5_enabled, &sequence_5_x, &sequence_5_y, &sequence_5_interval
    };
}

void ExperimentalTabSettings::LoadAll() {
    LoadTabSettings(all_settings_);
}

std::vector<SettingBase*> ExperimentalTabSettings::GetAllSettings() {
    return all_settings_;
}

} // namespace ui::new_ui
