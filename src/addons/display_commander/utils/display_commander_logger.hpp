#pragma once

#include <windows.h>
#include <atomic>
#include <deque>
#include <fstream>
#include <string>
#include <thread>

namespace display_commander::logger {

// Log levels
enum class LogLevel { Debug, Info, Warning, Error };

// Thread-safe logger: callers push to a queue; a dedicated writer thread does all file I/O (non-blocking for callers).
class DisplayCommanderLogger {
   public:
    static DisplayCommanderLogger& GetInstance();

    // Initialize logger with log file path (opens file and starts writer thread)
    void Initialize(const std::string& log_path);

    // Log a message with specified level (thread-safe, enqueues; does not block on file I/O)
    void Log(LogLevel level, const std::string& message);

    // Convenience methods
    void LogDebug(const std::string& message);
    void LogInfo(const std::string& message);
    void LogWarning(const std::string& message);
    void LogError(const std::string& message);

    // Shutdown logger (enqueues shutdown message, drains queue, closes file)
    void Shutdown();

    // Request flush: enqueues a flush sentinel so writer flushes without blocking the caller
    void FlushLogs();

    // Diagnostic: returns true if queue_lock_ is currently held (for stuck-detection reporting)
    bool IsWriteLockHeld();

   private:
    DisplayCommanderLogger();
    ~DisplayCommanderLogger();

    DisplayCommanderLogger(const DisplayCommanderLogger&) = delete;
    DisplayCommanderLogger& operator=(const DisplayCommanderLogger&) = delete;

    // Writer thread entry: drains queue, writes to file, flushes on sentinel, closes file on shutdown
    void WriterLoop();

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

    // Queue and writer thread (async I/O)
    std::deque<std::string> queue_;
    SRWLOCK queue_lock_ = SRWLOCK_INIT;
    CONDITION_VARIABLE queue_cv_ = CONDITION_VARIABLE_INIT;
    std::atomic<bool> shutdown_writer_{false};
    std::thread writer_thread_;

    std::atomic<bool> initialized_ = false;
};

// Global convenience functions
void Initialize(const std::string& log_path);
void LogDebug(const char* msg, ...);
void LogInfo(const char* msg, ...);
void LogWarning(const char* msg, ...);
void LogError(const char* msg, ...);
void Shutdown();
void FlushLogs();

// Diagnostic: returns true if logger write lock is currently held
bool IsWriteLockHeld();

}  // namespace display_commander::logger
