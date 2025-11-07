#pragma once

#include <string>
#include <vector>

namespace autoclick {

// Represents a single auto-click action
struct AutoClickAction {
    bool enabled = false;    // Whether this action is enabled
    int x = 0;                // X coordinate (client coordinates)
    int y = 0;                // Y coordinate (client coordinates)
    int interval_ms = 3000;    // Interval in milliseconds before next action

    // Default constructor
    AutoClickAction() = default;

    // Constructor with parameters
    AutoClickAction(bool enabled, int x, int y, int interval_ms)
        : enabled(enabled), x(x), y(y), interval_ms(interval_ms) {}

    // Equality operator for comparison
    bool operator==(const AutoClickAction& other) const {
        return enabled == other.enabled && x == other.x && y == other.y && interval_ms == other.interval_ms;
    }

    // Serialize to string format: "enabled:x:y:interval"
    std::string Serialize() const;

    // Deserialize from string format
    static AutoClickAction Deserialize(const std::string& str);
};

// Helper functions for serializing/deserializing action lists
std::string SerializeActions(const std::vector<AutoClickAction>& actions);
std::vector<AutoClickAction> DeserializeActions(const std::string& str);

} // namespace autoclick

