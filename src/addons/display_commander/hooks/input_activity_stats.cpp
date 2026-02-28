#include "input_activity_stats.hpp"
#include "windows_hooks/windows_message_hooks.hpp"
#include "../utils/timing.hpp"
#include <algorithm>

namespace display_commanderhooks {

InputActivityStats& InputActivityStats::GetInstance() {
    static InputActivityStats instance;
    return instance;
}

void InputActivityStats::MarkActive(InputApiId api_id) {
    const int i = static_cast<int>(api_id);
    if (i >= 0 && i < kNumApis) {
        last_call_time_ns_[i].store(utils::get_now_ns(), std::memory_order_relaxed);
    }
}

void InputActivityStats::MarkActiveByHookIndex(int hook_index) {
    // Map central hook index to InputApiId. Hooks that are not input-API are no-op.
    InputApiId api_id = InputApiId::Count;  // invalid
    switch (static_cast<HookIndex>(hook_index)) {
        case HOOK_XInputGetState:
        case HOOK_XInputGetStateEx:
        case HOOK_XInputSetState:
        case HOOK_XInputGetCapabilities:
            api_id = InputApiId::XInput;
            break;
        case HOOK_DInput8CreateDevice:
            api_id = InputApiId::DirectInput8;
            break;
        case HOOK_DInputCreateDevice:
            api_id = InputApiId::DirectInput;
            break;
        case HOOK_GetRawInputData:
        case HOOK_GetRawInputBuffer:
            api_id = InputApiId::RawInput;
            break;
        case HOOK_HID_CreateFileA:
        case HOOK_HID_CreateFileW:
        case HOOK_HID_ReadFile:
        case HOOK_HID_ReadFileEx:
        case HOOK_HID_ReadFileScatter:
        case HOOK_HIDD_GetInputReport:
        case HOOK_HIDD_GetAttributes:
        case HOOK_HIDD_GetPreparsedData:
        case HOOK_HIDD_FreePreparsedData:
        case HOOK_HIDD_GetCaps:
            api_id = InputApiId::HID;
            break;
        case HOOK_joyGetPos:
        case HOOK_joyGetPosEx:
            api_id = InputApiId::WinMmJoystick;
            break;
        default:
            break;
    }
    if (api_id != InputApiId::Count) {
        MarkActive(api_id);
    }
}

std::vector<std::string> InputActivityStats::GetActiveApiNames(uint64_t now_ns, uint64_t window_ns) const {
    std::vector<std::string> out;
    for (int i = 0; i < kNumApis; ++i) {
        const auto id = static_cast<InputApiId>(i);
        if (IsActiveWithin(id, now_ns, window_ns)) {
            out.push_back(GetApiDisplayName(id));
        }
    }
    return out;
}

bool InputActivityStats::IsActiveWithin(InputApiId api_id, uint64_t now_ns, uint64_t window_ns) const {
    const int i = static_cast<int>(api_id);
    if (i < 0 || i >= kNumApis) {
        return false;
    }
    const uint64_t last = last_call_time_ns_[i].load(std::memory_order_relaxed);
    return last > 0 && (now_ns - last) < window_ns;
}

void InputActivityStats::Reset() {
    for (int i = 0; i < kNumApis; ++i) {
        last_call_time_ns_[i].store(0, std::memory_order_relaxed);
    }
}

const char* InputActivityStats::GetApiDisplayName(InputApiId api_id) {
    switch (api_id) {
        case InputApiId::XInput:                return "XInput";
        case InputApiId::DirectInput8:          return "DirectInput 8";
        case InputApiId::DirectInput:           return "DirectInput";
        case InputApiId::RawInput:              return "Raw Input";
        case InputApiId::HID:                   return "HID";
        case InputApiId::WindowsGamingInput:   return "Windows.Gaming.Input";
        case InputApiId::WinMmJoystick:         return "winmm joystick";
        case InputApiId::GameInput:             return "GameInput";
        default:                                return "Unknown";
    }
}

}  // namespace display_commanderhooks
