// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/GamerServices/LocalStoreLock.hpp"
#include <cerrno>
#include <system_error>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace CNA::Internal::GamerServices {
#if !defined(_WIN32) && !defined(__unix__) && !defined(__APPLE__)
namespace { std::mutex writerMutex; }
#endif
LocalStoreLock::LocalStoreLock(const std::filesystem::path& path) {
    auto lockPath = path;
    lockPath += ".lock";
#if defined(_WIN32)
    const auto handle = CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) throw std::system_error(GetLastError(), std::system_category(), "Open local store lock");
    BY_HANDLE_FILE_INFORMATION info{};
    OVERLAPPED overlapped{};
    if (!GetFileInformationByHandle(handle, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
        !LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, MAXDWORD, &overlapped)) {
        const auto error = GetLastError();
        CloseHandle(handle);
        throw std::system_error(error ? error : ERROR_ACCESS_DENIED, std::system_category(), "Acquire local store lock");
    }
    handle_ = handle;
#elif defined(__unix__) || defined(__APPLE__)
    descriptor_ = open(lockPath.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (descriptor_ < 0) throw std::system_error(errno, std::generic_category(), "Open local store lock");
    struct stat info{};
    if (fstat(descriptor_, &info) != 0 || !S_ISREG(info.st_mode)) {
        const auto error = errno;
        close(descriptor_); descriptor_ = -1;
        throw std::system_error(error ? error : EINVAL, std::generic_category(), "Invalid local store lock");
    }
    int result;
    do { result = flock(descriptor_, LOCK_EX); } while (result != 0 && errno == EINTR);
    if (result != 0) {
        const auto error = errno;
        close(descriptor_); descriptor_ = -1;
        throw std::system_error(error, std::generic_category(), "Acquire local store lock");
    }
#else
    // Platforms without independent native processes retain process-local serialization.
    processLock_ = std::unique_lock<std::mutex>(writerMutex);
    (void)lockPath;
#endif
}
LocalStoreLock::~LocalStoreLock() {
#if defined(_WIN32)
    if (handle_) CloseHandle(static_cast<HANDLE>(handle_));
#elif defined(__unix__) || defined(__APPLE__)
    if (descriptor_ >= 0) close(descriptor_);
#endif
}
}
