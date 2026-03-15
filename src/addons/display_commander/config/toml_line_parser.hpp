#pragma once

#include <string>

namespace display_commander::config {

// Parse a single TOML line: key = "value" or key = value (unquoted). Trims key and value; strips surrounding quotes from value. Returns true if parsed.
inline bool ParseTomlLine(const std::string& line, std::string& out_key, std::string& out_value) {
    size_t eq = line.find('=');
    if (eq == std::string::npos) return false;
    out_key = line.substr(0, eq);
    out_value = line.substr(eq + 1);
    out_key.erase(0, out_key.find_first_not_of(" \t"));
    out_key.erase(out_key.find_last_not_of(" \t") + 1);
    out_value.erase(0, out_value.find_first_not_of(" \t"));
    out_value.erase(out_value.find_last_not_of(" \t") + 1);
    if (out_value.size() >= 2
        && ((out_value.front() == '"' && out_value.back() == '"')
            || (out_value.front() == '\'' && out_value.back() == '\''))) {
        out_value = out_value.substr(1, out_value.size() - 2);
    }
    return !out_key.empty();
}

}  // namespace display_commander::config
