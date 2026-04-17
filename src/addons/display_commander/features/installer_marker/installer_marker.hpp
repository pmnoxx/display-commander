// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#pragma once

// Feature behavior is specified in src/addons/display_commander/docs/specs/installer_marker.md

#include <filesystem>
#include <optional>
#include <string>

namespace display_commander::features::installer_marker {

// `dc_config_global_flag_present` must be true only when an empty `.DC_CONFIG_GLOBAL` exists next to the add-on DLL
// or under the Display Commander app data root (same rule as global config in `ChooseAndSetDcConfigPath`). If false,
// returns nullopt (marker is not consulted unless global mode is active).
std::optional<std::wstring> TryReadInstallerMarkerConfigDirectory(bool dc_config_global_flag_present,
                                                                  const std::filesystem::path& marker_path);

// Creates/refreshes the marker only when `dc_config_global_flag_present` is true; otherwise does nothing.
void WriteInstallerMarkerJson(bool dc_config_global_flag_present, const std::filesystem::path& game_root,
                              const std::wstring& config_dir_w, const std::string& game_display_name,
                              bool config_path_from_marker);

}  // namespace display_commander::features::installer_marker
