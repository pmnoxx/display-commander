#include "display_commander_logger.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdarg>
#include <filesystem>

namespace display_commander::logger {

DisplayCommanderLogger& DisplayCommanderLogger::GetInstance() {
    static DisplayCommanderLogger instance;
    return instance;
}

void DisplayCommanderLogger::Initialize(const std::string& log_path) {
    AcquireSRWLockExclusive(&lock_);

    if (initialized_) {
        ReleaseSRWLockExclusive(&lock_);
        return;
    }

    log_path_ = log_path;

    // Create directory if it doesn't exist
    std::filesystem::path log_dir = std::filesystem::path(log_path_).parent_path();
    if (!log_dir.empty() && !std::filesystem::exists(log_dir)) {
        std::filesystem::create_directories(log_dir);
    }

    // Write initial log entry
    WriteToFile(FormatMessage(LogLevel::Info, "DisplayCommander Logger initialized"));

    initialized_ = true;
    ReleaseSRWLockExclusive(&lock_);
}

void DisplayCommanderLogger::Log(LogLevel level, const std::string& message) {
    if (!initialized_) {
        return;
    }

    AcquireSRWLockExclusive(&lock_);
    std::string formatted_message = FormatMessage(level, message);
    WriteToFile(formatted_message);
    ReleaseSRWLockExclusive(&lock_);
}

void DisplayCommanderLogger::LogDebug(const std::string& message) {
    Log(LogLevel::Debug, message);
}

void DisplayCommanderLogger::LogInfo(const std::string& message) {
    Log(LogLevel::Info, message);
}

void DisplayCommanderLogger::LogWarning(const std::string& message) {
    Log(LogLevel::Warning, message);
}

void DisplayCommanderLogger::LogError(const std::string& message) {
    Log(LogLevel::Error, message);
}

void DisplayCommanderLogger::Shutdown() {
    AcquireSRWLockExclusive(&lock_);

    if (initialized_) {
        WriteToFile(FormatMessage(LogLevel::Info, "DisplayCommander Logger shutting down"));
        initialized_ = false;
    }

    ReleaseSRWLockExclusive(&lock_);
}

void DisplayCommanderLogger::WriteToFile(const std::string& formatted_message) {
    try {
        std::ofstream log_file(log_path_, std::ios::app);
        if (log_file.is_open()) {
            log_file << formatted_message;
            log_file.flush(); // Force flush to ensure data is written
            log_file.close();
        }
    } catch (...) {
        OutputDebugStringA(
            "DisplayCommander: Error writing to log file"
        );
        // Ignore any errors during logging to prevent crashes
    }
}

std::string DisplayCommanderLogger::FormatMessage(LogLevel level, const std::string& message) {
    // Get current time
    SYSTEMTIME time;
    GetLocalTime(&time);

    // Format timestamp
    std::ostringstream timestamp;
    timestamp << std::setfill('0')
              << std::setw(2) << time.wHour << ":"
              << std::setw(2) << time.wMinute << ":"
              << std::setw(2) << time.wSecond << ":"
              << std::setw(3) << time.wMilliseconds;

    // Format the complete log line
    std::ostringstream log_line;
    log_line << timestamp.str()
             << " [" << std::setw(5) << GetCurrentThreadId() << "] | "
             << std::setw(7) << GetLogLevelString(level) << " | "
             << message << "\r\n";

    return log_line.str();
}

std::string DisplayCommanderLogger::GetLogLevelString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO";
        case LogLevel::Warning: return "WARNING";
        case LogLevel::Error:   return "ERROR";
        default:                return "UNKNOWN";
    }
}

// Global convenience functions
void Initialize(const std::string& log_path) {
    DisplayCommanderLogger::GetInstance().Initialize(log_path);
}

void LogDebug(const char* msg, ...) {
    va_list args;
    va_start(args, msg);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), msg, args);
    va_end(args);
    DisplayCommanderLogger::GetInstance().LogDebug(buffer);
}

void LogInfo(const char* msg, ...) {
    va_list args;
    va_start(args, msg);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), msg, args);
    va_end(args);
    DisplayCommanderLogger::GetInstance().LogInfo(buffer);
}

void LogWarning(const char* msg, ...) {
    va_list args;
    va_start(args, msg);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), msg, args);
    va_end(args);
    DisplayCommanderLogger::GetInstance().LogWarning(buffer);
}

void LogError(const char* msg, ...) {
    va_list args;
    va_start(args, msg);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), msg, args);
    va_end(args);
    DisplayCommanderLogger::GetInstance().LogError(buffer);
}

void Shutdown() {
    DisplayCommanderLogger::GetInstance().Shutdown();
}

} // namespace display_commander::logger
