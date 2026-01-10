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
    queue_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

DisplayCommanderLogger::~DisplayCommanderLogger() {
    Shutdown();
    if (queue_event_ != nullptr) {
        CloseHandle(queue_event_);
        queue_event_ = nullptr;
    }
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

    // Open log file and keep handle open
    if (!OpenLogFile()) {
        OutputDebugStringA("DisplayCommander: Failed to open log file\n");
        initialized_ = false;
        return;
    }

    // Start background logger thread
    shutdown_requested_ = false;
    logger_thread_ = CreateThread(nullptr, 0, LoggerThreadProc, this, 0, nullptr);
    if (logger_thread_ == nullptr) {
        OutputDebugStringA("DisplayCommander: Failed to create logger thread\n");
        CloseLogFile();
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

    // Add to queue (thread-safe)
    AcquireSRWLockExclusive(&queue_lock_);
    message_queue_.push(std::move(formatted_message));
    ReleaseSRWLockExclusive(&queue_lock_);

    // Signal the logger thread
    if (queue_event_ != nullptr) {
        SetEvent(queue_event_);
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

    // Wait for queue to be empty (with timeout)
    const int max_wait_iterations = 50; // 5 seconds total (50 * 100ms)
    for (int i = 0; i < max_wait_iterations; ++i) {
        bool queue_empty = false;
        {
            utils::SRWLockShared lock(queue_lock_);
            queue_empty = message_queue_.empty();
        }

        if (queue_empty) {
            break;
        }

        // Signal the logger thread to process messages
        if (queue_event_ != nullptr) {
            SetEvent(queue_event_);
        }

        Sleep(100); // Wait 100ms before checking again
    }

    // Flush file buffers
    if (file_handle_ != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(file_handle_);
    }
}

void DisplayCommanderLogger::Shutdown() {
    bool expected = true;
    if (!initialized_.compare_exchange_strong(expected, false)) {
        return; // Already shut down or never initialized
    }

    // Signal shutdown
    shutdown_requested_ = true;
    if (queue_event_ != nullptr) {
        SetEvent(queue_event_);
    }

    // Wait for logger thread to finish (with timeout)
    if (logger_thread_ != nullptr) {
        WaitForSingleObject(logger_thread_, 5000); // 5 second timeout
        CloseHandle(logger_thread_);
        logger_thread_ = nullptr;
    }

    // Write final log entry, flush, and close file
    if (file_handle_ != INVALID_HANDLE_VALUE) {
        std::string shutdown_msg = FormatMessage(LogLevel::Info, "DisplayCommander Logger shutting down");
        WriteToFile(shutdown_msg);

        // Explicitly flush all buffers before closing
        FlushFileBuffers(file_handle_);

        CloseLogFile();
    }
}

DWORD WINAPI DisplayCommanderLogger::LoggerThreadProc(LPVOID lpParam) {
    DisplayCommanderLogger* logger = static_cast<DisplayCommanderLogger*>(lpParam);
    logger->LoggerThreadMain();
    return 0;
}

void DisplayCommanderLogger::LoggerThreadMain() {
    while (!shutdown_requested_.load()) {
        // Wait for messages or timeout
        DWORD wait_result = WaitForSingleObject(queue_event_, 100); // 100ms timeout

        // Process all queued messages
        while (true) {
            std::string message;
            {
                AcquireSRWLockExclusive(&queue_lock_);
                if (message_queue_.empty()) {
                    ReleaseSRWLockExclusive(&queue_lock_);
                    break;
                }
                message = std::move(message_queue_.front());
                message_queue_.pop();
                ReleaseSRWLockExclusive(&queue_lock_);
            }

            // Write to file (file handle is kept open)
            WriteToFile(message);
        }

        // If shutdown was requested and queue is empty, exit
        if (shutdown_requested_.load()) {
            AcquireSRWLockExclusive(&queue_lock_);
            bool queue_empty = message_queue_.empty();
            ReleaseSRWLockExclusive(&queue_lock_);
            if (queue_empty) {
                break;
            }
        }
    }

    // Process any remaining messages before exit
    while (true) {
        std::string message;
        {
            AcquireSRWLockExclusive(&queue_lock_);
            if (message_queue_.empty()) {
                ReleaseSRWLockExclusive(&queue_lock_);
                break;
            }
            message = std::move(message_queue_.front());
            message_queue_.pop();
            ReleaseSRWLockExclusive(&queue_lock_);
        }
        WriteToFile(message);
    }

    // Flush all buffers before thread exits
    if (file_handle_ != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(file_handle_);
    }
}

bool DisplayCommanderLogger::OpenLogFile() {
    if (file_handle_ != INVALID_HANDLE_VALUE) {
        return true; // Already open
    }

    // Convert string path to wide string
    std::wstring wide_path(log_path_.begin(), log_path_.end());

    // Open log file for writing (append mode, share read access, flush on write)
    // Similar to ReShade's approach but with OPEN_ALWAYS instead of CREATE_ALWAYS
    file_handle_ = CreateFileW(
        wide_path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        OPEN_ALWAYS,  // Open existing or create new
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
        nullptr
    );

    if (file_handle_ == INVALID_HANDLE_VALUE) {
        return false;
    }

    // Move to end of file for append mode
    SetFilePointer(file_handle_, 0, nullptr, FILE_END);

    return true;
}

void DisplayCommanderLogger::CloseLogFile() {
    if (file_handle_ != INVALID_HANDLE_VALUE) {
        // Flush all buffers before closing
        FlushFileBuffers(file_handle_);
        CloseHandle(file_handle_);
        file_handle_ = INVALID_HANDLE_VALUE;
    }
}

void DisplayCommanderLogger::WriteToFile(const std::string& formatted_message) {
    if (file_handle_ == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD written = 0;
    WriteFile(
        file_handle_,
        formatted_message.data(),
        static_cast<DWORD>(formatted_message.size()),
        &written,
        nullptr
    );

    // Flush file buffers
    FlushFileBuffers(file_handle_);
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
