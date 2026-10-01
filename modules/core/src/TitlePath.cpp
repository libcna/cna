// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/TitlePath.hpp"

#include <mutex>
#include <utility>

namespace CNA::Internal
{
    namespace
    {
        std::mutex& TitlePathMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        std::optional<std::string>& TitlePathValue()
        {
            static std::optional<std::string> value;
            return value;
        }
    }

    void SetTitlePath(std::string path)
    {
        const std::lock_guard lock(TitlePathMutex());
        TitlePathValue() = std::move(path);
    }

    std::optional<std::string> TryGetTitlePath()
    {
        const std::lock_guard lock(TitlePathMutex());
        return TitlePathValue();
    }
}
