#pragma once

#include <windows.h>
#include <string>

namespace display_commander::logger {

// Log levels
enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

// Thread-safe logger class using SRWLockExclusive
class DisplayCommanderLogger {
public:
    static DisplayCommanderLogger& GetInstance();

    // Initialize logger with log file path
    void Initialize(const std::string& log_path);

    // Log a message with specified level
    void Log(LogLevel level, const std::string& message);

    // Convenience methods
    void LogDebug(const std::string& message);
    void LogInfo(const std::string& message);
    void LogWarning(const std::string& message);
    void LogError(const std::string& message);

    // Shutdown logger
    void Shutdown();

private:
    DisplayCommanderLogger() = default;
    ~DisplayCommanderLogger() = default;
    DisplayCommanderLogger(const DisplayCommanderLogger&) = delete;
    DisplayCommanderLogger& operator=(const DisplayCommanderLogger&) = delete;

    void WriteToFile(const std::string& formatted_message);
    std::string FormatMessage(LogLevel level, const std::string& message);
    std::string GetLogLevelString(LogLevel level);

    std::string log_path_;
    bool initialized_ = false;
    SRWLOCK lock_ = SRWLOCK_INIT;
};

// Global convenience functions
void Initialize(const std::string& log_path);
void LogDebug(const char* msg, ...);
void LogInfo(const char* msg, ...);
void LogWarning(const char* msg, ...);
void LogError(const char* msg, ...);
void Shutdown();

} // namespace display_commander::logger
