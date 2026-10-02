// SPDX-License-Identifier: MS-PL
#pragma once
#include <filesystem>
#include <mutex>

namespace CNA::Internal::GamerServices {
/** @brief Serializes a local store's complete read-modify-replace operation. */
class LocalStoreLock final {
public:
    /**
     * @brief Acquires the process and interprocess locks, refusing an unlocked write on failure.
     * @param path Store file whose stable sibling lock file is used; its parent must exist.
     */
    explicit LocalStoreLock(const std::filesystem::path& path);
    /** @brief Releases the locks; the lock file remains so waiters keep the same inode. */
    ~LocalStoreLock();
    /** @brief Lock ownership cannot be copied. */
    LocalStoreLock(const LocalStoreLock&) = delete;
    /** @brief Lock ownership cannot be assigned. */
    LocalStoreLock& operator=(const LocalStoreLock&) = delete;
private:
    std::unique_lock<std::mutex> processLock_;
    int descriptor_ = -1;
    void* handle_ = nullptr;
};
}
