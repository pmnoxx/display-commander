// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "config_worker.hpp"

// Source Code <Display Commander>
#include "display_commander_config.hpp"
#include "../globals.hpp"
#include "../hooks/windows_hooks/windows_message_hooks.hpp"
#include "../utils/logging.hpp"
#include "../utils/srwlock_wrapper.hpp"
#include "../utils/string_utils.hpp"
#include "../utils/symlink_safe_file.hpp"
#include "../utils/timing.hpp"

// Libraries <standard C++>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

// Libraries <Windows.h>
#include <Windows.h>

namespace display_commander::config {

namespace {

constexpr LONGLONG kConfigSnapshotDelayNs = 3 * ::utils::SEC_TO_NS;

}  // namespace

void DisplayCommanderConfigManager::ClearQueueLocked() {
    freeze_requested_ = false;
    has_pending_ = false;
    pending_text_.clear();
    dirty_after_ = false;
    deadline_armed_ = false;
    deadline_ns_ = 0;
}

void DisplayCommanderConfigManager::ArmDeadlineLocked(LONGLONG now_ns) {
    if (display_commanderhooks::IsReShadeOverlayOpen()) {
        deadline_armed_ = false;
        deadline_ns_ = 0;
        return;
    }
    deadline_ns_ = now_ns + kConfigSnapshotDelayNs;
    deadline_armed_ = true;
}

void DisplayCommanderConfigManager::CompleteFlushLocked(uint64_t generation) {
    if (generation > completed_generation_) {
        completed_generation_ = generation;
    }
    WakeConditionVariable(&writer_cv_);
}

bool DisplayCommanderConfigManager::WriteTextAndReport(const std::string& text, const char* reason) {
    EnsureConfigFileExists();
    const std::filesystem::path path = display_commander::utils::Utf8ToFilesystemPath(config_path_);
    const bool ok = display_commander::utils::WriteTextFileReplace(path, text, "Config");
    if (ok) {
        g_config_save_failure_path.store(nullptr);
        if (reason != nullptr && reason[0] != '\0') {
            LogInfoThrottled(10, "Saved config to %s (reason: %s)", config_path_.c_str(), reason);
        } else {
            LogInfoThrottled(10, "Saved config to %s", config_path_.c_str());
        }
    } else {
        g_config_save_failure_path.store(std::make_shared<const std::string>(config_path_));
        if (reason != nullptr && reason[0] != '\0') {
            LogError("DisplayCommanderConfigManager: Failed to save config to %s (reason: %s)", config_path_.c_str(),
                     reason);
        } else {
            LogError("DisplayCommanderConfigManager: Failed to save config to %s", config_path_.c_str());
        }
    }
    return ok;
}

void DisplayCommanderConfigManager::EnsureWriterStarted() {
    {
        ::utils::SRWLockExclusive lock(config_mutex_);
        if (writer_started_ || writer_stop_) {
            return;
        }
        writer_started_ = true;
        writer_stop_ = false;
    }
    writer_thread_ = std::thread([this] { WriterLoop(); });
}

void DisplayCommanderConfigManager::RequestFlushAndWait(FlushKind kind, const char* reason) {
    if (kind == FlushKind::None) {
        return;
    }

    {
        ::utils::SRWLockExclusive lock(config_mutex_);
        if (!initialized_ && kind != FlushKind::Exit) {
            return;
        }
        if (!initialized_ && !writer_started_) {
            return;
        }
    }

    EnsureWriterStarted();

    uint64_t generation = 0;
    {
        ::utils::SRWLockExclusive lock(config_mutex_);
        if (!initialized_ && kind != FlushKind::Exit) {
            return;
        }
        if (kind == FlushKind::Exit || flush_request_ != FlushKind::Exit) {
            flush_request_ = kind;
        }
        if (reason != nullptr && reason[0] != '\0') {
            flush_reason_ = reason;
        } else if (flush_reason_.empty()) {
            flush_reason_ = latest_reason_;
        }
        generation = ++flush_generation_;
        WakeConditionVariable(&writer_cv_);
    }

    ::utils::SRWLockExclusive lock(config_mutex_);
    while (completed_generation_ < generation) {
        if (!writer_started_ && flush_request_ == FlushKind::None) {
            completed_generation_ = generation;
            break;
        }
        SleepConditionVariableSRW(&writer_cv_, &config_mutex_, INFINITE, 0);
    }
}

void DisplayCommanderConfigManager::WriterLoop() {
    for (;;) {
        FlushKind flush_kind = FlushKind::None;
        uint64_t flush_gen = 0;
        std::string job_reason;
        std::string first_text;
        std::string second_text;
        bool have_first = false;
        bool have_second = false;
        bool scheduled_write = false;
        bool stop_after = false;

        {
            ::utils::SRWLockExclusive lock(config_mutex_);
            for (;;) {
                const bool overlay_open = display_commanderhooks::IsReShadeOverlayOpen();
                const LONGLONG now_ns = ::utils::get_now_ns();
                const bool deadline_due = has_pending_ && deadline_armed_ && !overlay_open && now_ns >= deadline_ns_;
                if (flush_request_ != FlushKind::None || freeze_requested_ || deadline_due || writer_stop_) {
                    break;
                }

                DWORD timeout_ms = INFINITE;
                if (has_pending_ && deadline_armed_ && !overlay_open && deadline_ns_ > now_ns) {
                    const LONGLONG remain_ns = deadline_ns_ - now_ns;
                    const LONGLONG remain_ms = (remain_ns + 999999LL) / 1000000LL;
                    if (remain_ms >= static_cast<LONGLONG>(INFINITE)) {
                        timeout_ms = INFINITE - 1;
                    } else if (remain_ms <= 0) {
                        timeout_ms = 1;
                    } else {
                        timeout_ms = static_cast<DWORD>(remain_ms);
                    }
                }

                SleepConditionVariableSRW(&writer_cv_, &config_mutex_, timeout_ms, 0);
            }

            if (flush_request_ != FlushKind::None) {
                flush_kind = flush_request_;
                flush_request_ = FlushKind::None;
                flush_gen = flush_generation_;
                job_reason = flush_reason_.empty() ? latest_reason_ : flush_reason_;
                flush_reason_.clear();
                stop_after = (flush_kind == FlushKind::Exit);

                if (flush_kind == FlushKind::Immediate) {
                    ClearQueueLocked();
                    have_first = SerializeLiveIniLocked(first_text);
                } else {
                    if (freeze_requested_ && !has_pending_ && SerializeLiveIniLocked(pending_text_)) {
                        has_pending_ = true;
                        freeze_requested_ = false;
                        // Pending is current live; absorb dirty-after into this single snapshot.
                        dirty_after_ = false;
                    }
                    const bool write_pending = has_pending_;
                    const bool write_live = dirty_after_;
                    if (write_pending) {
                        first_text = pending_text_;
                        have_first = true;
                    }
                    if (write_live) {
                        have_second = SerializeLiveIniLocked(second_text);
                    }
                    ClearQueueLocked();
                }
            } else if (writer_stop_) {
                writer_started_ = false;
                WakeConditionVariable(&writer_cv_);
                return;
            } else {
                if (freeze_requested_ && !has_pending_ && SerializeLiveIniLocked(pending_text_)) {
                    has_pending_ = true;
                    freeze_requested_ = false;
                }

                const bool overlay_open = display_commanderhooks::IsReShadeOverlayOpen();
                const LONGLONG now_ns = ::utils::get_now_ns();
                if (has_pending_ && deadline_armed_ && !overlay_open && now_ns >= deadline_ns_) {
                    first_text = pending_text_;
                    job_reason = latest_reason_;
                    have_first = true;
                    scheduled_write = true;
                }
            }
        }

        if (flush_kind != FlushKind::None) {
            if (have_first) {
                WriteTextAndReport(first_text, job_reason.c_str());
            }
            if (have_second) {
                WriteTextAndReport(second_text, job_reason.c_str());
            }
            {
                ::utils::SRWLockExclusive lock(config_mutex_);
                CompleteFlushLocked(flush_gen);
                if (stop_after) {
                    writer_stop_ = true;
                    writer_started_ = false;
                    WakeConditionVariable(&writer_cv_);
                    return;
                }
            }
            continue;
        }

        if (!scheduled_write || !have_first) {
            continue;
        }

        const bool ok = WriteTextAndReport(first_text, job_reason.c_str());
        {
            ::utils::SRWLockExclusive lock(config_mutex_);
            if (!ok) {
                ArmDeadlineLocked(::utils::get_now_ns());
                continue;
            }

            has_pending_ = false;
            pending_text_.clear();
            deadline_armed_ = false;
            deadline_ns_ = 0;

            if (dirty_after_ && SerializeLiveIniLocked(pending_text_)) {
                dirty_after_ = false;
                has_pending_ = true;
                ArmDeadlineLocked(::utils::get_now_ns());
            }
        }
    }
}

void DisplayCommanderConfigManager::SaveConfig(const char* reason) {
    {
        ::utils::SRWLockExclusive lock(config_mutex_);
        if (!initialized_) {
            return;
        }
    }
    EnsureWriterStarted();

    ::utils::SRWLockExclusive lock(config_mutex_);
    if (!initialized_) {
        return;
    }

    if (reason != nullptr && reason[0] != '\0') {
        latest_reason_ = reason;
    }

    if (!has_pending_ && !freeze_requested_) {
        freeze_requested_ = true;
        ArmDeadlineLocked(::utils::get_now_ns());
    } else {
        dirty_after_ = true;
    }
    WakeConditionVariable(&writer_cv_);
}

void DisplayCommanderConfigManager::FlushConfig(const char* reason) {
    RequestFlushAndWait(FlushKind::Immediate, reason);
}

void DisplayCommanderConfigManager::FlushConfigOnOverlayClose() {
    RequestFlushAndWait(FlushKind::OverlayClose, "reshade overlay closed");
}

void DisplayCommanderConfigManager::FlushConfigOnExit() {
    RequestFlushAndWait(FlushKind::Exit, "process exit");
    if (writer_thread_.joinable()) {
        writer_thread_.join();
    }
}

}  // namespace display_commander::config
