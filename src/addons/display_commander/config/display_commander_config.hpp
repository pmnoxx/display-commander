// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#pragma once

// Source Code <Display Commander>
#include "../utils/srwlock_wrapper.hpp"

// Libraries <standard C++>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// Libraries <Windows.h>
#include <Windows.h>

namespace display_commander::config {

// Forward declaration
class IniFile;

// Configuration manager for DisplayCommander settings
class DisplayCommanderConfigManager {
   public:
    static DisplayCommanderConfigManager& GetInstance();

    // Initialize the config system. If config_directory is set, use it as the config dir (DisplayCommander.ini location); otherwise use exe directory.
    void Initialize(std::optional<std::wstring_view> config_directory = std::nullopt);

    // Get configuration value (compatible with reshade::get_config_value API)
    bool GetConfigValue(const char* section, const char* key, std::string& value);
    bool GetConfigValue(const char* section, const char* key, int& value);
    bool GetConfigValue(const char* section, const char* key, uint32_t& value);
    bool GetConfigValue(const char* section, const char* key, float& value);
    bool GetConfigValue(const char* section, const char* key, double& value);
    bool GetConfigValue(const char* section, const char* key, bool& value);
    bool GetConfigValue(const char* section, const char* key, std::vector<std::string>& values);

    // Get configuration value, ensuring it exists (writes default if missing)
    void GetConfigValueEnsureExists(const char* section, const char* key, std::string& value,
                                    const std::string& default_value);
    void GetConfigValueEnsureExists(const char* section, const char* key, int& value, int default_value);
    void GetConfigValueEnsureExists(const char* section, const char* key, uint32_t& value, uint32_t default_value);
    void GetConfigValueEnsureExists(const char* section, const char* key, float& value, float default_value);
    void GetConfigValueEnsureExists(const char* section, const char* key, double& value, double default_value);
    void GetConfigValueEnsureExists(const char* section, const char* key, bool& value, bool default_value);

    // Set configuration value (compatible with reshade::set_config_value API)
    void SetConfigValue(const char* section, const char* key, const std::string& value);
    void SetConfigValue(const char* section, const char* key, const char* value);
    void SetConfigValue(const char* section, const char* key, int value);
    void SetConfigValue(const char* section, const char* key, uint32_t value);
    void SetConfigValue(const char* section, const char* key, float value);
    void SetConfigValue(const char* section, const char* key, double value);
    void SetConfigValue(const char* section, const char* key, bool value);
    void SetConfigValue(const char* section, const char* key, const std::vector<std::string>& values);

    // Schedule a DisplayCommander.ini snapshot write (no I/O on the caller).
    void SaveConfig(const char* reason = nullptr);
    // Write live ini now on the config I/O thread and wait; clears any pending snapshot.
    void FlushConfig(const char* reason = nullptr);
    // Overlay closed: write pending then live catch-up; wait for completion.
    void FlushConfigOnOverlayClose();
    // Clean exit: same catch-up as overlay close, then stop and join the I/O thread.
    void FlushConfigOnExit();

    // Get config file path
    std::string GetConfigPath() const;

    // Set auto-flush logs mode (enables immediate flushing during startup)
    void SetAutoFlushLogs(bool enabled);

    // Get auto-flush logs mode
    bool GetAutoFlushLogs() const;

   private:
    enum class FlushKind : uint8_t {
        None = 0,
        Immediate,
        OverlayClose,
        Exit,
    };

    DisplayCommanderConfigManager() = default;
    ~DisplayCommanderConfigManager() = default;
    DisplayCommanderConfigManager(const DisplayCommanderConfigManager&) = delete;
    DisplayCommanderConfigManager& operator=(const DisplayCommanderConfigManager&) = delete;

    void EnsureConfigFileExists();
    std::string GetConfigFilePath();
    // Caller holds config_mutex_. Only the config I/O worker (config_worker.cpp) calls this.
    bool SerializeLiveIniLocked(std::string& out);

    void EnsureWriterStarted();
    void WriterLoop();
    void RequestFlushAndWait(FlushKind kind, const char* reason);
    void ClearQueueLocked();
    void ArmDeadlineLocked(LONGLONG now_ns);
    bool WriteTextAndReport(const std::string& text, const char* reason);
    void CompleteFlushLocked(uint64_t generation);

    std::unique_ptr<IniFile> config_file_;
    std::string config_path_;
    mutable SRWLOCK config_mutex_ = SRWLOCK_INIT;
    bool initialized_ = false;
    std::atomic<bool> auto_flush_logs_ = false;

    // Snapshot queue (under config_mutex_). config_worker.cpp owns the thread, freeze, and disk write.
    bool freeze_requested_ = false;
    bool has_pending_ = false;
    std::string pending_text_;
    bool dirty_after_ = false;
    bool deadline_armed_ = false;
    LONGLONG deadline_ns_ = 0;
    std::string latest_reason_;

    FlushKind flush_request_ = FlushKind::None;
    uint64_t flush_generation_ = 0;
    uint64_t completed_generation_ = 0;
    std::string flush_reason_;

    CONDITION_VARIABLE writer_cv_ = CONDITION_VARIABLE_INIT;
    std::thread writer_thread_;
    bool writer_started_ = false;
    bool writer_stop_ = false;
};

// Global functions that replace reshade::get_config_value and reshade::set_config_value
bool get_config_value(const char* section, const char* key, std::string& value);
bool get_config_value(const char* section, const char* key, int& value);
bool get_config_value(const char* section, const char* key, uint32_t& value);
bool get_config_value(const char* section, const char* key, float& value);
bool get_config_value(const char* section, const char* key, double& value);
bool get_config_value(const char* section, const char* key, bool& value);
bool get_config_value(const char* section, const char* key, std::vector<std::string>& values);
// Special overload for char buffer with size (compatible with ReShade API)
bool get_config_value(const char* section, const char* key, char* buffer, size_t* buffer_size);

void set_config_value(const char* section, const char* key, const std::string& value);
void set_config_value(const char* section, const char* key, const char* value);
void set_config_value(const char* section, const char* key, int value);
void set_config_value(const char* section, const char* key, uint32_t value);
void set_config_value(const char* section, const char* key, float value);
void set_config_value(const char* section, const char* key, double value);
void set_config_value(const char* section, const char* key, bool value);
void set_config_value(const char* section, const char* key, const std::vector<std::string>& values);

// Schedule a DisplayCommander.ini snapshot (batched).
void save_config(const char* reason = nullptr);
// Write live DisplayCommander.ini now and wait.
void save_config_immediate(const char* reason = nullptr);
void flush_config_on_overlay_close();
void flush_config_on_exit();

// Get configuration value, ensuring it exists (writes default if missing)
void get_config_value_ensure_exists(const char* section, const char* key, std::string& value,
                                    const std::string& default_value);
void get_config_value_ensure_exists(const char* section, const char* key, int& value, int default_value);
void get_config_value_ensure_exists(const char* section, const char* key, uint32_t& value, uint32_t default_value);
void get_config_value_ensure_exists(const char* section, const char* key, float& value, float default_value);
void get_config_value_ensure_exists(const char* section, const char* key, double& value, double default_value);
void get_config_value_ensure_exists(const char* section, const char* key, bool& value, bool default_value);

// Get config value; if missing, use per-game default override (if any) else use passed default. Does not write to config.
void get_config_value_or_default(const char* section, const char* key, bool default_value, bool* out_value);
void get_config_value_or_default(const char* section, const char* key, int default_value, int* out_value);
void get_config_value_or_default(const char* section, const char* key, float default_value, float* out_value);

}  // namespace display_commander::config
