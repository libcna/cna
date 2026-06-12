// SPDX-License-Identifier: MS-PL

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "CNA/CNAHelper.hpp"
#include "System/IO/Stream.hpp"

namespace Microsoft::Xna::Framework
{
    /// Provides access to title content files.
    class TitleContainer final
    {
    public:
        /// Static-only class; not instantiable.
        TitleContainer() = delete;

        /// Opens a title content file for reading.
        [[nodiscard]] static std::unique_ptr<System::IO::Stream> OpenStream(const std::string& name);

    private:
        using IntPtr = std::uintptr_t;

        [[nodiscard]] static std::string NormalizeFilePathSeparators(const std::string& name);
        [[nodiscard]] static bool IsPathRooted(const std::string& name);
        [[nodiscard]] static std::string CombineTitlePath(const std::string& name);
        [[nodiscard]] static std::string ResolveRealPath(const std::string& name);

        /// Reads a title content file into a newly allocated byte buffer (internal use).
        NOXNA [[nodiscard]] static void* ReadToPointer(const std::string& name, IntPtr& size);

        /// Releases a pointer returned by ReadToPointer.
        NOXNA static void FreePointer(void* pointer);
    };
}
