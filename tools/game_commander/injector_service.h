#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <atomic>
#include <thread>
#include <memory>
#include <windows.h>
#include <TlHelp32.h>

struct Game;

class InjectorService {
public:
    InjectorService();
    ~InjectorService();

    // Service control
    bool start();
    void stop();
    bool isRunning() const { return running_.load(); }

    // Configuration
    void setTargetGames(const std::vector<Game>& games);
    void setReShadeDllPaths(const std::string& path_32bit, const std::string& path_64bit);
    void setDisplayCommanderPaths(const std::string& path_32bit, const std::string& path_64bit);
    void setVerboseLogging(bool enabled);

    // Status
    size_t getInjectedProcessCount() const;
    std::vector<std::string> getInjectedProcesses() const;

private:
    struct TargetProcess {
        std::string exe_name;
        std::string display_name;
        std::string executable_path;
        std::string working_directory;
        bool enabled;
        bool use_local_injection;
        int proxy_dll_type; // ProxyDllType as int
        std::unordered_set<DWORD> injected_pids;
    };

    // Core functionality
    void monitoringLoop();
    bool injectIntoProcess(DWORD pid, const TargetProcess& target);
    bool isProcessRunning(DWORD pid);
    void cleanupInjectedPids();
    bool copyDisplayCommanderToGameFolder(DWORD pid);
    bool performLocalInjection(const Game& game);
    bool performLocalInjection(const TargetProcess& target);
    void logMessage(const std::string& message);
    void logError(const std::string& message, DWORD error_code = 0);

    // Member variables
    std::vector<TargetProcess> targets_;
    std::string reshade_dll_path_32bit_;
    std::string reshade_dll_path_64bit_;
    std::string display_commander_path_32bit_;
    std::string display_commander_path_64bit_;
    std::atomic<bool> running_;
    std::atomic<bool> verbose_logging_;
    std::unique_ptr<std::thread> monitoring_thread_;
    mutable SRWLOCK targets_srwlock_ = SRWLOCK_INIT;
};
