#pragma once

#include <windows.h>
#include <string>
#include <queue>
#include <atomic>

namespace display_commander::logger {

// Log levels
enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

// Thread-safe logger class with background thread and persistent file handle
class DisplayCommanderLogger {
public:
    static DisplayCommanderLogger& GetInstance();

    // Initialize logger with log file path
    void Initialize(const std::string& log_path);

    // Log a message with specified level (thread-safe, queues message)
    void Log(LogLevel level, const std::string& message);

    // Convenience methods
    void LogDebug(const std::string& message);
    void LogInfo(const std::string& message);
    void LogWarning(const std::string& message);
    void LogError(const std::string& message);

    // Shutdown logger (waits for background thread to finish)
    void Shutdown();

    // Flush all queued logs to disk (waits for queue to be empty and flushes file buffers)
    void FlushLogs();

private:
    DisplayCommanderLogger();
    ~DisplayCommanderLogger();

    DisplayCommanderLogger(const DisplayCommanderLogger&) = delete;
    DisplayCommanderLogger& operator=(const DisplayCommanderLogger&) = delete;

    // Background thread function
    static DWORD WINAPI LoggerThreadProc(LPVOID lpParam);
    void LoggerThreadMain();

    // Internal methods
    bool OpenLogFile();
    void CloseLogFile();
    void WriteToFile(const std::string& formatted_message);
    std::string FormatMessage(LogLevel level, const std::string& message);
    std::string GetLogLevelString(LogLevel level);

    std::string log_path_;
    HANDLE file_handle_ = INVALID_HANDLE_VALUE;

    // Thread synchronization
    std::atomic<bool> initialized_ = false;
    std::atomic<bool> shutdown_requested_ = false;
    HANDLE logger_thread_ = nullptr;
    HANDLE queue_event_ = nullptr;  // Signaled when new messages arrive

    // Message queue (protected by queue_lock_)
    SRWLOCK queue_lock_ = SRWLOCK_INIT;
    std::queue<std::string> message_queue_;
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
