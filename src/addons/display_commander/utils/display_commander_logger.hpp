#pragma once

#include <windows.h>
#include <string>
#include <fstream>
#include <atomic>

namespace display_commander::logger {

// Log levels
enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

// Thread-safe logger class with buffered ostream
class DisplayCommanderLogger {
public:
    static DisplayCommanderLogger& GetInstance();

    // Initialize logger with log file path
    void Initialize(const std::string& log_path);

    // Log a message with specified level (thread-safe, writes to buffered stream)
    void Log(LogLevel level, const std::string& message);

    // Convenience methods
    void LogDebug(const std::string& message);
    void LogInfo(const std::string& message);
    void LogWarning(const std::string& message);
    void LogError(const std::string& message);

    // Shutdown logger (flushes and closes file)
    void Shutdown();

    // Flush buffered logs to disk
    void FlushLogs();

private:
    DisplayCommanderLogger();
    ~DisplayCommanderLogger();

    DisplayCommanderLogger(const DisplayCommanderLogger&) = delete;
    DisplayCommanderLogger& operator=(const DisplayCommanderLogger&) = delete;

    // Internal methods
    bool OpenLogFile();
    void CloseLogFile();
    void WriteToFile(const std::string& formatted_message);
    std::string FormatMessage(LogLevel level, const std::string& message);
    std::string GetLogLevelString(LogLevel level);
    bool ShouldRotateLog();
    void RotateLog();

    std::string log_path_;
    std::ofstream log_file_;

    // Thread synchronization
    std::atomic<bool> initialized_ = false;
    SRWLOCK write_lock_ = SRWLOCK_INIT;  // Protects file writes
};

// Global convenience functions
void Initialize(const std::string& log_path);
void LogDebug(const char* msg, ...);
void LogInfo(const char* msg, ...);
void LogWarning(const char* msg, ...);
void LogError(const char* msg, ...);
void Shutdown();
void FlushLogs();

} // namespace display_commander::logger
