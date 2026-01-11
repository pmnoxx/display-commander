#include "display_commander_logger.hpp"
#include "srwlock_wrapper.hpp"
#include <sstream>
#include <iomanip>
#include <cstdarg>
#include <filesystem>

namespace display_commander::logger {

DisplayCommanderLogger& DisplayCommanderLogger::GetInstance() {
    static DisplayCommanderLogger instance;
    return instance;
}

DisplayCommanderLogger::DisplayCommanderLogger() {
}

DisplayCommanderLogger::~DisplayCommanderLogger() {
    Shutdown();
}

void DisplayCommanderLogger::Initialize(const std::string& log_path) {
    bool expected = false;
    if (!initialized_.compare_exchange_strong(expected, true)) {
        return; // Already initialized
    }

    log_path_ = log_path;

    // Create directory if it doesn't exist
    std::filesystem::path log_dir = std::filesystem::path(log_path_).parent_path();
    if (!log_dir.empty() && !std::filesystem::exists(log_dir)) {
        std::filesystem::create_directories(log_dir);
    }

    // Open log file with buffered ostream
    if (!OpenLogFile()) {
        OutputDebugStringA("DisplayCommander: Failed to open log file\n");
        initialized_ = false;
        return;
    }

    // Write initial log entry
    Log(LogLevel::Info, "DisplayCommander Logger initialized");
}

void DisplayCommanderLogger::Log(LogLevel level, const std::string& message) {
    if (!initialized_.load()) {
        return;
    }

    std::string formatted_message = FormatMessage(level, message);

    // Write directly to buffered stream (thread-safe)
    {
        utils::SRWLockExclusive lock(write_lock_);
        WriteToFile(formatted_message);
    }
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

void DisplayCommanderLogger::FlushLogs() {
    if (!initialized_.load()) {
        return;
    }

    // Flush ostream buffer
    {
        utils::SRWLockExclusive lock(write_lock_);
        if (log_file_.is_open()) {
            log_file_.flush();
        }
    }
}

void DisplayCommanderLogger::Shutdown() {
    bool expected = true;
    if (!initialized_.compare_exchange_strong(expected, false)) {
        return; // Already shut down or never initialized
    }

    // Write final log entry, flush, and close file
    {
        utils::SRWLockExclusive lock(write_lock_);
        if (log_file_.is_open()) {
            std::string shutdown_msg = FormatMessage(LogLevel::Info, "DisplayCommander Logger shutting down");
            log_file_ << shutdown_msg;
            log_file_.flush();
            CloseLogFile();
        }
    }
}


bool DisplayCommanderLogger::OpenLogFile() {
    if (log_file_.is_open()) {
        return true; // Already open
    }

    // Open log file for writing (append mode, binary mode for CRLF handling)
    log_file_.open(log_path_, std::ios::app | std::ios::binary);

    if (!log_file_.is_open()) {
        return false;
    }

    return true;
}

void DisplayCommanderLogger::CloseLogFile() {
    if (log_file_.is_open()) {
        log_file_.flush();
        log_file_.close();
    }
}

void DisplayCommanderLogger::WriteToFile(const std::string& formatted_message) {
    if (!log_file_.is_open()) {
        return;
    }

    // Write to buffered ostream
    log_file_ << formatted_message;
    // Note: We don't flush here for performance - FlushLogs() can be called explicitly
}

std::string DisplayCommanderLogger::FormatMessage(LogLevel level, const std::string& message) {
    // Get current time
    SYSTEMTIME time;
    GetLocalTime(&time);

    // Format timestamp (matching ReShade format: HH:MM:SS:mmm)
    std::ostringstream timestamp;
    timestamp << std::setfill('0')
              << std::setw(2) << time.wHour << ":"
              << std::setw(2) << time.wMinute << ":"
              << std::setw(2) << time.wSecond << ":"
              << std::setw(3) << time.wMilliseconds;

    // Format the complete log line (matching ReShade format)
    std::ostringstream log_line;
    log_line << timestamp.str()
             << " [" << std::setw(5) << GetCurrentThreadId() << "] | "
             << std::setw(5) << GetLogLevelString(level) << " | "
             << message;

    std::string line_string = log_line.str();

    // Replace all standalone LF with CRLF (like ReShade does)
    // Skip if already CRLF (check for \r before \n)
    for (size_t offset = 0; (offset = line_string.find('\n', offset)) != std::string::npos; ) {
        if (offset == 0 || line_string[offset - 1] != '\r') {
            // Standalone LF, replace with CRLF
            line_string.replace(offset, 1, "\r\n", 2);
            offset += 2;
        } else {
            // Already CRLF, skip
            offset += 1;
        }
    }

    // Ensure line ends with CRLF
    if (line_string.empty() || (line_string.size() < 2 || line_string.substr(line_string.size() - 2) != "\r\n")) {
        // Remove any trailing single newline characters
        while (!line_string.empty() && (line_string.back() == '\n' || line_string.back() == '\r')) {
            line_string.pop_back();
        }
        line_string += "\r\n";
    }

    return line_string;
}

std::string DisplayCommanderLogger::GetLogLevelString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO ";
        case LogLevel::Warning: return "WARN ";
        case LogLevel::Error:   return "ERROR";
        default:                return "UNKNO";
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

void FlushLogs() {
    DisplayCommanderLogger::GetInstance().FlushLogs();
}

} // namespace display_commander::logger
