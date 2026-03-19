#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace display_commanderhooks {

/** Input API identifier for the "active input APIs" display (Controller tab). */
enum class InputApiId : int {
    XInput = 0,
    DirectInput8,
    DirectInput,
    RawInput,
    WindowsGamingInput,
    GameInput,
    Count
};

/**
 * Single class that keeps input activity stats for the "Active input APIs (last 10s)" display.
 * All input-related detours (XInput, DInput, Raw Input, WGI, GameInput) should
 * call MarkActive() or MarkActiveByHookIndex() so the Controller tab can show which APIs
 * the game has used recently.
 */
class InputActivityStats {
public:
    static InputActivityStats& GetInstance();

    /** Mark an input API as just used (updates last-call timestamp). */
    void MarkActive(InputApiId api_id);

    /**
     * Mark activity by central hook index. Maps hook_index (from windows_message_hooks)
     * to the corresponding InputApiId and updates that API's timestamp.
     * No-op if hook_index is not an input-related hook.
     */
    void MarkActiveByHookIndex(int hook_index);

    /** Returns display names of APIs that were active within the last window_ns. */
    std::vector<std::string> GetActiveApiNames(uint64_t now_ns, uint64_t window_ns) const;

    /** Returns true if the given API was called within the last window_ns. */
    bool IsActiveWithin(InputApiId api_id, uint64_t now_ns, uint64_t window_ns) const;

    /** Clear all last-call timestamps (e.g. when user resets hook stats). */
    void Reset();

    /** Display name for an API (for UI). */
    static const char* GetApiDisplayName(InputApiId api_id);

private:
    InputActivityStats() = default;
    ~InputActivityStats() = default;
    InputActivityStats(const InputActivityStats&) = delete;
    InputActivityStats& operator=(const InputActivityStats&) = delete;

    static constexpr int kNumApis = static_cast<int>(InputApiId::Count);
    std::atomic<uint64_t> last_call_time_ns_[kNumApis] = {};
};

}  // namespace display_commanderhooks
