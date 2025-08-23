#pragma once

#include "../../renodx/settings.hpp"
#include <vector>

namespace renodx::ui::new_ui {

// Create a custom setting that wraps the new UI system
// This allows us to integrate with the existing settings system while using the new UI
void AddNewUISettings(std::vector<renodx::utils::settings2::Setting*>& settings);

// Check if the new UI should be used instead of the old one
bool ShouldUseNewUI();

} // namespace renodx::ui::new_ui
