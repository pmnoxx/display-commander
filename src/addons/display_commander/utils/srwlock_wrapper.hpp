#pragma once

#include <windows.h>

namespace utils {

/**
 * RAII wrapper for SRWLOCK exclusive (write) locking
 * Automatically acquires the lock on construction and releases on destruction
 */
class SRWLockExclusive {
   public:
    explicit SRWLockExclusive(SRWLOCK& lock) : lock_(lock) { AcquireSRWLockExclusive(&lock_); }

    ~SRWLockExclusive() { ReleaseSRWLockExclusive(&lock_); }

    // Non-copyable, non-movable
    SRWLockExclusive(const SRWLockExclusive&) = delete;
    SRWLockExclusive& operator=(const SRWLockExclusive&) = delete;
    SRWLockExclusive(SRWLockExclusive&&) = delete;
    SRWLockExclusive& operator=(SRWLockExclusive&&) = delete;

   private:
    SRWLOCK& lock_;
};

/**
 * RAII wrapper for SRWLOCK shared (read) locking
 * Automatically acquires the lock on construction and releases on destruction
 */
class SRWLockShared {
   public:
    explicit SRWLockShared(SRWLOCK& lock) : lock_(lock) { AcquireSRWLockShared(&lock_); }

    ~SRWLockShared() { ReleaseSRWLockShared(&lock_); }

    // Non-copyable, non-movable
    SRWLockShared(const SRWLockShared&) = delete;
    SRWLockShared& operator=(const SRWLockShared&) = delete;
    SRWLockShared(SRWLockShared&&) = delete;
    SRWLockShared& operator=(SRWLockShared&&) = delete;

   private:
    SRWLOCK& lock_;
};

/**
 * Check if an SRWLOCK is currently held (for diagnostics only).
 * Uses TryAcquireSRWLockExclusive; does not block.
 * Returns true if the lock is held (try failed), false if free (try succeeded, then released).
 */
inline bool TryIsSRWLockHeld(SRWLOCK& lock) {
    if (TryAcquireSRWLockExclusive(&lock)) {
        ReleaseSRWLockExclusive(&lock);
        return false;  // Lock was free
    }
    return true;  // Lock is held
}

}  // namespace utils
