#include "autoclick_action.hpp"
#include <sstream>
#include <algorithm>

namespace autoclick {

std::string AutoClickAction::Serialize() const {
    std::ostringstream oss;
    oss << (enabled ? 1 : 0) << ":" << x << ":" << y << ":" << interval_ms;
    return oss.str();
}

AutoClickAction AutoClickAction::Deserialize(const std::string& str) {
    AutoClickAction action;

    if (str.empty()) {
        return action;
    }

    std::istringstream iss(str);
    std::string token;
    int field = 0;

    while (std::getline(iss, token, ':')) {
        try {
            int value = std::stoi(token);
            switch (field) {
                case 0: action.enabled = (value != 0); break;
                case 1: action.x = value; break;
                case 2: action.y = value; break;
                case 3: action.interval_ms = value; break;
            }
            field++;
        } catch (const std::exception&) {
            // Invalid format, return default action
            return AutoClickAction();
        }
    }

    // Clamp interval to reasonable values
    action.interval_ms = std::max(100, std::min(60000, action.interval_ms));

    return action;
}

std::string SerializeActions(const std::vector<AutoClickAction>& actions) {
    std::ostringstream oss;
    for (size_t i = 0; i < actions.size(); ++i) {
        if (i > 0) {
            oss << ";";
        }
        oss << actions[i].Serialize();
    }
    return oss.str();
}

std::vector<AutoClickAction> DeserializeActions(const std::string& str) {
    std::vector<AutoClickAction> actions;

    if (str.empty()) {
        return actions;
    }

    std::istringstream iss(str);
    std::string action_str;

    while (std::getline(iss, action_str, ';')) {
        if (!action_str.empty()) {
            actions.push_back(AutoClickAction::Deserialize(action_str));
        }
    }

    return actions;
}

} // namespace autoclick

